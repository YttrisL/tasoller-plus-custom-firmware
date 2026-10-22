#pragma once
/* What dump.c defines. */

void boot_snapshot_take(void);
uint32_t dump_command(const uint8_t *request);
