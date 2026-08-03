#!/bin/sh
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

# Build test kernel
echo "==> Building test kernel"
$CROSS-gcc -march=rv64gc -mabi=lp64d -mcmodel=medany -ffreestanding -nostdlib \
    -O2 -Wall -Wextra "$TEST_DIR/kernel.c" -T "$TEST_DIR/kernel.ld" -nostdlib \
    -o "$TEST_DIR/kernel.elf"

# Build bootloader
echo "==> Building bootloader"
make -C "$TOP_DIR" CROSS="$CROSS" clean all

# Create FAT32 disk image
echo "==> Creating FAT32 disk image"
dd if=/dev/zero of="$TEST_DIR/test.img" bs=1M count=64 2>/dev/null
parted -s "$TEST_DIR/test.img" mklabel msdos
parted -s "$TEST_DIR/test.img" mkpart primary fat32 1M 100%
part_start=2048
mkfs.fat -F 32 "$TEST_DIR/test.img" --offset=$part_start
mcopy -i "$TEST_DIR/test.img"@@$((part_start * 512)) "$TEST_DIR/kernel.elf" ::kernel.elf

# Run QEMU with timeout and output capture.
# Feed '\n' so the boot menu auto-selects the first device immediately
# instead of waiting out its 20M-iteration polling loop under TCG.
echo "==> Starting QEMU (timeout 30s)"
{ sleep 2; printf '\n'; sleep 5; } | timeout 30 qemu-system-riscv64 -M virt -m 256M \
    -bios "$TOP_DIR/bootloader.bin" \
    -drive file="$TEST_DIR/test.img",format=raw,if=none,id=drive0 \
    -device virtio-blk-device,drive=drive0 \
    -nographic > "$TEST_DIR/qemu_output.log" 2>&1 || true

echo "==> Checking output"
if grep -q "Hello from test kernel!" "$TEST_DIR/qemu_output.log"; then
    echo "[PASS] MBR+FAT32"
    rm -f "$TEST_DIR/qemu_output.log" "$TEST_DIR/test.img" "$TEST_DIR/kernel.elf"
    exit 0
else
    echo "[FAIL] MBR+FAT32"
    echo "--- QEMU output ---"
    cat "$TEST_DIR/qemu_output.log"
    echo "-------------------"
    rm -f "$TEST_DIR/qemu_output.log" "$TEST_DIR/test.img" "$TEST_DIR/kernel.elf"
    exit 1
fi
