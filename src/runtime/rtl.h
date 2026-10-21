#pragma once
/* What rtl.c defines. */

void memcpy(void *dst, const void *src, size_t n);
void * memset(void *dst, int value, size_t n);
size_t strlen(const char *s);
int memcmp(const uint8_t *a, const uint8_t *b, size_t n);
