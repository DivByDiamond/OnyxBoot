#pragma once
#include "types.h"

#define VIRTIO_QUEUE_SIZE 8

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtio_desc;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_QUEUE_SIZE];
} virtio_avail;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtio_used_elem;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtio_used_elem ring[VIRTIO_QUEUE_SIZE];
} virtio_used;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
} virtio_blk_req;

typedef struct __attribute__((packed, aligned(16))) {
    virtio_desc desc[VIRTIO_QUEUE_SIZE];
    virtio_avail avail;
    uint16_t used_event;
    uint8_t _pad[10];
    virtio_used used;
} VirtQueue;

extern VirtQueue g_vq __attribute__((aligned(4096)));

typedef struct {
    volatile uint32_t* regs;
    bool legacy;
    uint16_t last_used_idx;
} VirtIOBlock;

static inline void vq_init(void) {
    for (int i = 0; i < VIRTIO_QUEUE_SIZE; i++) {
        g_vq.desc[i].addr = 0;
        g_vq.desc[i].len = 0;
        g_vq.desc[i].flags = 0;
        g_vq.desc[i].next = 0;
    }
    g_vq.avail.flags = 0;
    g_vq.avail.idx = 0;
    for (int i = 0; i < VIRTIO_QUEUE_SIZE; i++) {
        g_vq.avail.ring[i] = 0;
        g_vq.used.ring[i].id = 0;
        g_vq.used.ring[i].len = 0;
    }
    g_vq.used.flags = 0;
    g_vq.used.idx = 0;
}

static inline void vio_write(VirtIOBlock* d, uint32_t off, uint32_t val) {
    d->regs[off / 4] = val;
}

static inline uint32_t vio_read(VirtIOBlock* d, uint32_t off) {
    return d->regs[off / 4];
}

static inline bool vio_setup_queue_legacy(VirtIOBlock* d) {
    vio_write(d, 0x028, 4096);
    vio_write(d, 0x030, 0);
    uint32_t max = vio_read(d, 0x034);
    if (max < VIRTIO_QUEUE_SIZE) return false;
    vio_write(d, 0x038, VIRTIO_QUEUE_SIZE);
    vio_write(d, 0x03C, 16);
    uint64_t pa = (uint64_t)(uintptr_t)&g_vq;
    vio_write(d, 0x040, (uint32_t)(pa >> 12));
    vio_write(d, 0x070, 4);
    return true;
}

static inline bool vio_setup_queue_modern(VirtIOBlock* d) {
    vio_write(d, 0x030, 0);
    uint32_t max = vio_read(d, 0x034);
    if (max < VIRTIO_QUEUE_SIZE) return false;
    vio_write(d, 0x038, VIRTIO_QUEUE_SIZE);

    uint64_t desc_pa  = (uint64_t)(uintptr_t)g_vq.desc;
    uint64_t avail_pa = (uint64_t)(uintptr_t)&g_vq.avail;
    uint64_t used_pa  = (uint64_t)(uintptr_t)&g_vq.used;

    vio_write(d, 0x080, (uint32_t)(desc_pa));
    vio_write(d, 0x084, (uint32_t)(desc_pa >> 32));
    vio_write(d, 0x090, (uint32_t)(avail_pa));
    vio_write(d, 0x094, (uint32_t)(avail_pa >> 32));
    vio_write(d, 0x0A0, (uint32_t)(used_pa));
    vio_write(d, 0x0A4, (uint32_t)(used_pa >> 32));

    vio_write(d, 0x03C, 1);
    vio_write(d, 0x070, 4);
    return true;
}

static inline bool vio_probe(VirtIOBlock* d) {
    if (vio_read(d, 0x000) != 0x74726976) return false;
    uint32_t ver = vio_read(d, 0x004);
    if (ver != 1 && ver != 2) return false;
    if (vio_read(d, 0x008) != 2) return false;
    d->legacy = (ver == 1);

    vio_write(d, 0x070, 0);
    vio_write(d, 0x070, 1);
    vio_write(d, 0x070, 2);

    vio_write(d, 0x014, 0);
    uint32_t feat_lo = vio_read(d, 0x010);
    vio_write(d, 0x014, 1);
    uint32_t feat_hi = vio_read(d, 0x010);

    uint32_t drv_lo = feat_lo;
    uint32_t drv_hi = feat_hi | 1;

    vio_write(d, 0x024, 0);
    vio_write(d, 0x020, drv_lo);
    vio_write(d, 0x024, 1);
    vio_write(d, 0x020, drv_hi);

    vio_write(d, 0x070, 8);
    if (!(vio_read(d, 0x070) & 8)) return false;

    if (d->legacy)
        return vio_setup_queue_legacy(d);
    else
        return vio_setup_queue_modern(d);
}

static inline bool vio_read_sectors(VirtIOBlock* d, uint64_t lba, uint32_t count, void* buf) {
    virtio_blk_req hdr;
    hdr.type = 0;
    hdr.ioprio = 0;
    hdr.sector = lba;
    uint8_t status = 0xFF;

    uint16_t desc_idx = g_vq.avail.idx % VIRTIO_QUEUE_SIZE;

    g_vq.desc[desc_idx].addr  = (uint64_t)(uintptr_t)&hdr;
    g_vq.desc[desc_idx].len   = 16;
    g_vq.desc[desc_idx].flags = 1;
    g_vq.desc[desc_idx].next  = (desc_idx + 1) % VIRTIO_QUEUE_SIZE;

    uint16_t d1 = (desc_idx + 1) % VIRTIO_QUEUE_SIZE;
    g_vq.desc[d1].addr  = (uint64_t)(uintptr_t)buf;
    g_vq.desc[d1].len   = count * 512u;
    g_vq.desc[d1].flags = 3;
    g_vq.desc[d1].next  = (desc_idx + 2) % VIRTIO_QUEUE_SIZE;

    uint16_t d2 = (desc_idx + 2) % VIRTIO_QUEUE_SIZE;
    g_vq.desc[d2].addr  = (uint64_t)(uintptr_t)&status;
    g_vq.desc[d2].len   = 1;
    g_vq.desc[d2].flags = 2;

    __sync_synchronize();

    g_vq.avail.ring[g_vq.avail.idx % VIRTIO_QUEUE_SIZE] = desc_idx;
    __sync_synchronize();
    g_vq.avail.idx++;
    __sync_synchronize();

    vio_write(d, d->legacy ? 0x050 : 0x040, 0);

    while (g_vq.used.idx == d->last_used_idx)
        __sync_synchronize();
    d->last_used_idx = g_vq.used.idx;

    return status == 0;
}

/* Single-sector helper kept for callers that only ever need one sector. */
static inline bool vio_read_sector(VirtIOBlock* d, uint64_t lba, void* buf) {
    return vio_read_sectors(d, lba, 1, buf);
}

static inline void vio_init(VirtIOBlock* d, uint64_t base) {
    d->regs = (volatile uint32_t*)base;
    d->legacy = false;
    d->last_used_idx = 0;
}
