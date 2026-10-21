#pragma once
/* Helpers for what the decompiler prints as pseudo functions. */
#define CONCAT44(a, b) ((((uint64_t)(uint32_t)(a)) << 32) | (uint32_t)(b))
#define CONCAT31(a, b) ((((uint32_t)(a)) << 8) | (uint8_t)(b))
#define CONCAT13(a, b) ((((uint32_t)(uint8_t)(a)) << 24) | ((uint32_t)(b) & 0xFFFFFFu))
#define CONCAT12(a, b) ((((uint32_t)(uint8_t)(a)) << 16) | (uint16_t)(b))
#define CONCAT21(a, b) ((((uint32_t)(uint16_t)(a)) << 8) | (uint8_t)(b))
#define CONCAT11(a, b) ((((uint16_t)(uint8_t)(a)) << 8) | (uint8_t)(b))
#define SUB41(x, n) ((uint8_t)((uint32_t)(x) >> (8 * (n))))
#define CARRY4(a, b) ((uint32_t)((uint32_t)(a) + (uint32_t)(b)) < (uint32_t)(a))
#define LZCOUNT(x) ((uint32_t)((x) ? __builtin_clz((uint32_t)(x)) : 32))
#define NAN(x) __builtin_isnan(x)
#define VectorSignedToFloat(x, rm) ((float)(int32_t)(x))
#define VectorUnsignedToFloat(x, rm) ((float)(uint32_t)(x))
#define VectorFloatToUnsigned(x, rm) ((uint32_t)(x))
#define VectorFloatToSigned(x, rm) ((int32_t)(x))
#define InstructionSynchronizationBarrier(x) __asm volatile("isb" ::: "memory")
#define DataSynchronizationBarrier(x) __asm volatile("dsb" ::: "memory")
#define halt_baddata() for (;;) {}
typedef uint32_t (*code_t)();
