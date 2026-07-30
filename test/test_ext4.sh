#!/bin/sh
# test_ext4.sh - QEMU test: MBR partition table + ext4 filesystem
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

echo "==> Creating MBR+ext4 disk image"
dd if=/dev/zero of="$TEST_DIR/test.img" bs=1M count=64 2>/dev/null

echo 'label: dos
start=2048, type=83' | sfdisk "$TEST_DIR/test.img"

# Create ext4 filesystem on a temporary image, copy kernel via debugfs, then inject
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
    echo "[PASS] MBR+ext4"
    rm -f "$TEST_DIR/qemu_output.log" "$TEST_DIR/test.img" "$TEST_DIR/kernel.elf"
    exit 0
else
    echo "[FAIL] MBR+ext4 - \"Hello from test kernel!\" not found"
    echo "--- QEMU output ---"
    cat "$TEST_DIR/qemu_output.log"
    echo "-------------------"
    rm -f "$TEST_DIR/qemu_output.log" "$TEST_DIR/test.img" "$TEST_DIR/kernel.elf"
    exit 1
fi
