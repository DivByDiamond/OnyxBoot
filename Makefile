# Default cross-compiler prefix. We prefer `riscv64-elf-` because that's
# what the mainstream bare-metal toolchain (xpack-riscv-none-elf-gcc,
# riscv-collaborations/riscv-gnu-toolchain) installs on most distros.
#
# If you have the `riscv64-unknown-elf-` variant instead (Debian's
# `gcc-riscv64-unknown-elf` package), override with:
#   make CROSS=riscv64-unknown-elf
#
# If you have the Linux-targeted `riscv64-linux-gnu-` (Arch's
# `riscv64-linux-gnu-gcc`), bare-metal OnyxBoot won't link correctly —
# install a bare-metal variant instead.
CROSS ?= $(shell \
  if command -v riscv64-elf-gcc >/dev/null 2>&1; then echo riscv64-elf; \
  elif command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then echo riscv64-unknown-elf; \
  else echo riscv64-elf; fi)
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

test-all:
	./test/test_all.sh

.PHONY: all clean test test-all
