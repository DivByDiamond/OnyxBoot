#pragma once
#include "types.h"
#include "uart.h"

typedef enum {
    DEV_NONE,
    DEV_VIRTIO,
    DEV_SDHCI,
} DevType;

typedef struct {
    DevType type;
    uint64_t base;
    uint32_t irq;
    bool avail;
} BootDevice;

static inline int boot_menu(UART* uart, BootDevice* devs, int ndevs) {
    uart_puts(uart, "\nOnyxBoot boot menu\n");
    uart_puts(uart, "--------------------\n");

    for (int i = 0; i < ndevs; i++) {
        if (!devs[i].avail) continue;
        const char* tname = devs[i].type == DEV_VIRTIO ? "VirtIO" : "MMC/SD";
        uart_puts(uart, "  ");
        uart_putchar(uart, '0' + i);
        uart_puts(uart, ": ");
        uart_puts(uart, tname);
        uart_puts(uart, " @ 0x");

        uint64_t b = devs[i].base;
        for (int j = 60; j >= 0; j -= 4) {
            uint8_t nib = (uint8_t)(b >> j) & 0xF;
            uart_putchar(uart, nib < 10 ? '0' + nib : 'a' + nib - 10);
        }
        uart_putchar(uart, '\n');
    }
    uart_puts(uart, "--------------------\n");
    uart_puts(uart, "Select device (0-");
    uart_putchar(uart, '0' + ndevs - 1);
    uart_puts(uart, ", or enter for auto): ");

    /* One selectable device means there is no choice to make: waiting for a
     * keypress would only stall boot. Measured cost of the spin below when
     * nothing is typed: ~8.3 s under QEMU TCG (20M MMIO polls) and seconds
     * on real hardware, i.e. most of the boot-to-login time. */
    int avail = 0;
    int first = -1;
    for (int i = 0; i < ndevs; i++) {
        if (devs[i].avail) {
            avail++;
            if (first < 0) first = i;
        }
    }
    if (avail == 1) {
        uart_puts(uart, "auto\n");
        return first;
    }

    char c = 0;
    for (volatile int timeout = 0; timeout < 20000000; timeout++) {
        if (uart_data_ready(uart)) {
            c = uart_read_char(uart);
            uart_putchar(uart, c);
            uart_putchar(uart, '\n');
            break;
        }
    }

    if (c >= '0' && c <= '9') {
        int sel = c - '0';
        if (sel < ndevs && devs[sel].avail)
            return sel;
    }

    /* Auto: return first available */
    for (int i = 0; i < ndevs; i++)
        if (devs[i].avail) return i;

    return -1;
}
