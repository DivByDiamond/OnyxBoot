#!/bin/sh
# test_fallback.sh — QEMU test: GPT disk with ONLY ext4 (no FAT32).
# Bootloader must fall back from FAT32→ext4 and successfully load the kernel.
set -e

if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
    CROSS="${CROSS:-riscv64-unknown-elf}"
elif command -v riscv64-elf-gcc >/dev/null 2>&1; then
    CROSS="${CROSS:-riscv64-elf}"
else
    CROSS="${CROSS:-riscv64-unknown-elf}"
fi
TEST_DIR="$(dirname "$0")"
TOP_DIR="$(dirname "$TEST_DIR")"

echo "==> Building test kernel"
$CROSS-gcc -march=rv64gc -mabi=lp64d -mcmodel=medany -ffreestanding -nostdlib \
    -O2 -Wall -Wextra "$TEST_DIR/kernel.c" -T "$TEST_DIR/kernel.ld" -nostdlib \
    -o "$TEST_DIR/kernel.elf"

echo "==> Building bootloader"
make -C "$TOP_DIR" CROSS="$CROSS" clean all

echo "==> Creating GPT+ext4 disk image (NO FAT32)"
dd if=/dev/zero of="$TEST_DIR/test.img" bs=1M count=64 2>/dev/null

# GPT partition with Linux filesystem GUID (no FAT32 GUID)
echo 'label: gpt
start=2048, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4' | sfdisk "$TEST_DIR/test.img"

part_start=2048
part_sectors=$((64 * 1024 * 1024 / 512 - part_start))

dd if=/dev/zero of="$TEST_DIR/ext4_part.img" bs=512 count=$part_sectors 2>/dev/null
mkfs.ext4 -q "$TEST_DIR/ext4_part.img"

echo "==> Copying kernel.elf to ext4 image via debugfs"
debugfs -w -R "write $TEST_DIR/kernel.elf kernel.elf" "$TEST_DIR/ext4_part.img"

dd if="$TEST_DIR/ext4_part.img" of="$TEST_DIR/test.img" bs=512 seek=$part_start conv=notrunc 2>/dev/null
rm -f "$TEST_DIR/ext4_part.img"

echo "==> Starting QEMU (timeout 30s)"
timeout 30 qemu-system-riscv64 -M virt -m 256M \
    -bios "$TOP_DIR/bootloader.bin" \
    -drive file="$TEST_DIR/test.img",format=raw,if=none,id=drive0 \
    -device virtio-blk-device,drive=drive0 \
    -nographic -serial mon:stdio > "$TEST_DIR/qemu_output.log" 2>&1 || true

echo "==> Checking output"
if grep -q "Hello from test kernel!" "$TEST_DIR/qemu_output.log"; then
    echo "[PASS] fallback (FAT32→ext4)"
    rm -f "$TEST_DIR/qemu_output.log" "$TEST_DIR/test.img" "$TEST_DIR/kernel.elf"
    exit 0
else
    echo "[FAIL] fallback — \"Hello from test kernel!\" not found"
    echo "--- QEMU output ---"
    cat "$TEST_DIR/qemu_output.log"
    echo "-------------------"
    rm -f "$TEST_DIR/qemu_output.log" "$TEST_DIR/test.img" "$TEST_DIR/kernel.elf"
    exit 1
fi
