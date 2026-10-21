/* The version string the device reports to the updater (command 2). The field is 40 bytes wide and the
 * linker script puts it at flash 0x4400, where the package tools and the bootloader expect it.
 * build-tools/build.py passes FW_VERSION, for example SS321_CFW_SNV_20261021. */

#include "ss321.h"

#ifndef FW_VERSION
#error "FW_VERSION is defined by build-tools/build.py"
#endif

__attribute__((section(".version"), used))
const char gVersionString[40] = FW_VERSION;
