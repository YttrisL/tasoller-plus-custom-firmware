/* Run-time start-up: the C equivalent of the scatter loader the vendor's toolchain adds.
 *
 * The initialised data (gDataInit) goes to RAM 0x20000000, the rest of the RAM up to the top of
 * the stack is cleared. The stack itself is in use while this runs, so the top of it is left alone. */

#include "ss321.h"

#define RAM_DATA     ((uint32_t *)0x20000000)
#define RAM_DATA_WORDS (0x1024 / 4)
#define RAM_ZERO     ((uint32_t *)0x20001024)
#define RAM_ZERO_END ((uint32_t *)(0x2001ED48 - 0x80))

extern const uint32_t gDataInit[RAM_DATA_WORDS];

void fw_init_data(void)
{
    volatile uint32_t *dst = RAM_DATA;
    volatile uint32_t *zero = RAM_ZERO;
    uint32_t i;

    for (i = 0; i < RAM_DATA_WORDS; i++) {
        dst[i] = gDataInit[i];
    }
    while (zero < RAM_ZERO_END) {
        *zero++ = 0;
    }
}
