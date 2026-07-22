# Roadmap

## v0.2 - OC2r (VirtIO)
- [x] UART драйвер (NS16550A, reg-shift из FDT)
- [x] VirtIO v1+v2 MMIO block (probe, read_sector)
- [x] ELF64 парсер (header, program headers, load, check_safe)
- [x] FDT парсер (memory, UART, VirtIO, SDHCI, model)
- [x] Точка входа _start (BSS, стек, прыжок, hartid из a0)
- [x] Проверено на QEMU riscv64 virt

## v0.3 - Milk-V Duo S (SDHCI)
- [x] SDHCI драйвер
- [x] DTB от предыдущей стадии (a1), единая сборка
- [x] Загрузка kernel.elf с блочного устройства
- [ ] Проверить на реальной Milk-V Duo S (нет железа)

## v0.4 - Стабильность
- [x] Boot menu с timeout auto-select
- [x] Поддержка нескольких устройств
- [x] PHDRS в linker.ld (без RWX warning)
- [x] S-mode совместимость (a0 вместо csrr mhartid)
- [x] UART reg-shift из FDT
- [x] Универсальный бинарник (без compile-time platform)
- [x] Убран 2MB static buffer из BSS
- [x] nsectors ограничен по DRAM size

## v0.5 - Инфраструктура
- [x] genimage + mkimage
- [x] 9P документация (комментарий в run_qemu.sh)
- [x] Debug UART (debug.hpp, guarded by -DDEBUG)
- [x] CI (GitHub Actions, .github/workflows/test.yml)
- [x] Документация (README)

## v0.6 - Файловые системы
- [x] FAT32 read-only (GPT + MBR, 8.3 имена)
- [x] ext4 read-only (extents + indirect fallback, ext2/3 совместимость)
- [x] GPT парсер + MBR fallback для FAT32
- [x] GPT GUID + MBR 0x83 для ext4
- [x] fallback цепочка FAT32→ext4 в boot_main
- [x] Рефакторинг include/ (8→6 файлов)
- [x] Все 6 code review фиксов (fdt_sdhci, goto, ALIGN, kernel_buf, UART, g_vq)
- [x] QEMU тест FAT32 загрузки (MBR + FAT32, make test)
- [x] QEMU тест ext4 загрузки

## v0.7 - Тестирование
- [x] QEMU тест GPT+FAT32 — `test/test_gpt_fat32.sh`
- [x] QEMU тест MBR+FAT32 — `test/run_qemu.sh` (тест v0.6)
- [x] QEMU тест MBR+ext4 — `test/test_ext4.sh`
- [x] CI workflow исправлен (Ubuntu apt + FAT32 образ, добавлены mtools/parted)
- [x] Проверка fallback FAT32→ext4 — `test/test_fallback.sh`

## v1.0 - Релиз
- [ ] Проверка на реальном Milk-V Duo S
- [ ] Стабильная интеграция с OnyxOS
- [ ] CI прогоняет все QEMU тесты
