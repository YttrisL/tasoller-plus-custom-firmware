#pragma once
#include <stdint.h>

/* A double from the two words (low, high) the Keil style helpers pass in register pairs. */
static inline double dbl_from(uint32_t lo, uint32_t hi)
{
    union {
        double d;
        uint64_t u;
    } x;

    x.u = ((uint64_t)hi << 32) | lo;
    return x.d;
}

/* The two words of a double. */
static inline uint64_t dbl_bits(double d)
{
    union {
        double d;
        uint64_t u;
    } x;

    x.d = d;
    return x.u;
}

/* A float seen as its 32 bits and back (the official code moves floats through core registers). */
static inline uint32_t float_bits(float f)
{
    union {
        float f;
        uint32_t u;
    } x;

    x.f = f;
    return x.u;
}

static inline float bits_float(uint32_t u)
{
    union {
        float f;
        uint32_t u;
    } x;

    x.u = u;
    return x.f;
}

/* A 32 bit word at any alignment (packed configuration data). */
typedef uint32_t __attribute__((packed, aligned(1))) u32_unaligned;
typedef uint16_t __attribute__((packed, aligned(1))) u16_unaligned;
