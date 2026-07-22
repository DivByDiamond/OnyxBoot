<p align="center">
  <img src="https://img.shields.io/badge/platform-RISC--V%2064--bit-green" alt="RISC-V 64">
  <img src="https://img.shields.io/badge/language-C%2B%2B20-blue" alt="C++20">
  <img src="https://img.shields.io/badge/size-%7E9.5KB-purple" alt="~9.5KB">
  <img src="https://img.shields.io/badge/license-GPL--3.0-red" alt="GPL-3.0">
</p>

<h1 align="center">OnyxBoot</h1>
<p align="center"><em>Минималистичный RISC-V 64-bit загрузчик на голом C++</em></p>

----

OnyxBoot - крошечный bare-metal загрузчик для RISC-V 64-bit (rv64gc).
Находит железо через FDT (device tree из регистра a1), читает kernel.elf
с VirtIO или SDHCI, парсит ELF64 и прыгает в точку входа. ~9.5 КБ бинарник,
ноль зависимостей времени выполнения.

Загрузчик первой ступени для [OnyxOS](https://github.com/anomalyco/OnyxOS).

----

## Быстрый старт

```console
$ make CROSS=riscv64-elf
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

## Зависимости

| Инструмент | Назначение |
|-----------|-----------|
| `riscv64-elf-g++` или `riscv64-unknown-elf-g++` | Кросс-компилятор |
| `riscv64-elf-objcopy` | Извлечение бинарника |
| `make` | Сборка |
| `qemu-system-riscv64` | Тестирование (опционально) |

Сменить тулчейн:

```console
make CROSS=riscv64-unknown-elf
```

----

## Как это работает

1. QEMU или MaskROM грузит загрузчик по адресу 0x80000000, передаёт FDT в a1.
2. `_start` (boot_entry.cpp): обнуляет BSS, настраивает стек 8 КБ, вызывает `boot_main`.
3. `boot_main` парсит FDT:
   - Находит NS16550A UART, инициализирует последовательный порт.
   - Читает модель платы из свойства `/model`.
   - Ищет устройства `virtio,mmio` и SDHCI.
   - Проверяет каждый слот VirtIO (v1 legacy или v2 modern), оставляет рабочие.
4. Boot menu: показывает доступные устройства, авто-выбор через таймаут (~20M nops).
5. Читает kernel.elf с выбранного устройства (до 4096 секторов, ограничено реальным размером DRAM).
6. Проверяет ELF заголовок, проверяет что сегменты не накладываются на bootloader.
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

Запускает четыре QEMU-сценария: MBR+FAT32, GPT+FAT32, MBR+ext4 и fallback
FAT32→ext4. Каждый создаёт образ диска, загружает kernel.elf и проверяет
наличие "Hello from test kernel!" в выводе UART.

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

CI в `.github/workflows/test.yml` собирает и прогоняет все четыре теста на каждый push.

----

## Технические детали

- **Freestanding C++20** - нет libc, libstdc++, RTTI, исключений.
- **Нет .S файлов** - весь привилегированный код через inline asm в boot_entry.cpp.
- **FDT** - DTB из a1, никаких хардкодных адресов. Парсит /model, /memory,
  UART, VirtIO, SDHCI из дерева устройств.
- **VirtIO v1+v2** - определяет версию MMIO при probe. Legacy использует
  GuestPageSize + QueuePFN. VirtQueue странично выровнен (alignas 4096).
- **UART reg-shift** - читает reg-shift из FDT. Работает с байтовым шагом
  (QEMU) и 32-битным шагом (реальное железо, например Sophgo SG2000).
- **S-mode совместимость** - использует a0 для hartid вместо csrr mhartid.
  Работает и в M-mode (-bios) и в S-mode (после OpenSBI).
- **Boot menu** - таймаут ~20M nops, авто-выбор первого устройства. Можно
  выбрать номером.
- **ELF integrity** - check_safe проверяет что сегменты не пересекаются с
  памятью загрузчика.
- **Бинарник** - ~9.5 КБ text, 240 Б BSS, 8 КБ стек. Бюджет 32 КБ.

----

## Лицензия

GPL-3.0-or-later. См. [LICENSE](LICENSE).
