<p align="center">
<pre class="not-prose" style="text-align:center;font-family:monospace;">
    ███████                                     
  ███▒▒▒▒▒███                                   
 ███     ▒▒███ ████████   █████ ████ █████ █████
▒███      ▒███▒▒███▒▒███ ▒▒███ ▒███ ▒▒███ ▒▒███ 
▒███      ▒███ ▒███ ▒███  ▒███ ▒███  ▒▒▒█████▒  
▒▒███     ███  ▒███ ▒███  ▒███ ▒███   ███▒▒▒███ 
 ▒▒▒███████▒   ████ █████ ▒▒███████  █████ █████
   ▒▒▒▒▒▒▒    ▒▒▒▒ ▒▒▒▒▒   ▒▒▒▒▒███ ▒▒▒▒▒ ▒▒▒▒▒ 
                           ███ ▒███             
                          ▒▒██████              
                           ▒▒▒▒▒▒               
 ███████████                     █████          
▒▒███▒▒▒▒▒███                   ▒▒███           
 ▒███    ▒███  ██████   ██████  ███████         
 ▒██████████  ███▒▒███ ███▒▒███▒▒▒███▒          
 ▒███▒▒▒▒▒███▒███ ▒███▒███ ▒███  ▒███           
 ▒███    ▒███▒███ ▒███▒███ ▒███  ▒███ ███       
 ███████████ ▒▒██████ ▒▒██████   ▒▒█████        
▒▒▒▒▒▒▒▒▒▒▒   ▒▒▒▒▒▒   ▒▒▒▒▒▒     ▒▒▒▒▒         
                                                
                                                
                                                                                                           
</pre>
</p>
<p align="center"><em>Minimalist RISC-V 64-bit bootloader written in freestanding C++</em></p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-RISC--V%2064--bit-green" alt="RISC-V 64">
  <img src="https://img.shields.io/badge/language-C%2B%2B20-blue" alt="C++20">
  <img src="https://img.shields.io/badge/size-%7E9.5KB-purple" alt="~9.5KB">
  <img src="https://img.shields.io/badge/license-GPL--3.0-red" alt="GPL-3.0">
</p>

----

OnyxBoot is a tiny bare-metal bootloader for RISC-V 64-bit (rv64gc). It
discovers hardware through FDT (device tree from the a1 register), reads
kernel.elf from VirtIO or SDHCI, parses ELF64, and jumps to the entry
point. The binary is ~9.5 KB and has zero runtime dependencies.

