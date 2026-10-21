/* Debug output. The official firmware links Keil's printf; this is a small replacement that
 * supports what the firmware uses (%x, and %X %d %u %c %s %% with a width and the 0 and -
 * flags), writing every character through debug_putchar (the serial port). */

#include <stdarg.h>
#include "ss321.h"

static void put(uint8_t c)
{
    debug_putchar(c);
}

static void put_number(uint32_t value, uint32_t base, bool upper, bool negative, int width, bool zero, bool left)
{
    char digits[12];
    int n = 0, i, pad;

    do {
        uint32_t d = value % base;

        digits[n++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
        value /= base;
    } while (value != 0);
    pad = width - n - (negative ? 1 : 0);
    if (!left && !zero) {
        for (i = 0; i < pad; i++) {
            put(' ');
        }
    }
    if (negative) {
        put('-');
    }
    if (!left && zero) {
        for (i = 0; i < pad; i++) {
            put('0');
        }
    }
    while (n > 0) {
        put((uint8_t)digits[--n]);
    }
    if (left) {
        for (i = 0; i < pad; i++) {
            put(' ');
        }
    }
}

void debug_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    while (*fmt) {
        bool zero = false, left = false;
        int width = 0;

        if (*fmt != '%') {
            put((uint8_t)*fmt++);
            continue;
        }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '0') {
                zero = true;
            } else if (*fmt == '-') {
                left = true;
            } else {
                break;
            }
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        switch (*fmt) {
        case 'x':
            put_number(va_arg(ap, uint32_t), 16, false, false, width, zero, left);
            break;
        case 'X':
            put_number(va_arg(ap, uint32_t), 16, true, false, width, zero, left);
            break;
        case 'u':
            put_number(va_arg(ap, uint32_t), 10, false, false, width, zero, left);
            break;
        case 'd': {
            int32_t v = va_arg(ap, int32_t);

            put_number(v < 0 ? (uint32_t)-v : (uint32_t)v, 10, false, v < 0, width, zero, left);
            break;
        }
        case 'c':
            put((uint8_t)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);

            while (*s) {
                put((uint8_t)*s++);
            }
            break;
        }
        case '%':
            put('%');
            break;
        default:
            break;
        }
        if (*fmt) {
            fmt++;
        }
    }
    va_end(ap);
}
