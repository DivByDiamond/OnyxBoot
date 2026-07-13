#include "types.h"

typedef bool (*blk_read_t)(uint64_t lba, void* buf, void* priv);

static const uint8_t FAT32_GUID[16] = {
    0xC1, 0x2A, 0x73, 0x28, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static inline uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Try GPT first, then MBR. Return partition start LBA. */
static bool find_fat_part(blk_read_t read, void* priv, uint64_t* start) {
    uint8_t sec[512];
    if (!read(0, sec, priv)) return false;
    /* Try GPT: protective MBR has type 0xEE at offset 0x1C2 */
    if (sec[0x1C2] == 0xEE) {
        if (!read(1, sec, priv)) return false;
        if (sec[0] == 'E' && sec[1] == 'F' && sec[2] == 'I' && sec[3] == ' ' &&
            sec[4] == 'P' && sec[5] == 'A' && sec[6] == 'R' && sec[7] == 'T') {
            uint64_t elba = rd32(sec + 72);
            uint32_t num = rd32(sec + 80);
            uint32_t esz = rd32(sec + 84);
            if (esz < 56) esz = 56;
            uint32_t eper = 512 / esz;
            for (uint32_t i = 0; i < num; i++) {
                uint8_t ent[512];
                uint32_t secn = (uint32_t)(elba + i / eper);
                if (i % eper == 0 && !read(secn, ent, priv)) return false;
                uint32_t off = (i % eper) * esz;
                bool match = true;
                for (int j = 0; j < 16; j++)
                    if (ent[off + j] != FAT32_GUID[j]) { match = false; break; }
                if (match) { *start = rd32(ent + off + 32); return true; }
            }
        }
    }
    /* MBR fallback: check 0x55AA, scan for 0x0B/0x0C */
    if (sec[510] != 0x55 || sec[511] != 0xAA) return false;
    for (int i = 0; i < 4; i++) {
        uint8_t* e = sec + 0x1BE + i * 16;
        uint8_t type = e[4];
        if (type == 0x0B || type == 0x0C) {
            *start = rd32(e + 8);
            return true;
        }
    }
    return false;
}

typedef struct {
    uint32_t part_lba;
    uint32_t sec_per_clus;
    uint32_t rsvd_sec;
    uint32_t fat_cnt;
    uint32_t root_clus;
    uint32_t fat_sec;
    uint32_t data_sec;
    blk_read_t read;
    void* priv;
} FAT32;

static bool fat32_init(FAT32* fs, blk_read_t r, void* p, uint64_t plba) {
    fs->read = r; fs->priv = p; fs->part_lba = (uint32_t)plba;
    uint8_t bpb[512];
    if (!fs->read(fs->part_lba, bpb, fs->priv)) return false;
    if (bpb[510] != 0x55 || bpb[511] != 0xAA) return false;
    if (rd16(bpb + 11) != 512) return false;
    fs->sec_per_clus = bpb[13];
    fs->rsvd_sec = rd16(bpb + 14);
    fs->fat_cnt = bpb[16];
    fs->root_clus = rd32(bpb + 44);
    fs->fat_sec = rd32(bpb + 36);
    fs->data_sec = fs->part_lba + fs->rsvd_sec + fs->fat_cnt * fs->fat_sec;
    return fs->sec_per_clus != 0 && fs->fat_cnt != 0;
}

static uint32_t fat32_next_clus(FAT32* fs, uint32_t clus, uint8_t* tmp) {
    uint32_t fat_off = clus * 4;
    if (!fs->read(fs->part_lba + fs->rsvd_sec + fat_off / 512, tmp, fs->priv)) return 0;
    uint32_t val = rd32(tmp + fat_off % 512) & 0x0FFFFFFF;
    return (val >= 0x0FFFFFF8) ? 0 : val;
}

static bool fat32_read_clus(FAT32* fs, uint32_t clus, uint8_t* buf, uint8_t* tmp) {
    (void)tmp; /* tmp is reserved for future double-buffering / prefetch. */
    uint32_t lba = fs->data_sec + (clus - 2) * fs->sec_per_clus;
    for (uint32_t i = 0; i < fs->sec_per_clus; i++)
        if (!fs->read(lba + i, buf + i * 512, fs->priv)) return false;
    return true;
}

static bool fat32_find_file(FAT32* fs, const char name[11], uint32_t* clus, uint32_t* size, uint8_t* tmp) {
    uint32_t cur = fs->root_clus;
    while (cur) {
        if (!fat32_read_clus(fs, cur, tmp, tmp + 512)) return false;
        for (uint32_t i = 0; i < fs->sec_per_clus * 512; i += 32) {
            uint8_t* e = tmp + i;
            if (e[0] == 0x00) return false;
            if (e[0] == 0xE5 || e[11] == 0x0F) continue;
            bool match = true;
            for (int j = 0; j < 11; j++)
                if (e[j] != (uint8_t)name[j]) { match = false; break; }
            if (!match) continue;
            *clus = rd16(e + 20) << 16 | rd16(e + 26);
            *size = rd32(e + 28);
            return true;
        }
        cur = fat32_next_clus(fs, cur, tmp + 512);
    }
    return false;
}

static void name_to_83(const char* name, char out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    const char* p = name;
    int ni = 0;
    while (*p && *p != '.' && ni < 8) {
        char c = *p++;
        if (c >= 'a' && c <= 'z') c -= 32;
        out[ni++] = c;
    }
    if (*p == '.') {
        p++;
        int ei = 0;
        while (*p && ei < 3) {
            char c = *p++;
            if (c >= 'a' && c <= 'z') c -= 32;
            out[8 + ei++] = c;
        }
    }
}

bool fat32_read_file(blk_read_t read, void* priv, const char* name,
                     uint8_t* buf, uint32_t* size, uint32_t max_size) {
    uint64_t plba;
    if (!find_fat_part(read, priv, &plba)) return false;
    FAT32 fs;
    if (!fat32_init(&fs, read, priv, plba)) return false;
    char name83[11];
    name_to_83(name, name83);
    uint8_t tmp[1024];
    uint32_t fclus, fsize;
    if (!fat32_find_file(&fs, name83, &fclus, &fsize, tmp)) return false;
    if (fsize > max_size) fsize = max_size;
    *size = fsize;
    uint32_t off = 0;
    while (fclus && off < fsize) {
        if (!fat32_read_clus(&fs, fclus, buf + off, tmp)) return false;
        off += fs.sec_per_clus * 512;
        fclus = fat32_next_clus(&fs, fclus, tmp);
    }
    return true;
}
