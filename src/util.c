/* Small helpers. */

#include "ss321.h"

uint8_t checksum8(int data,int length)
{
  int i;
  uint8_t sum;
  
  sum = '\0';
  for (i = 0; i < length; i = i + 1) {
    sum = *(uint8_t *)(data + i) + sum;
  }
  return sum;
}

uint32_t hook_noop(void)
{
  return 0;
}
