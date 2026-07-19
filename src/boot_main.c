#include "types.h"
#include "uart.h"
#include "virtio.h"
#include "sdhci.h"
#include "elf.h"
#include "fdt.h"
#include "bootmenu.h"

extern uint8_t _sbss[];
extern uint8_t _stack_end[];

VirtQueue g_vq __attribute__((aligned(4096)));

typedef bool (*blk_read_t)(uint64_t lba, void* buf, void* priv);

bool fat32_read_file(blk_read_t read, void* priv, const char* name,
                     uint8_t* buf, uint32_t* size, uint32_t max_size);
bool ext4_read_file(blk_read_t read, void* priv, const char* path,
                    uint8_t* buf, uint32_t* size, uint32_t max_size);

/* Wrappers for VirtIO and SDHCI read_sector */
static bool vio_read_wrap(uint64_t lba, void* buf, void* priv) {
    return vio_read_sector((VirtIOBlock*)priv, lba, buf);
}
static bool sdhci_read_wrap(uint64_t lba, void* buf, void* priv) {
    return sdhci_read_sector((SDHCI*)priv, lba, buf);
}

/* ----------------------------------------------------------------------- */
/* Reserved-region tracking for safe kernel buffer / ELF placement.        */
/* ----------------------------------------------------------------------- */

#define MAX_BOOT_DEVS  32   /* was 16; 8 VirtIO + 8 SDHCI + headroom */
#define MAX_RESV_REGS  32   /* FDT + initrd + reserved-memory children */

typedef struct { uint64_t start; uint64_t end; } region_t;

typedef struct {
    region_t items[MAX_RESV_REGS];
    int count;
} region_list_t;

static bool overlaps(const region_t* a, uint64_t s, uint64_t e) {
    return s < a->end && e > a->start;
}

static bool overlaps_any(const region_list_t* rl, uint64_t s, uint64_t e) {
    for (int i = 0; i < rl->count; i++)
        if (overlaps(&rl->items[i], s, e)) return true;
    return false;
}

static void region_add(region_list_t* rl, uint64_t start, uint64_t end) {
    if (rl->count >= MAX_RESV_REGS) return;
    if (end <= start) return;
    rl->items[rl->count].start = start;
    rl->items[rl->count].end = end;
    rl->count++;
}

/* Callback for fdt_for_each_reserved(): stash each region. */
static bool collect_reserved_cb(uint64_t base, uint64_t size, void* arg) {
    region_list_t* rl = (region_list_t*)arg;
    region_add(rl, base, base + size);
    return true;  /* keep going */
}

/* ----------------------------------------------------------------------- */
/* Boot main.                                                              */
/* ----------------------------------------------------------------------- */

