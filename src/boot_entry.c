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
        /* Secondary harts: park in WFI permanently */
        "wfi\n"
        "j 2b\n"
    );
}
