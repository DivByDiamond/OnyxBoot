<p align="center">
  <img src="https://img.shields.io/badge/platform-RISC--V%2064--bit-green" alt="RISC-V 64">
  <img src="https://img.shields.io/badge/language-C%2B%2B20-blue" alt="C++20">
  <img src="https://img.shields.io/badge/size-%7E9.5KB-purple" alt="~9.5KB">
  <img src="https://img.shields.io/badge/license-GPL--3.0-red" alt="GPL-3.0">
  <a href="README.ru.md"><img src="https://img.shields.io/badge/ru_readme-blue" alt="ru_readme"></a>
</p>

<p align="center">
<pre class="not-prose" style="text-align:center;font-family:monospace;">
    ███████                                      ███████████                     █████   
  ███▒▒▒▒▒███                                   ▒▒███▒▒▒▒▒███                   ▒▒███    
 ███     ▒▒███ ████████   █████ ████ █████ █████ ▒███    ▒███  ██████   ██████  ███████  
▒███      ▒███▒▒███▒▒███ ▒▒███ ▒███ ▒▒███ ▒▒███  ▒██████████  ███▒▒███ ███▒▒███▒▒▒███▒   
▒███      ▒███ ▒███ ▒███  ▒███ ▒███  ▒▒▒█████▒   ▒███▒▒▒▒▒███▒███ ▒███▒███ ▒███  ▒███    
▒▒███     ███  ▒███ ▒███  ▒███ ▒███   ███▒▒▒███  ▒███    ▒███▒███ ▒███▒███ ▒███  ▒███ ███
 ▒▒▒███████▒   ████ █████ ▒▒███████  █████ █████ ███████████ ▒▒██████ ▒▒██████   ▒▒█████ 
   ▒▒▒▒▒▒▒    ▒▒▒▒ ▒▒▒▒▒   ▒▒▒▒▒███ ▒▒▒▒▒ ▒▒▒▒▒ ▒▒▒▒▒▒▒▒▒▒▒   ▒▒▒▒▒▒   ▒▒▒▒▒▒     ▒▒▒▒▒  
                           ███ ▒███                                                      
                          ▒▒██████                                                       
                           ▒▒▒▒▒▒                                                                 
</pre>
</p>
<p align="center"><em>Minimalist RISC-V 64-bit bootloader written in freestanding C++</em></p>

----

OnyxBoot - tiny bare-metal bootloader for RISC-V 64-bit (rv64gc).
Discovers hardware via FDT (device tree from a1 register), reads kernel.elf
from VirtIO or SDHCI, parses ELF64, and jumps to entry point. ~9.5 KB binary,
zero runtime dependencies.

First-stage loader for [OnyxOS](https://github.com/anomalyco/OnyxOS).

----

## Quick start

```console
$ make CROSS=riscv64-elf
$ qemu-system-riscv64 -M virt -m 256M -bios bootloader.bin \
    -drive file=disk.img,format=raw,if=none,id=drive0 \
    -device virtio-blk-device,drive=drive0 \
    -nographic -serial mon:stdio
```

Or use the test script:

```console
$ bash test/run_qemu.sh
```

----

## Build dependencies

| Tool | Purpose |
|------|---------|
| `riscv64-elf-g++` or `riscv64-unknown-elf-g++` | Cross-compiler |
| `riscv64-elf-objcopy` | Binary extraction |
| `make` | Build |
| `qemu-system-riscv64` | Test (optional) |

Override toolchain:

```console
make CROSS=riscv64-unknown-elf
```

----

## How it works

1. QEMU or MaskROM loads bootloader at 0x80000000, passes FDT pointer in a1.
2. `_start` (boot_entry.cpp): clears BSS, sets up 8KB stack, calls `boot_main`.
3. `boot_main` parses FDT:
   - Finds NS16550A UART, inits serial output.
   - Reads board model from `/model` property.
   - Scans for `virtio,mmio` and SDHCI compatible devices.
   - Probes each VirtIO slot (v1 legacy or v2 modern), keeps only working ones.
4. Boot menu: shows available devices, auto-selects after timeout (~20M nops).
5. Reads kernel.elf from selected device (up to 4096 sectors, limited by actual DRAM size).
6. Validates ELF header, checks no segment overlaps bootloader memory.
7. Copies ELF segments to target addresses via `load_all()`.
8. Jumps to entry point with hartid=0 and FDT address.

----

## Project structure

| File | What |
|------|------|
| `src/boot_entry.cpp` | Naked _start: BSS, stack, hartid check via a0 |
| `src/boot_main.cpp` | Main boot: FDT, devices, ELF load, jump |
| `include/uart.hpp` | NS16550A UART with reg-shift from FDT |
| `include/virtio.hpp` | VirtIO v1 (legacy) + v2 (modern) MMIO block driver |
| `include/sdhci.hpp` | SDHCI SD card driver |
| `include/bootmenu.hpp` | Boot menu with timeout auto-select |
| `include/elf.hpp` | ELF64 parser + check_safe overlap detection |
| `include/fdt.hpp` | FDT core parser (DTB tokenizer) |
| `include/fdt_dev.hpp` | Device finders: UART, VirtIO, SDHCI, memory, model |
| `include/debug.hpp` | Conditional debug output (guarded by -DDEBUG) |
| `linker.ld` | Linker script: 0x80000000, PHDRS (RX/RW), 8KB stack |
| `Makefile` | Cross-compilation, overridable CROSS |

----

## Test

```console
$ bash test/run_qemu.sh
```

Creates 4MB disk image with kernel.elf at sector 0, runs QEMU with VirtIO block.
Expected output:

```
OnyxBoot v0.4 [riscv-virtio,qemu]

OnyxBoot boot menu
--------------------
  0: VirtIO @ 0x0000000010008000
--------------------
Select device (0-0, or enter for auto): loading kernel.elf [########]
jumping to kernel
Hello from test kernel!
```

Also includes CI at `.github/workflows/test.yml` for automated testing.

----

## Technical details

- **Freestanding C++20** - no libc, no libstdc++, no RTTI, no exceptions.
- **No assembly files** - all privileged code via inline asm in boot_entry.cpp.
- **FDT driven** - DTB from a1, no hardcoded addresses. Parses /model, /memory,
  UART, VirtIO, SDHCI from device tree.
- **VirtIO v1+v2** - detects MMIO version at probe time. Legacy uses GuestPageSize
  + QueuePFN. Uses static page-aligned VirtQueue (alignas 4096).
- **UART reg-shift** - reads reg-shift from FDT. Works with byte stride (QEMU)
  and 32-bit stride (real hardware like Sophgo SG2000).
- **S-mode compatible** - uses a0 for hartid instead of csrr mhartid. Works both
  in M-mode (-bios) and S-mode (after OpenSBI).
- **Boot menu** - timeout ~20M nops, auto-selects first device. Accepts number key.
- **ELF integrity** - check_safe validates no segment overlaps bootloader memory.
- **Binary** - ~9.5 KB text, 240 B BSS, 8 KB stack. Budget 32 KB.

----

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
