/* The settings backup: CMD_SETTINGS lets the setup tool save the settings block (SETTINGS_SIZE bytes, from
 * settings.device_mode on, the same bytes that are kept in flash at SETTINGS_FLASH) and put one back.
 *
 * The request is [length][CMD_SETTINGS][sequence][sub-command][arguments ...], numbers are little endian.
 *
 *   SETTINGS_INFO                         -> [0, protocol, size]                 size is 16 bits
 *   SETTINGS_READ   [offset, n]           -> [1, offset, n, data ...]            the settings in use, n is 1..48
 *   SETTINGS_STAGE  [offset, n, data ...] -> [2, 0]                              collect a block in settings_staging
 *   SETTINGS_COMMIT [crc32]               -> [3, status]
 *
 * offset is 16 bits. COMMIT checks that the collected block has the given CRC-32 and the marker of a valid
 * settings page, then makes it the settings in use and has it saved. Status 0 is done, 1 a bad CRC-32 (the
 * block is incomplete or damaged), 2 a block that is not a settings page. The setup tool restarts the device
 * afterwards. */

#include "ss321.h"

#define SETTINGS_PROTOCOL       1

#define SETTINGS_INFO           0
#define SETTINGS_READ           1
#define SETTINGS_STAGE          2
#define SETTINGS_COMMIT         3

#define SETTINGS_CHUNK          48

#define REPLY_SEND              0       /* what command.c's enum reply says */
#define REPLY_ERROR             1

static uint32_t crc32(const volatile uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xffffffffu;
    uint32_t i, bit;

    for (i = 0; i < length; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
        }
    }
    return ~crc;
}

/* CMD_SETTINGS. Returns the status command_process expects: 0 to send command_reply, 1 for an error. */
uint32_t settings_transfer_command(const uint8_t *request)
{
    uint32_t offset = request[4] | (request[5] << 8);
    uint32_t n = request[6];
    uint32_t i;

    switch (request[3]) {
    case SETTINGS_INFO:
        command_reply.data[0] = SETTINGS_INFO;
        command_reply.data[1] = SETTINGS_PROTOCOL;
        command_reply.data[2] = (uint8_t)SETTINGS_SIZE;
        command_reply.data[3] = (uint8_t)(SETTINGS_SIZE >> 8);
        command_reply.length += 4;
        return REPLY_SEND;
    case SETTINGS_READ:
        if (n == 0 || n > SETTINGS_CHUNK || offset + n > SETTINGS_SIZE) {
            return REPLY_ERROR;
        }
        command_reply.data[0] = SETTINGS_READ;
        command_reply.data[1] = (uint8_t)offset;
        command_reply.data[2] = (uint8_t)(offset >> 8);
        command_reply.data[3] = (uint8_t)n;
        for (i = 0; i < n; i++) {
            command_reply.data[4 + i] = ((const volatile uint8_t *)&settings.device_mode)[offset + i];
        }
        command_reply.length += 4 + n;
        return REPLY_SEND;
    case SETTINGS_STAGE:
        if (n == 0 || n > SETTINGS_CHUNK || offset + n > SETTINGS_SIZE || request[0] < 2 + 4 + n) {
            return REPLY_ERROR;
        }
        for (i = 0; i < n; i++) {
            settings_staging[offset + i] = request[7 + i];
        }
        command_reply.data[0] = SETTINGS_STAGE;
        command_reply.data[1] = 0;
        command_reply.length += 2;
        return REPLY_SEND;
    case SETTINGS_COMMIT:
        command_reply.data[0] = SETTINGS_COMMIT;
        if (crc32(settings_staging, SETTINGS_SIZE) != (request[4] | (request[5] << 8) | (request[6] << 16) | ((uint32_t)request[7] << 24))) {
            command_reply.data[1] = 1;
        } else {
            command_reply.data[1] = settings_replace(settings_staging) ? 0 : 2;
        }
        command_reply.length += 2;
        return REPLY_SEND;
    }
    return REPLY_ERROR;
}
