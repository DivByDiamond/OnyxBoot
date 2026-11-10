typedef unsigned long size_t;

/* Word-sized fast paths: the bootloader runs on a 64-bit hart and moves
 * whole ELF images, so 8-byte strides beat the old byte loops by ~8x.
 * Alignment is checked once per call; unaligned buffers fall back to bytes. */

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    size_t i = 0;
    unsigned long word = (unsigned char)c * 0x0101010101010101ul;
    if (((unsigned long)p & 7u) == 0) {
        for (; i + 8 <= n; i += 8)
            *(unsigned long *)(p + i) = word;
    }
    for (; i < n; i++)
        p[i] = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    size_t i = 0;
    if ((((unsigned long)d | (unsigned long)s) & 7u) == 0) {
        for (; i + 8 <= n; i += 8)
            *(unsigned long *)(d + i) = *(const unsigned long *)(s + i);
    }
    for (; i < n; i++)
        d[i] = s[i];
    return dest;
}

/* Overlap-safe and kept byte-wise on purpose: the backward/forward choice
 * depends on the direction, and this path is cold in the bootloader. */
void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dest;
}