void boot_main(uint64_t fdt_addr) {
    uart_info ui = fdt_find_uart((const void*)fdt_addr);
    UART uart;
    uart.regs = (volatile uint8_t*)ui.base;
    uart.shift = ui.reg_shift;
    uart_init(&uart);

    const char* model = fdt_get_model((const void*)fdt_addr, "unknown");

    uart_puts(&uart, "     ███████\n");
    uart_puts(&uart, "  ███░░░░░███\n");
    uart_puts(&uart, " ███     ░░███ ████████   █████ ████ █████ █████\n");
    uart_puts(&uart, "░███      ░███░░███░░███ ░░███ ░███ ░░███ ░░███\n");
    uart_puts(&uart, "░███      ░███ ░███ ░███  ░███ ░███  ░░░█████░\n");
    uart_puts(&uart, "░░███     ███  ░███ ░███  ░███ ░███   ███░░░███\n");
    uart_puts(&uart, " ░░░███████░   ████ █████ ░░███████  █████ █████\n");
    uart_puts(&uart, "   ░░░░░░░    ░░░░ ░░░░░   ░░░░░███ ░░░░░ ░░░░░\n");
    uart_puts(&uart, "                           ███ ░███\n");
    uart_puts(&uart, "                          ░░██████\n");
    uart_puts(&uart, "                           ░░░░░░\n");

    uart_puts(&uart, "OnyxBoot v0.4 [");
    uart_puts(&uart, model);
    uart_puts(&uart, "]\n");

    BootDevice devs[MAX_BOOT_DEVS];
    int ndevs = 0;

    mmio_dev virtio_devs[8];
    int nv = fdt_find_virtio((const void*)fdt_addr, virtio_devs, 8);
    for (int i = 0; i < nv && ndevs < MAX_BOOT_DEVS; i++) {
        VirtIOBlock disk;
        vio_init(&disk, virtio_devs[i].base);
        if (vio_probe(&disk)) {
            devs[ndevs].type = DEV_VIRTIO;
            devs[ndevs].base = virtio_devs[i].base;
            devs[ndevs].irq = virtio_devs[i].irq;
            devs[ndevs].avail = true;
            ndevs++;
        }
    }

    mmio_dev sdhci_devs[8];
    int ns = fdt_find_sdhci((const void*)fdt_addr, sdhci_devs, 8);
    for (int i = 0; i < ns && ndevs < MAX_BOOT_DEVS; i++) {
        SDHCI mmc;
        sdhci_init(&mmc, sdhci_devs[i].base);
        if (sdhci_probe(&mmc)) {
            devs[ndevs].type = DEV_SDHCI;
            devs[ndevs].base = sdhci_devs[i].base;
            devs[ndevs].irq = sdhci_devs[i].irq;
            devs[ndevs].avail = true;
            ndevs++;
        }
    }

    if (ndevs < 1) {
        uart_puts(&uart, "no boot device\n");
        while (1) ;
    }

    int sel = boot_menu(&uart, devs, ndevs);
    if (sel < 0 || sel >= ndevs || !devs[sel].avail) {
        uart_puts(&uart, "invalid device\n");
        while (1) ;
    }

    mem_info dram = fdt_find_memory((const void*)fdt_addr);

    /* ---- Fix #2: refuse to boot on absurdly small RAM ----
     * The original code did `dram.size - 0x10000` in uint64; if
     * dram.size < 0x10000 the subtraction underflows and the cast to
     * uint32_t produces a multi-GB "max_size", which then gets used as
     * the kernel buffer length. Bail out explicitly instead. */
    if (dram.size <= 0x10000) {
        uart_puts(&uart, "RAM too small (<64KiB)\n");
        while (1) ;
    }
    uint32_t max_size = 4 * 1024 * 1024;
    if (dram.size < (uint64_t)max_size + 0x10000)
        max_size = (uint32_t)(dram.size - 0x10000);
    if (max_size < 64 * 1024) {
        uart_puts(&uart, "no room for kernel\n");
        while (1) ;
    }

    /* ---- Fix #6: build a reserved-region list and place the
     * kernel buffer so it does not collide with FDT, initrd,
     * reserved-memory, or the bootloader itself. */
    region_list_t resv = {0};

    /* 1. FDT blob itself. */
    uint64_t fdt_size = fdt_totalsize((const void*)fdt_addr);
    if (fdt_size != 0)
        region_add(&resv, fdt_addr, fdt_addr + fdt_size);

    /* 2. initrd (if /chosen describes one). */
    uint64_t ird_start = 0, ird_end = 0;
    if (fdt_find_initrd((const void*)fdt_addr, &ird_start, &ird_end))
        region_add(&resv, ird_start, ird_end);

    /* 3. /reserved-memory children. */
    fdt_for_each_reserved((const void*)fdt_addr, collect_reserved_cb, &resv);

    /* 4. Bootloader itself (text + data + bss + stack). */
    uint64_t boot_start = (uint64_t)_sbss;
    uint64_t boot_end   = (uint64_t)_stack_end;
    region_add(&resv, boot_start, boot_end);

    /* Iteratively push the kernel buffer downward until it fits in a
     * free slice of RAM. We start from the top of RAM because the
     * original layout assumed that, and only retreat when something
     * is in the way. The buffer is [ps, ps + max_size); at each step
     * we look for a conflicting reserved region and, if one exists,
     * slide ps down so the whole buffer sits below r->start. */
    uint64_t ps = (dram.base + dram.size - max_size) & ~(uint64_t)0xFFF;
    for (int iter = 0; iter < 128; iter++) {
        uint64_t pe = ps + max_size;
        uint64_t new_ps = ps;
        for (int i = 0; i < resv.count; i++) {
            const region_t* r = &resv.items[i];
            if (ps < r->end && pe > r->start) {
                /* Conflict: push ps below r->start - max_size. */
                uint64_t candidate = (r->start > max_size)
                    ? ((r->start - max_size) & ~(uint64_t)0xFFF)
                    : 0;
                if (candidate < new_ps) new_ps = candidate;
            }
        }
        if (new_ps == ps) break;  /* no conflict, done */
        ps = new_ps;
        if (ps < dram.base + 0x10000) {
            uart_puts(&uart, "cannot place kernel buffer\n");
            while (1) ;
        }
    }

    uint8_t* kernel_buf = (uint8_t*)ps;
    uint32_t kernel_size = 0;
    bool ok = false;

    uart_puts(&uart, "loading kernel.elf\n");

    if (devs[sel].type == DEV_VIRTIO) {
        VirtIOBlock disk;
        vio_init(&disk, devs[sel].base);
        if (vio_probe(&disk)) {
            ok = fat32_read_file(vio_read_wrap, &disk, "kernel.elf",
                                 kernel_buf, &kernel_size, max_size)
              || ext4_read_file(vio_read_wrap, &disk, "kernel.elf",
                                kernel_buf, &kernel_size, max_size);
        }
    } else {
        SDHCI mmc;
        sdhci_init(&mmc, devs[sel].base);
        if (sdhci_probe(&mmc)) {
            ok = fat32_read_file(sdhci_read_wrap, &mmc, "kernel.elf",
                                 kernel_buf, &kernel_size, max_size)
              || ext4_read_file(sdhci_read_wrap, &mmc, "kernel.elf",
                                kernel_buf, &kernel_size, max_size);
        }
    }

    if (!ok) {
        uart_puts(&uart, "read error\n");
        while (1) ;
    }

    ELF64 elf;
    elf64_init(&elf, kernel_buf);
    if (!elf64_valid(&elf)) {
        uart_puts(&uart, "bad ELF\n");
        while (1) ;
    }

    /* Bootloader overlap (existing check). */
    if (!elf64_check_safe(&elf, boot_start, boot_end)) {
        uart_puts(&uart, "ELF overlaps bootloader\n");
        while (1) ;
    }

    /* Fix #6 (continued): also make sure no PT_LOAD segment lands on
     * top of FDT / initrd / reserved-memory. */
    {
        const elf64_ehdr* eh = elf.ehdr;
        const elf64_phdr* ph = (const elf64_phdr*)((const uint8_t*)eh + eh->e_phoff);
        for (int i = 0; i < eh->e_phnum; i++) {
            if (ph[i].p_type != 1) continue;
            uint64_t ss = ph[i].p_paddr;
            uint64_t se = ss + ph[i].p_memsz;
            if (overlaps_any(&resv, ss, se)) {
                uart_puts(&uart, "ELF overlaps reserved memory\n");
                while (1) ;
            }
        }
    }

    elf64_load_all(&elf);

    uart_puts(&uart, "jumping to kernel\n");

    uint64_t entry = elf64_entry(&elf);
    void (*kernel_entry)(uint64_t, uint64_t) = (void (*)(uint64_t, uint64_t))entry;
    kernel_entry(0, fdt_addr);
}
