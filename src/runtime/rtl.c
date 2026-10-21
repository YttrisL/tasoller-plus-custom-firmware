/* Run-time library routines.
 *
 * The official firmware was built with Keil's C library; its routines are in the image under
 * names the linker dropped. These are their equivalents, each checked against the official
 * routine. The double precision arithmetic of the official library is not
 * needed: the compiler's own is used.
 */

#include "ss321.h"

/* Copy n bytes (the official routine leaves the end of the destination in r0; nothing uses it). */
void memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;

    while (n--) {
        *d++ = *s++;
    }
}

void *memset(void *dst, int value, size_t n)
{
    uint8_t *d = dst;

    while (n--) {
        *d++ = (uint8_t)value;
    }
    return dst;
}

size_t strlen(const char *s)
{
    const char *p = s;

    while (*p) {
        p++;
    }
    return (size_t)(p - s);
}

/* Difference of the first bytes that differ, 0 when the blocks are equal. */
int memcmp(const uint8_t *a, const uint8_t *b, size_t n)
{
    size_t i;
    int diff = 0;

    for (i = 0; i < n; i++) {
        diff = a[i] - b[i];
        if (diff) {
            break;
        }
    }
    return diff;
}
