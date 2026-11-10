#pragma once
#include "types.h"

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr;

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr;

/* Word-sized copies: the bootloader moves whole ELF images (megabytes) on
 * a 64-bit hart, so an 8-byte stride cuts the dominant boot cost by ~8x.
 * Alignment is checked once; unaligned buffers fall back to bytes. */
static inline void elf_memcpy(uint8_t* dst, const uint8_t* src, uint64_t n) {
    uint64_t i = 0;
    if ((((uintptr_t)dst | (uintptr_t)src) & 7u) == 0) {
        for (; i + 8 <= n; i += 8)
            *(uint64_t*)(dst + i) = *(const uint64_t*)(src + i);
    }
    for (; i < n; i++)
        dst[i] = src[i];
}

static inline void elf_memset(uint8_t* dst, uint8_t c, uint64_t n) {
    uint64_t i = 0;
    uint64_t word = (uint64_t)c * 0x0101010101010101ull;
    if (((uintptr_t)dst & 7u) == 0) {
        for (; i + 8 <= n; i += 8)
            *(uint64_t*)(dst + i) = word;
    }
    for (; i < n; i++)
        dst[i] = c;
}

typedef struct {
    const elf64_ehdr* ehdr;
    bool valid_elf;
    uint64_t entry_point;
} ELF64;

static inline void elf64_init(ELF64* e, const void* d) {
    e->ehdr = (const elf64_ehdr*)d;
    const uint8_t* id = (const uint8_t*)d;
    e->valid_elf = false;
    e->entry_point = 0;
    if (id[0] != 0x7F || id[1] != 'E' || id[2] != 'L' || id[3] != 'F')
        return;
    if (e->ehdr->e_type != 2 || e->ehdr->e_machine != 0xF3)
        return;
    e->entry_point = e->ehdr->e_entry;
    e->valid_elf = true;
}

static inline bool elf64_valid(ELF64* e) { return e->valid_elf; }
static inline uint64_t elf64_entry(ELF64* e) { return e->entry_point; }

static inline bool elf64_check_safe(ELF64* e, uint64_t boot_start, uint64_t boot_end) {
    if (!e->valid_elf) return false;
    const elf64_phdr* phdr = (const elf64_phdr*)((const uint8_t*)e->ehdr + e->ehdr->e_phoff);
    for (int i = 0; i < e->ehdr->e_phnum; i++) {
        if (phdr[i].p_type != 1) continue;
        uint64_t seg_start = phdr[i].p_paddr;
        uint64_t seg_end = seg_start + phdr[i].p_memsz;
        if (seg_end < seg_start) return false;
        if (seg_start < boot_end && seg_end > boot_start) return false;
        if (seg_end > 0xFFFFFFFFFFFFFFFFULL) return false;
    }
    return true;
}

static inline void elf64_load_all(ELF64* e) {
    const elf64_phdr* phdr = (const elf64_phdr*)((const uint8_t*)e->ehdr + e->ehdr->e_phoff);
    for (int i = 0; i < e->ehdr->e_phnum; i++) {
        if (phdr[i].p_type != 1)
            continue;
        uint8_t* dst = (uint8_t*)(uintptr_t)phdr[i].p_paddr;
        const uint8_t* src = (const uint8_t*)e->ehdr + phdr[i].p_offset;
        elf_memcpy(dst, src, phdr[i].p_filesz);
        if (phdr[i].p_memsz > phdr[i].p_filesz)
            elf_memset(dst + phdr[i].p_filesz, 0, phdr[i].p_memsz - phdr[i].p_filesz);
    }
}
