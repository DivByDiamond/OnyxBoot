#pragma once
#include "types.h"

/* SDHCI register offsets */
#define SDHCI_DMA_ADDR     0x00
#define SDHCI_BLK_SIZE     0x04
#define SDHCI_BLK_CNT      0x06
#define SDHCI_ARGUMENT     0x08
#define SDHCI_TRANS_MODE   0x0C
#define SDHCI_COMMAND      0x0E
#define SDHCI_RESP0        0x10
#define SDHCI_RESP1        0x14
#define SDHCI_RESP2        0x18
#define SDHCI_RESP3        0x1C
#define SDHCI_DATA_PORT    0x20
#define SDHCI_PRES_STATE   0x24
#define SDHCI_PWR_CTRL     0x28
#define SDHCI_CLK_CTRL     0x2C
#define SDHCI_TIMEOUT_CTRL 0x2E
#define SDHCI_SW_RESET     0x2F
#define SDHCI_INT_STAT     0x30
#define SDHCI_INT_EN       0x34
#define SDHCI_SIG_EN       0x38
#define SDHCI_CAPS         0x40

#define PRES_CMD_INHIBIT   (1 << 0)
#define PRES_DAT_INHIBIT   (1 << 1)
#define PRES_CARD_INSERT   (1 << 16)
#define PRES_CARD_STABLE   (1 << 17)

#define INT_CMD_COMPLETE   (1 << 0)
#define INT_XFER_COMPLETE  (1 << 1)
#define INT_BUF_READ       (1 << 4)
#define INT_BUF_WRITE      (1 << 5)
#define INT_ERROR          (1 << 15)

#define SD_CMD0    0
#define SD_CMD2    2
#define SD_CMD3    3
#define SD_CMD7    7
#define SD_CMD8    8
#define SD_CMD9    9
#define SD_CMD12   12
#define SD_CMD16   16
#define SD_CMD17   17
#define SD_CMD55   55
#define SD_ACMD41  41

typedef struct {
    volatile uint32_t* regs;
    uint16_t rca;
    bool sd_mode;
} SDHCI;

static inline void sd_wr(SDHCI* s, uint32_t off, uint32_t v) { s->regs[off / 4] = v; }
static inline uint32_t sd_rd(SDHCI* s, uint32_t off) { return s->regs[off / 4]; }

static inline bool sd_wait_bits(SDHCI* s, uint32_t mask, bool set, uint32_t timeout) {
    for (uint32_t i = 0; i < timeout; i++) {
        if (!!(sd_rd(s, SDHCI_PRES_STATE) & mask) == set) return true;
    }
    return false;
}

static inline bool sd_wait_int(SDHCI* s, uint32_t want, uint32_t timeout) {
    for (uint32_t i = 0; i < timeout; i++) {
        uint32_t st = sd_rd(s, SDHCI_INT_STAT);
        if (st & INT_ERROR) { sd_wr(s, SDHCI_INT_STAT, st & INT_ERROR); return false; }
        if (st & want) { sd_wr(s, SDHCI_INT_STAT, st & (want | INT_ERROR)); return true; }
    }
    return false;
}

static inline bool sd_send_cmd(SDHCI* s, uint8_t idx, uint32_t arg, int resp_type) {
    if (!sd_wait_bits(s, PRES_CMD_INHIBIT, false, 100000))
        return false;

    uint16_t cmd = idx;
    cmd |= (uint16_t)(resp_type & 3) << 6;
    if (resp_type == 1 || resp_type == 3) cmd |= 1 << 8;
    if (resp_type == 3) cmd |= 1 << 9;
    if (resp_type < 0) { cmd |= 1 << 10; resp_type = -resp_type; }

    sd_wr(s, SDHCI_ARGUMENT, arg);
    sd_wr(s, SDHCI_COMMAND, cmd);

    if (resp_type != 0)
        return sd_wait_int(s, INT_CMD_COMPLETE, 200000);
    for (volatile int i = 0; i < 500; i++) ;
    return true;
}

static inline uint32_t sd_resp(SDHCI* s, int n) { return sd_rd(s, SDHCI_RESP0 + n * 4); }

