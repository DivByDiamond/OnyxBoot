#include "types.h"

typedef bool (*blk_read_t)(uint64_t lba, void* buf, void* priv);

#define S_IFMT  0xF000
#define S_IFDIR 0x4000
#define S_IFREG 0x8000

static const uint8_t EXT4_GUID[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

static inline uint32_t erd32(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}
static inline uint16_t erd16(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return (uint16_t)b[0] | ((uint16_t)b[1]<<8);
}

static bool rd_blk(blk_read_t rd, void* priv, uint64_t plba, uint32_t b, uint32_t bs, uint8_t* buf) {
    uint32_t s = bs / 512;
    for (uint32_t i = 0; i < s; i++)
        if (!rd(plba + (uint64_t)b * s + i, buf + i * 512, priv)) return false;
    return true;
}

static uint32_t bmap_ext(blk_read_t rd, void* priv, uint64_t plba, uint32_t bs,
                         const uint8_t* ino, uint32_t lb, uint32_t dsz,
                         bool extents, uint8_t* tmp) {
    (void)dsz; /* dsz is reserved for future extent-size validation. */
    if (extents) {
        const uint8_t* eh = ino + 40;
        uint16_t depth = erd16(eh + 6), entries = erd16(eh + 2);
        if (erd16(eh) != 0xF30A) return 0;
        while (depth > 0) {
            uint8_t idx_buf[4096];
            const uint8_t* idx = eh + 12;
            bool found = false;
            for (uint16_t i = 0; i < entries - 1; i++) {
                if (lb >= erd32(idx + i * 12) && lb < erd32(idx + (i + 1) * 12)) {
                    uint64_t leaf = (uint64_t)erd32(idx + i * 12 + 4) | ((uint64_t)erd16(idx + i * 12 + 8) << 32);
                    if (!rd_blk(rd, priv, plba, (uint32_t)leaf, bs, idx_buf)) return 0;
                    eh = idx_buf; depth = erd16(eh + 6); entries = erd16(eh + 2);
                    found = true; break;
                }
            }
            if (!found) {
                uint64_t leaf = (uint64_t)erd32(idx + (entries - 1) * 12 + 4) | ((uint64_t)erd16(idx + (entries - 1) * 12 + 8) << 32);
                if (!rd_blk(rd, priv, plba, (uint32_t)leaf, bs, idx_buf)) return 0;
                eh = idx_buf; depth = erd16(eh + 6); entries = erd16(eh + 2);
            }
        }
        for (uint16_t i = 0; i < entries; i++) {
            const uint8_t* ex = eh + 12 + i * 12;
            uint32_t block = erd32(ex);
            uint16_t len = erd16(ex + 4) & 0x7FFF;
            uint64_t start = (uint64_t)erd16(ex + 6) << 16 | erd32(ex + 8);
            if (lb >= block && lb < block + len)
                return (uint32_t)(start + lb - block);
        }
        return 0;
    }
    const uint32_t* blocks = (const uint32_t*)(ino + 40);
    uint32_t epb = bs / 4;
    if (lb < 12) return blocks[lb];
    if (lb < 12 + epb) {
        if (!blocks[12] || !rd_blk(rd, priv, plba, blocks[12], bs, tmp)) return 0;
        return erd32(tmp + (lb - 12) * 4);
    }
    lb -= 12 + epb;
    if (lb < epb * epb) {
        if (!blocks[13]) return 0;
        uint32_t n = lb / epb;
        if (!rd_blk(rd, priv, plba, blocks[13], bs, tmp)) return 0;
        uint32_t b1 = erd32(tmp + n * 4);
        if (!b1 || !rd_blk(rd, priv, plba, b1, bs, tmp)) return 0;
        return erd32(tmp + (lb % epb) * 4);
    }
    lb -= epb * epb;
    if (lb < epb * epb * epb) {
        if (!blocks[14]) return 0;
        if (!rd_blk(rd, priv, plba, blocks[14], bs, tmp)) return 0;
        uint32_t n1 = lb / (epb * epb), n2 = (lb / epb) % epb;
        uint32_t b1 = erd32(tmp + n1 * 4);
        if (!b1 || !rd_blk(rd, priv, plba, b1, bs, tmp)) return 0;
        uint32_t b2 = erd32(tmp + n2 * 4);
        if (!b2 || !rd_blk(rd, priv, plba, b2, bs, tmp)) return 0;
        return erd32(tmp + (lb % epb) * 4);
    }
    return 0;
}

static bool read_inode(blk_read_t rd, void* priv, uint64_t plba, uint32_t bs,
                       uint32_t isize, uint32_t ipg, uint32_t bgd_base,
                       uint32_t inum, uint8_t* ino, uint8_t* tmp) {
    uint32_t grp = (inum - 1) / ipg, idx = (inum - 1) % ipg, bgds = bs / 32;
    if (!rd_blk(rd, priv, plba, bgd_base + grp / bgds, bs, tmp)) return false;
    uint32_t itable = erd32(tmp + (grp % bgds) * 32 + 8);
    uint32_t off = itable * bs + idx * isize;
    if (!rd_blk(rd, priv, plba, off / bs, bs, tmp)) return false;
    for (uint32_t i = 0; i < isize; i++) ino[i] = tmp[(off % bs) + i];
    return true;
}

static bool find_ext4_part(blk_read_t read, void* priv, uint64_t* start) {
    uint8_t sec[512];
    if (!read(0, sec, priv)) return false;
    if (sec[0x1C2] == 0xEE) {
        if (!read(1, sec, priv)) return false;
        if (sec[0] == 'E' && sec[1] == 'F' && sec[2] == 'I' && sec[3] == ' ' &&
            sec[4] == 'P' && sec[5] == 'A' && sec[6] == 'R' && sec[7] == 'T') {
            uint32_t num = erd32(sec + 80), esz = erd32(sec + 84), eper = 512 / esz;
            uint64_t elba = erd32(sec + 72);
            if (esz < 56) esz = 56;
            for (uint32_t i = 0; i < num; i++) {
                uint8_t ent[512];
                uint32_t sn = (uint32_t)(elba + i / eper);
                if (i % eper == 0 && !read(sn, ent, priv)) return false;
                uint32_t off = (i % eper) * esz;
                bool match = true;
                for (int j = 0; j < 16; j++)
                    if (ent[off + j] != EXT4_GUID[j]) { match = false; break; }
                if (match) { *start = erd32(ent + off + 32); return true; }
            }
        }
    }
    if (sec[510] != 0x55 || sec[511] != 0xAA) return false;
    for (int i = 0; i < 4; i++) {
        if (sec[0x1BE + i * 16 + 4] == 0x83) {
            *start = erd32(sec + 0x1BE + i * 16 + 8);
            return true;
        }
    }
    return false;
}

bool ext4_read_file(blk_read_t read, void* priv, const char* path,
                    uint8_t* buf, uint32_t* size, uint32_t max_size) {
    uint64_t part_lba;
    if (!find_ext4_part(read, priv, &part_lba)) return false;
    uint8_t tmp[4096], ino[256];
    if (!read(part_lba + 2, tmp, priv)) return false;
    if (erd16(tmp + 56) != 0xEF53) return false;
    bool extents = (erd32(tmp + 96) & 0x40) != 0;
    uint32_t bs = 1024u << erd32(tmp + 24);
    uint32_t ipg = erd32(tmp + 40), isize = erd16(tmp + 88);
    if (!isize) isize = 128;
    uint32_t bgd_base = erd32(tmp + 20) + 1;

    const char* p = path;
    if (*p == '/') p++;
    uint32_t cur = 2;

    while (*p) {
        char comp[256];
        uint32_t clen = 0;
        while (*p && *p != '/' && clen < 255) comp[clen++] = *p++;
        if (*p == '/') p++;
        comp[clen] = 0;
        if (!clen) continue;

        if (!read_inode(read, priv, part_lba, bs, isize, ipg, bgd_base, cur, ino, tmp))
            return false;
        if ((erd16(ino) & S_IFMT) != S_IFDIR) return false;

        uint32_t dsz = erd32(ino + 4);
        cur = 0;
        uint32_t remain = dsz, lb = 0;
        while (remain > 0 && !cur) {
            uint32_t pb = bmap_ext(read, priv, part_lba, bs, ino, lb, dsz, extents, tmp);
            if (!pb || !rd_blk(read, priv, part_lba, pb, bs, tmp)) return false;
            uint32_t end = remain < bs ? remain : bs, pos = 0;
            while (pos < end) {
                uint32_t inum = erd32(tmp + pos);
                uint16_t rl = erd16(tmp + pos + 4);
                uint8_t nl = tmp[pos + 6];
                if (!rl) break;
                if (inum && nl == clen) {
                    bool match = true;
                    for (uint32_t k = 0; k < clen; k++)
                        if (tmp[pos + 8 + k] != (uint8_t)comp[k]) { match = false; break; }
                    if (match) { cur = inum; break; }
                }
                pos += rl;
            }
            remain -= end; lb++;
        }
        if (!cur) return false;
    }

    if (!read_inode(read, priv, part_lba, bs, isize, ipg, bgd_base, cur, ino, tmp))
        return false;
    if ((erd16(ino) & S_IFMT) != S_IFREG) return false;
    uint32_t fsz = erd32(ino + 4);
    if (fsz > max_size) fsz = max_size;
    *size = fsz;
    uint32_t remain = fsz, off = 0, lb = 0;
    while (remain) {
        uint32_t pb = bmap_ext(read, priv, part_lba, bs, ino, lb, fsz, extents, tmp);
        if (!pb || !rd_blk(read, priv, part_lba, pb, bs, tmp)) return false;
        uint32_t n = remain < bs ? remain : bs;
        for (uint32_t i = 0; i < n; i++) buf[off + i] = tmp[i];
        off += n; remain -= n; lb++;
    }
    return true;
}
