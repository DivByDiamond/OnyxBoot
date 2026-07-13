#!/bin/sh
# test_all.sh — run all OnyxBoot QEMU tests
#
# Runs every test in sequence and reports PASS/FAIL for each.
# Exits non-zero if any test failed.
set -e

TEST_DIR="$(dirname "$0")"
PASS=0
FAIL=0
FAILED_TESTS=""

run_test() {
    name="$1"
    script="$2"
    echo ""
    echo "========================================"
    echo "  TEST: $name"
    echo "========================================"
    if sh "$script"; then
        PASS=$((PASS + 1))
        echo "[result] $name: PASS"
    else
        FAIL=$((FAIL + 1))
        FAILED_TESTS="$FAILED_TESTS $name"
        echo "[result] $name: FAIL"
    fi
}

# MBR + FAT32 (existing test, refactored as a script).
run_test "MBR+FAT32" "$TEST_DIR/run_qemu.sh"

# GPT + FAT32.
run_test "GPT+FAT32" "$TEST_DIR/test_gpt_fat32.sh"

# MBR + ext4.
run_test "MBR+ext4" "$TEST_DIR/test_ext4.sh"

# FAT32→ext4 fallback.
run_test "fallback" "$TEST_DIR/test_fallback.sh"

echo ""
echo "========================================"
echo "  SUMMARY: $PASS passed, $FAIL failed"
if [ -n "$FAILED_TESTS" ]; then
    echo "  Failed:$FAILED_TESTS"
fi
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