static inline bool sdhci_probe(SDHCI* s) {
    sd_wr(s, SDHCI_SW_RESET, 7);
    for (int i = 0; i < 10000; i++)
        if (!(sd_rd(s, SDHCI_SW_RESET) & 7)) break;

    sd_wr(s, SDHCI_INT_EN, 0xFFFF);
    sd_wr(s, SDHCI_SIG_EN, 0xFFFF);
    sd_wr(s, SDHCI_INT_STAT, 0xFFFF);

    sd_wr(s, SDHCI_PWR_CTRL, 0x0E);
    sd_wr(s, SDHCI_CLK_CTRL, 0xFA);
    for (int i = 0; i < 10000; i++)
        if (sd_rd(s, SDHCI_CLK_CTRL) & 2) break;

    sd_wr(s, SDHCI_TIMEOUT_CTRL, 0x0F);

    if (!(sd_rd(s, SDHCI_PRES_STATE) & PRES_CARD_INSERT))
        return false;

    /* CMD0: go idle */
    sd_send_cmd(s, SD_CMD0, 0, 0);

    /* CMD8: check SDHC */
    if (!sd_send_cmd(s, SD_CMD8, 0x1AA, 3))
        return false;
    if ((sd_resp(s, 0) & 0xFFF) != 0x1AA)
        return false;

    /* ACMD41: init */
    int ok = 0;
    for (int i = 0; i < 1000; i++) {
        sd_send_cmd(s, SD_CMD55, 0, 3);
        if (!sd_send_cmd(s, SD_ACMD41, 0x40FF8000, 2))
            continue;
        if (sd_resp(s, 0) & (1 << 31)) { ok = 1; break; }
    }
    if (!ok) return false;
    s->sd_mode = (sd_resp(s, 0) & (1 << 30)) != 0;

    /* CMD2: CID */
    if (!sd_send_cmd(s, SD_CMD2, 0, 1)) return false;
    /* CMD3: RCA */
    if (!sd_send_cmd(s, SD_CMD3, 0, 3)) return false;
    s->rca = (uint16_t)(sd_resp(s, 0) >> 16);
    /* CMD9: CSD */
    if (!sd_send_cmd(s, SD_CMD9, (uint32_t)s->rca << 16, 1)) return false;
    /* CMD7: select */
    if (!sd_send_cmd(s, SD_CMD7, (uint32_t)s->rca << 16, 3)) return false;
    /* CMD16: block size */
    if (!sd_send_cmd(s, SD_CMD16, 512, 3)) return false;

    return true;
}

static inline bool sdhci_read_sector(SDHCI* s, uint64_t lba, void* buf) {
    uint32_t arg = s->sd_mode ? (uint32_t)lba : (uint32_t)(lba * 512);

    sd_wr(s, SDHCI_BLK_SIZE, 512);
    sd_wr(s, SDHCI_BLK_CNT, 1);
    /* read, block count enable, single */
    sd_wr(s, SDHCI_TRANS_MODE, (1 << 4) | (1 << 1));

    if (!sd_send_cmd(s, SD_CMD17, arg, -3))
        return false;

    if (!sd_wait_int(s, INT_BUF_READ, 500000))
        return false;

    uint32_t* dst = (uint32_t*)buf;
    for (int i = 0; i < 128; i++)
        dst[i] = sd_rd(s, SDHCI_DATA_PORT);

    return sd_wait_int(s, INT_XFER_COMPLETE, 2000000);
}

/* Multi-sector read for the block layer. SDHCI multi-block mode is
 * hardware-sensitive, so each sector still goes through one CMD17; the
 * count-based API only spares the callers a loop. */
static inline bool sdhci_read_sectors(SDHCI* s, uint64_t lba, uint32_t count, void* buf) {
    uint8_t* dst = (uint8_t*)buf;
    for (uint32_t i = 0; i < count; i++)
        if (!sdhci_read_sector(s, lba + i, dst + i * 512u)) return false;
    return true;
}

static inline void sdhci_init(SDHCI* s, uint64_t base) {
    s->regs = (volatile uint32_t*)base;
    s->rca = 0;
    s->sd_mode = false;
}
