CROSS ?= riscv64-unknown-elf
CC = $(CROSS)-gcc
OBJCOPY = $(CROSS)-objcopy

CFLAGS = -march=rv64imafdc -mabi=lp64d -mcmodel=medany \
    -Os \
    -ffunction-sections -fdata-sections \
    -fomit-frame-pointer -fno-ident -g0 \
    -ffreestanding -nostdlib -Iinclude -Wall -Wextra -Wno-unused-function

LDFLAGS = -T linker.ld -nostdlib \
    -Wl,--gc-sections -Wl,--strip-all -Wl,-n

SRCS = src/boot_entry.c src/boot_main.c src/string.c fs/fat.c fs/ext4.c

all: bootloader.bin

bootloader.elf: $(SRCS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

bootloader.bin: bootloader.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f bootloader.elf bootloader.bin

test:
	./test/run_qemu.sh

.PHONY: all clean test