First-stage loader for [OnyxOS](https://github.com/DivByDiamond/OnyxOS).

----

## Quick start

```console
$ make                                          # default: CROSS=riscv64-elf
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
| `riscv64-elf-g++` (preferred) or `riscv64-unknown-elf-g++` | Cross-compiler |
| `riscv64-elf-objcopy` | Binary extraction |
| `make` | Build |
| `qemu-system-riscv64` | Test (optional) |

Override toolchain (if you have the `riscv64-unknown-elf-` variant):

```console
make CROSS=riscv64-unknown-elf
```

The Makefile default is `CROSS ?= riscv64-elf`. That matches the mainstream
bare-metal toolchains (xpack-riscv-none-elf-gcc, RISC-V
collaborations/riscv-gnu-toolchain). If you installed Debian's
`gcc-riscv64-unknown-elf` package instead, override as shown above.

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

## Tests

### MBR+FAT32
```console
$ make test
# or: bash test/run_qemu.sh
```

### All tests (GPT+FAT32, MBR+ext4, fallback)
```console
$ make test-all
# or: bash test/test_all.sh
```

Runs four QEMU scenarios: MBR+FAT32, GPT+FAT32, MBR+ext4, and FAT32→ext4
fallback. Each creates a disk image, loads kernel.elf, and checks for
"Hello from test kernel!" in the serial output.

Expected output (MBR+FAT32):
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

CI at `.github/workflows/test.yml` builds and runs all four tests on every push.

----

## Technical details

- **Freestanding C++20**: no libc, no libstdc++, no RTTI, no exceptions.
- **No assembly files**: all privileged code is inline asm in boot_entry.cpp.
- **FDT driven**: DTB comes from a1, no hardcoded addresses. The parser reads
  /model, /memory, UART, VirtIO, SDHCI from the device tree.
- **VirtIO v1+v2**: detects the MMIO version at probe time. Legacy uses
  GuestPageSize + QueuePFN. The VirtQueue is static and page-aligned
  (alignas 4096).
- **UART reg-shift**: reads reg-shift from FDT. Works with byte stride (QEMU)
  and with 32-bit stride (real hardware like Sophgo SG2000).
- **S-mode compatible**: uses a0 for hartid instead of csrr mhartid. Works both
  in M-mode (-bios) and in S-mode (after OpenSBI).
- **Boot menu**: timeout ~20M nops, then auto-select of the first device. A
  number key picks a device manually.
- **ELF integrity**: check_safe validates that no segment overlaps bootloader
  memory.
- **Binary**: ~9.5 KB text, 240 B BSS, 8 KB stack. Budget 32 KB.

----

## Ecosystem

| Repository | Role | Language |
|---|---|---|
| [OnyxOS](https://github.com/DivByDiamond/OnyxOS) | Ecosystem: build, disk image, docs | Bash / Python |
| [OnyxKernel](https://github.com/DivByDiamond/OnyxKernel) | RISC-V 64 kernel (Sv39, 3 rings) | Rust |
| [OnyxBoot](https://github.com/DivByDiamond/OnyxBoot) | Bootloader (M-mode, FDT) | C++20 |
| [OnyxShell](https://github.com/DivByDiamond/OnyxShell) | User shell `osh` | Rust |
| [OnyxCompiller](https://github.com/DivByDiamond/OnyxCompiller) | C99 compiler `onyxcc` + linker | C99 |
| [OnyxApps](https://github.com/DivByDiamond/OnyxApps) | Userland applications | C99 |
| [OnyxDesktop](https://github.com/DivByDiamond/OnyxDesktop) | Desktop environment `onyxdesk` | C99 |

----

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).

----

## Русская версия

OnyxBoot: крошечный bare-metal загрузчик для RISC-V 64-bit (rv64gc).
Он находит железо через FDT (device tree из регистра a1), читает
kernel.elf с VirtIO или SDHCI, парсит ELF64 и прыгает в точку входа.
Бинарник ~9.5 КБ, ноль зависимостей времени выполнения.

Загрузчик первой ступени для [OnyxOS](https://github.com/DivByDiamond/OnyxOS).

----

## Быстрый старт

```console
$ make                                          # по умолчанию CROSS=riscv64-elf
$ qemu-system-riscv64 -M virt -m 256M -bios bootloader.bin \
    -drive file=disk.img,format=raw,if=none,id=drive0 \
    -device virtio-blk-device,drive=drive0 \
    -nographic -serial mon:stdio
```

Или через тестовый скрипт:

```console
$ bash test/run_qemu.sh
```

----

## Зависимости сборки

| Инструмент | Назначение |
|-----------|-----------|
| `riscv64-elf-g++` (предпочтительно) или `riscv64-unknown-elf-g++` | Кросс-компилятор |
| `riscv64-elf-objcopy` | Извлечение бинарника |
| `make` | Сборка |
| `qemu-system-riscv64` | Тестирование (опционально) |

Сменить тулчейн (если у вас вариант `riscv64-unknown-elf-`):

```console
make CROSS=riscv64-unknown-elf
```

В Makefile по умолчанию `CROSS ?= riscv64-elf`: это соответствует
основным bare-metal тулчейнам (xpack-riscv-none-elf-gcc, RISC-V
collaborations/riscv-gnu-toolchain). Если у вас установлен Debian-пакет
`gcc-riscv64-unknown-elf`, переопределите как показано выше.

----

## Как это работает

1. QEMU или MaskROM грузит загрузчик по адресу 0x80000000 и передаёт
   указатель FDT в a1.
2. `_start` (boot_entry.cpp): обнуляет BSS, настраивает стек 8 КБ,
   вызывает `boot_main`.
3. `boot_main` парсит FDT:
   - Находит NS16550A UART, инициализирует последовательный порт.
   - Читает модель платы из свойства `/model`.
   - Ищет устройства `virtio,mmio` и SDHCI.
   - Проверяет каждый слот VirtIO (v1 legacy или v2 modern), оставляет
     рабочие.
4. Boot menu: показывает доступные устройства, авто-выбор после таймаута
   (~20M nops).
5. Читает kernel.elf с выбранного устройства (до 4096 секторов, ограничено
   реальным размером DRAM).
6. Проверяет ELF-заголовок и что ни один сегмент не накладывается на
   память загрузчика.
7. Копирует сегменты ELF по целевым адресам через `load_all()`.
8. Прыгает в точку входа с hartid=0 и адресом FDT.

----

## Структура проекта

| Файл | Назначение |
|------|-----------|
| `src/boot_entry.cpp` | Голая _start: BSS, стек, проверка hartid через a0 |
| `src/boot_main.cpp` | Основная загрузка: FDT, устройства, ELF, прыжок |
| `include/uart.hpp` | NS16550A UART с reg-shift из FDT |
| `include/virtio.hpp` | VirtIO v1 (legacy) + v2 (modern) MMIO block драйвер |
| `include/sdhci.hpp` | SDHCI драйвер для SD-карт |
| `include/bootmenu.hpp` | Boot menu с таймаутом и авто-выбором |
| `include/elf.hpp` | Парсер ELF64 + проверка check_safe |
| `include/fdt.hpp` | Парсер FDT (токенизатор DTB) |
| `include/fdt_dev.hpp` | Поиск устройств: UART, VirtIO, SDHCI, memory, model |
| `include/debug.hpp` | Отладочный вывод (включается флагом -DDEBUG) |
| `linker.ld` | Скрипт линковки: 0x80000000, PHDRS (RX/RW), стек 8 КБ |
| `Makefile` | Кросс-компиляция, CROSS переопределяется |

----

## Тестирование

### MBR+FAT32
```console
$ make test
# или: bash test/run_qemu.sh
```

### Все тесты (GPT+FAT32, MBR+ext4, fallback)
```console
$ make test-all
# или: bash test/test_all.sh
```

Запускаются четыре QEMU-сценария: MBR+FAT32, GPT+FAT32, MBR+ext4 и
fallback FAT32→ext4. Каждый создаёт образ диска, загружает kernel.elf
и проверяет наличие "Hello from test kernel!" в выводе UART.

Ожидаемый вывод (MBR+FAT32):
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

CI в `.github/workflows/test.yml` собирает и прогоняет все четыре теста
на каждый push.

----

## Технические детали

- **Freestanding C++20**: нет libc, libstdc++, RTTI, исключений.
- **Нет .S файлов**: весь привилегированный код это inline asm в
  boot_entry.cpp.
- **FDT**: DTB приходит из a1, никаких хардкодных адресов. Парсер читает
  /model, /memory, UART, VirtIO, SDHCI из дерева устройств.
- **VirtIO v1+v2**: определяет версию MMIO при probe. Legacy использует
  GuestPageSize + QueuePFN. VirtQueue статический и странично выровнен
  (alignas 4096).
- **UART reg-shift**: читает reg-shift из FDT. Работает с байтовым шагом
  (QEMU) и с 32-битным шагом (реальное железо, например Sophgo SG2000).
- **S-mode совместимость**: использует a0 для hartid вместо csrr mhartid.
  Работает и в M-mode (-bios), и в S-mode (после OpenSBI).
- **Boot menu**: таймаут ~20M nops, затем авто-выбор первого устройства.
  Ручной выбор: клавиша с номером.
- **ELF integrity**: check_safe проверяет, что сегменты не пересекаются
  с памятью загрузчика.
- **Бинарник**: ~9.5 КБ text, 240 Б BSS, 8 КБ стек. Бюджет 32 КБ.

----

## Экосистема

| Repository | Role | Language |
|---|---|---|
| [OnyxOS](https://github.com/DivByDiamond/OnyxOS) | Ecosystem: build, disk image, docs | Bash / Python |
| [OnyxKernel](https://github.com/DivByDiamond/OnyxKernel) | RISC-V 64 kernel (Sv39, 3 rings) | Rust |
| [OnyxBoot](https://github.com/DivByDiamond/OnyxBoot) | Bootloader (M-mode, FDT) | C++20 |
| [OnyxShell](https://github.com/DivByDiamond/OnyxShell) | User shell `osh` | Rust |
| [OnyxCompiller](https://github.com/DivByDiamond/OnyxCompiller) | C99 compiler `onyxcc` + linker | C99 |
| [OnyxApps](https://github.com/DivByDiamond/OnyxApps) | Userland applications | C99 |
| [OnyxDesktop](https://github.com/DivByDiamond/OnyxDesktop) | Desktop environment `onyxdesk` | C99 |

----

## Лицензия

GPL-3.0-or-later. См. [LICENSE](LICENSE).
