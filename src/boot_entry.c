#include "types.h"

extern void boot_main(uint64_t fdt_addr);

extern uint8_t _sbss[];
extern uint8_t _ebss[];
extern uint8_t _stack_end[];

__attribute__((naked, section(".text.boot")))
void _start(void) {
    asm volatile(
        "bnez a0, 2f\n"
        "1:\n"
        /* Hart 0: save FDT (a1), clear BSS, call boot_main */
        "mv s0, a1\n"
        "la t0, _sbss\n"
        "la t1, _ebss\n"
        "3:\n"
        "bgeu t0, t1, 4f\n"
        /*
         * _sbss and _ebss are both 16-byte aligned (see linker.ld),
         * so 8-byte stores (sd) always fit cleanly inside [t0, t1).
         * Using sd instead of sw halves the loop iterations and keeps
         * the last chunk safe regardless of section alignment.
         */
        "sd zero, 0(t0)\n"
        "addi t0, t0, 8\n"
        "j 3b\n"
        "4:\n"
        /*
         * Per RISC-V calling convention, sp must be 16-byte aligned
         * at function entry. _stack_end is already aligned in the
         * linker script, but mask explicitly so the invariant holds
         * even if the symbol ever drifts.
         */
        "la sp, _stack_end\n"
        "andi sp, sp, -16\n"
        "mv a0, s0\n"
        "call boot_main\n"
        "5:\n"
        "wfi\n"
        "j 5b\n"
        "2:\n"
        /*
         * Secondary harts: poll the SMP release mailbox at 0x80100000.
         * The previous "wfi; j 2b" loop parked secondaries FOREVER: WFI
         * sleeps until an interrupt becomes pending and nothing ever
         * targets a parked hart, so they never reached the kernel (the
         * kernel's G_RELEASE spin was unreachable from this boot chain).
         *
         * Protocol (mirrors kernel arch/smp.rs):
         *   - mailbox lives in the unused 2 MB gap between the bootloader
         *     image @0x80000000 and the kernel @0x80200000, so neither side
         *     can clobber it;
         *   - once the kernel is up it stores the S-mode secondary entry
         *     address there (release_secondary_harts);
         *   - a parked hart observes a non-zero word, fences so that all
         *     prior kernel initialization is visible, and jumps to it with
         *     tp = hartid (the kernel's per-hart code reads tp).
         */
        "mv tp, a0\n"
        "li t0, 0x80100000\n"
        "1:\n"
        "ld t1, 0(t0)\n"
        "beqz t1, 1b\n"
        /*
         * fence rw,rw orders the mailbox load ahead of everything this
         * hart does next, so all kernel initialization published before
         * the mailbox store is visible after the jump.
         */
        "fence rw, rw\n"
        "li t0, 0x80100000\n"
        "ld t1, 0(t0)\n"
        "jr t1\n"
    );
}
