/* The command channel: the setup tool sends 64 byte commands on USB endpoint EP2 and the device answers
 * on EP3. A command is
 *
 *     [length][command][sequence][arguments ...] ... [checksum]       (64 bytes)
 *
 * where length counts the bytes after it up to the end of the arguments and the checksum, at the end of the
 * block, is the sum of those bytes. The block is encrypted with AES-128 (ECB); the only command
 * that may be sent in plain text is CMD_SET_KEY, with which the host derives a new key from the
 * default one. The answer has the same shape, with the command and sequence of the request in it,
 * and is encrypted with the key that is current when it is sent. */

#include "ss321.h"

/* Command numbers */
#define CMD_SET_KEY                 0x01    /* [nonce, 16 bytes]: new key = AES(default key, nonce) */
#define CMD_VERSION                 0x02    /* -> the firmware version string */
#define CMD_UPDATE                  0x04    /* update the firmware: see update_command */
#define CMD_TOUCH_INFO              0x08    /* -> version of touch controller 0, device name */
#define CMD_GET_PARAMS              0x09    /* -> touch_param_bd[0], touch_param_be */
#define CMD_SET_PARAMS              0x0a
#define CMD_BRIDGE_ENTER            0x0b
#define CMD_BRIDGE_EXIT             0x0c
#define CMD_TOUCH_CONFIG_MODE       0x0d    /* [port or 0xff] */
#define CMD_TOUCH_SCAN_MODE         0x0e
#define CMD_REQUEST_RAW_A0          0x0f    /* ask all controllers for their 0xa0 frame ... */
#define CMD_READ_RAW_A0             0x10    /* ... and read what [port] sent (the answer comes later) */
#define CMD_REQUEST_RAW_A1          0x11
#define CMD_READ_RAW_A1             0x12
#define CMD_REQUEST_RAW_AA          0x13
#define CMD_READ_RAW_AA             0x14
#define CMD_SET_OPTION              0x19
#define CMD_GET_OPTION              0x20
#define CMD_CALIBRATE               0x24    /* [port] */
#define CMD_HARDWARE_INFO           0x25
#define CMD_TOUCH_NAME              0x26
#define CMD_DEVICE_MODE             0x27    /* [0] -> mode, [1, mode] sets it */
#define CMD_SAVE_AND_RESET          0x28
#define CMD_SENSOR_THRESHOLDS       0x29    /* [0|1 read|write, group 0..7] */
#define CMD_KEY_MAP                 0x2a    /* [0|1, mode 0..5] */
#define CMD_DEFAULT_THRESHOLDS      0x2b    /* -> the factory thresholds */
#define CMD_SAVE                    0x2c
#define CMD_GLOBAL_THRESHOLDS       0x2d
#define CMD_RESTORE_DEFAULTS        0x2e    /* [what 0..4] */
#define CMD_RESTART_USB             0x2f
#define CMD_PAIR_TABLE              0x30
#define CMD_INPUT_DELAY             0x32
#define CMD_TOUCH_CAP_STATUS        0x34
#define CMD_RESTART_TOUCH           0x35
#define CMD_BRIGHTNESS              0x36    /* [0|1, air, slider] */
#define CMD_TUNING                  0x37
#define CMD_DUMP                    0x40    /* Supernova only: read the chip, see dump.c */
#define CMD_SETTINGS                0x41    /* Supernova only: back up and restore the settings, see settings_transfer.c */
#define CMD_LED                     0x42    /* Supernova only: asks for the extended LED packets, see led_ext.c */
#define CMD_TOUCH_UPDATE_START      0xa0
#define CMD_TOUCH_UPDATE_DATA       0xa2
#define CMD_RESET                   0xa3

/* What a command handler tells command_process to do. */
enum reply {
    REPLY_SEND = 0,         /* send command_reply */
    REPLY_ERROR = 1,        /* send the error reply */
    REPLY_NONE = 2          /* nothing to send now (the handler sent it, or the answer comes later) */
};

#define ERROR_CODE              5
#define READ                    0
#define WRITE                   1
#define MODE_QUERY_REAL         0xa5    /* after the read of CMD_DEVICE_MODE: report MODE_IO4 as such */

/* The packet that says where the firmware update goes: slot B of the flash, where the bootloader looks
 * for it. */
#define SLOT_B                  0x1d000
#define SLOT_SIZE               0x19000
#define UPDATE_TRIGGER_ADDRESS  0x39000
#define UPDATE_TRIGGER          0x5aa56789

extern const char gVersionString[];

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void put_be16(volatile uint8_t *p, uint16_t value)
{
    p[0] = value >> 8;
    p[1] = (uint8_t)value;
}

/* Checksum and encrypt command_reply. */
static void seal_reply(void)
{
    command_reply.checksum = checksum8((const uint8_t *)&command_reply.code, command_reply.length);
    aes_encrypt((uint8_t *)&command_reply, 0x40);
}

/* The LED brightness factors from the percentages of the settings. */
static void apply_brightness(void)
{
    settings.tower_brightness = (((float)settings.air_brightness * 1.0f) / 100.0f) * 0.5f;
    settings.slider_brightness = ((float)settings.slider_brightness_percent * 1.0f) / 100.0f;
}

/* Send a frame to the controller of `port` (0..3), or to all with 0xff; false for another number. */
static bool send_frame(uint8_t port, const uint8_t *frame)
{
    if (port == 0xff) {
        touch_send_all(frame, 5);
    } else if (port == 0) {
        UART_Write(&UART5_DAT, frame, 5);
    } else if (port == 1) {
        UART_Write(&UART2_DAT, frame, 5);
    } else if (port == 2) {
        UART_Write(&UART3_DAT, frame, 5);
    } else if (port == 3) {
        UART_Write(&UART4_DAT, frame, 5);
    } else {
        return false;
    }
    return true;
}

/* A frame to every controller, in the order UART5, 2, 3, 4. */
static void send_frame_all(const uint8_t *frame)
{
    UART_Write(&UART5_DAT, frame, 5);
    UART_Write(&UART2_DAT, frame, 5);
    UART_Write(&UART3_DAT, frame, 5);
    UART_Write(&UART4_DAT, frame, 5);
}

static void clear_touch_rings(void)
{
    int port;

    for (port = 0; port < TOUCH_PORTS; port++) {
        touch_ring[port].read = 0;
        touch_ring[port].write = 0;
    }
}

/* ---- Handlers ------------------------------------------------------------------------------- */

/* CMD_UPDATE: the firmware update. The image goes to slot B first; the bootloader copies it over the
 * application when it finds UPDATE_TRIGGER at UPDATE_TRIGGER_ADDRESS.
 *   [1, size]            erase slot B, the image will be `size` bytes long
 *   [2, offset, data]    write a piece of the image
 *   [3, checksum]        when the image in slot B has that checksum, ask for the copy
 *   [4]                  reset */
static enum reply update_command(const uint8_t *request)
{
    if (request[3] == 1) {
        SYS_UnlockReg();
        FMC_Open();
        FMC_EraseRange(SLOT_B, SLOT_SIZE);
        update_size = *(const u32_unaligned *)(request + 4);
    } else if (request[3] == 2) {
        uint32_t offset = *(const u32_unaligned *)(request + 4);

        FMC_WriteRange(offset + SLOT_B, request[0] + offset + SLOT_B - 7, (uint32_t *)(request + 8));
    } else {
        if (request[3] == 3) {
            if (flash_checksum(SLOT_B, update_size) == request[4]) {
                settings_flush(1);
                SYS_UnlockReg();
                FMC_Open();
                FMC_Write(UPDATE_TRIGGER_ADDRESS, UPDATE_TRIGGER);
                FMC_Close();
                SYS_REGLCTL = 0;
                return REPLY_SEND;
            }
        } else if (request[3] == 4) {
            GPIO_PC5_PDIO = 0;
            NVIC_SystemReset();
        }
        return REPLY_ERROR;
    }
    return REPLY_SEND;
}

/* CMD_TOUCH_INFO: the version of controller 0 and the name of the device. */
static enum reply touch_info_command(void)
{
    uint32_t length = strlen(settings.name);

    if (length >= 0x1f) {
        length = 0x1e;
    }
    *(u32_unaligned *)&command_reply.data[0] = *(volatile u32_unaligned *)&touch_info[0][0];
    *(u16_unaligned *)&command_reply.data[4] = *(volatile u16_unaligned *)&touch_info[0][4];
    command_reply.data[6] = touch_info[0][6];
    memcpy((void *)&command_reply.data[7], (const void *)settings.name, length);
    command_reply.length += length;
    command_reply.length += 7;
    return REPLY_SEND;
}

/* CMD_TOUCH_CONFIG_MODE and CMD_TOUCH_SCAN_MODE: switch the controller of port request[3] (or all of
 * them) to a mode. No answer: the controller answers with its own frame. */
static enum reply touch_mode_command(const uint8_t *request, uint32_t command)
{
    uint8_t frame[5];

    frame[0] = 0xde;
    frame[1] = 2;
    frame[2] = 2;
    frame[3] = command == CMD_TOUCH_CONFIG_MODE ? 0 : 1;
    frame[4] = command == CMD_TOUCH_CONFIG_MODE ? 0xe2 : 0xe3;
    if (!send_frame(request[3], frame)) {
        return REPLY_ERROR;
    }
    if (command == CMD_TOUCH_SCAN_MODE && request[3] == 0xff) {
        delay_ms(300);
    }
    return REPLY_NONE;
}

/* CMD_REQUEST_RAW_*: ask all the controllers for a kind of raw data. */
static enum reply request_raw_command(uint32_t command)
{
    uint8_t frame[5];
    uint8_t what = command == CMD_REQUEST_RAW_A0 ? 0 : (command == CMD_REQUEST_RAW_A1 ? 1 : 2);

    frame[0] = 0xb6;
    frame[1] = 2;
    frame[2] = 0;
    frame[3] = what;
    frame[4] = 0xb8 + what;
    send_frame_all(frame);
    return REPLY_NONE;
}

/* CMD_READ_RAW_*: send what controller request[3] sent for the last request: its port and 32 of
 * the 64 bytes. */
static enum reply read_raw_command(const uint8_t *request, uint32_t command)
{
    uint8_t packet[0x24];
    uint8_t port = request[3];
    volatile uint8_t (*source)[0x40];

    source = command == CMD_READ_RAW_A0 ? touch_raw_a0 : (command == CMD_READ_RAW_A1 ? touch_raw_a1 : touch_raw_aa);
    memset(packet, 0, sizeof packet);
    packet[0] = port;
    memcpy(packet + 1, (const void *)&source[port][0x20], 0x20);
    command_send_reply(command, packet, 0x21);
    return REPLY_NONE;
}

/* CMD_CALIBRATE: reset one controller and send it its settings again, one step at a time. The
 * answer is sent on its own: 0 when all went well, else the number of the step that failed (nothing
 * is sent when the very last step fails). */
static enum reply calibrate_command(const uint8_t *request)
{
    uint8_t reset_all[5] = { 0xde, 2, 3, 0, 0xe3 };
    uint8_t config_mode[5] = { 0xde, 2, 2, 0, 0xe2 };
    uint8_t param_bd[5] = { 0xbd, 2, 0, 0, 0 };
    uint8_t status = 0;
    uint8_t high;
    uint8_t port = request[3];
    uint8_t bit = 1 << port;

    touch_rx_flush();
    touch_send_to(port, reset_all, 5);
    if (wait_for_touch_boot(bit) != 1) {
        status = 1;
        command_send_reply(CMD_CALIBRATE, &status, 1);
        return REPLY_NONE;
    }
    touch_rx_flush();
    touch_ack.mode_config_ack = 0;
    touch_send_to(port, config_mode, 5);
    if (wait_for_flag(400, &touch_ack.mode_config_ack, bit) != 1) {
        status = 2;
        command_send_reply(CMD_CALIBRATE, &status, 1);
        return REPLY_NONE;
    }
    touch_rx_flush();
    touch_ack.param_bd_ack = 0;
    high = (uint8_t)(settings.touch_param_bd[port] >> 8);
    param_bd[2] = high;
    param_bd[3] = (uint8_t)settings.touch_param_bd[port];
    param_bd[4] = param_bd[0] + param_bd[1] + high + param_bd[3];
    touch_send_to(port, param_bd, 5);
    if (wait_for_flag(400, &touch_ack.param_bd_ack, bit) != 1) {
        status = 3;
        command_send_reply(CMD_CALIBRATE, &status, 1);
        return REPLY_NONE;
    }
    touch_rx_flush();
    touch_ack.mode_config_ack = 0;
    touch_send_to(port, config_mode, 5);
    if (wait_for_flag(400, &touch_ack.mode_config_ack, bit) == 1) {
        delay_ms(0x32);
        command_reply.data[0] = 0;
        command_reply.length += 1;
        command_send_reply(CMD_CALIBRATE, &status, 1);
    }
    return REPLY_NONE;
}

/* The commands below CMD_HARDWARE_INFO. */
static enum reply basic_command(uint8_t *request, uint32_t command)
{
    switch (command) {
    case CMD_SET_KEY:
        aes_set_key(aes_default_key);
        memcpy((void *)aes_key, request + 3, 0x10);
        aes_encrypt((uint8_t *)aes_key, 0x10);
        aes_set_key((const uint8_t *)aes_key);
        return REPLY_SEND;
    case CMD_VERSION:
        memset((void *)command_reply.data, 0, 0x32);
        memcpy((void *)command_reply.data, gVersionString, strlen(gVersionString));
        command_reply.length += strlen(gVersionString);
        return REPLY_SEND;
    case CMD_UPDATE:
        return update_command(request);
    case CMD_TOUCH_INFO:
        return touch_info_command();
    case CMD_GET_PARAMS:
        put_be16(&command_reply.data[0], settings.touch_param_bd[0]);
        put_be16(&command_reply.data[2], settings.touch_param_be);
        command_reply.length += 4;
        return REPLY_SEND;
    case CMD_SET_PARAMS:
        settings.touch_param_bd[0] = be16(request + 3);
        settings.touch_param_be = be16(request + 5);
        settings_flush(1);
        return REPLY_SEND;
    case CMD_BRIDGE_ENTER:
        bridge_mode_enter(0xe1000);
        return REPLY_SEND;
    case CMD_BRIDGE_EXIT:
        bridge_mode_exit();
        return REPLY_SEND;
    case CMD_TOUCH_CONFIG_MODE:
    case CMD_TOUCH_SCAN_MODE:
        return touch_mode_command(request, command);
    case CMD_REQUEST_RAW_A0:
    case CMD_REQUEST_RAW_A1:
    case CMD_REQUEST_RAW_AA:
        return request_raw_command(command);
    case CMD_READ_RAW_A0:
    case CMD_READ_RAW_A1:
    case CMD_READ_RAW_AA:
        return read_raw_command(request, command);
    case CMD_SET_OPTION:
        settings.option_word = *(u16_unaligned *)(request + 3);
        settings_flush(1);
        return REPLY_SEND;
    case CMD_GET_OPTION:
        *(u16_unaligned *)&command_reply.data[0] = settings.option_word;
        command_reply.length += 2;
        return REPLY_SEND;
    case CMD_CALIBRATE:
        return calibrate_command(request);
    default:
        return REPLY_ERROR;
    }
}

/* CMD_SENSOR_THRESHOLDS: the 16 thresholds of the pads 4 * group .. 4 * group + 3, big endian. */
static enum reply sensor_thresholds_command(const uint8_t *request)
{
    uint32_t pad, sensor;
    int offset;

    if (request[3] == READ && request[4] < 8) {
        command_reply.data[0] = request[3];
        command_reply.data[1] = request[4];
        command_reply.length = 4;
        offset = 2;
        for (pad = request[4] << 2; pad < request[4] * 4U + 4; pad++) {
            for (sensor = 0; sensor < SENSORS_PER_PAD; sensor++) {
                put_be16(&command_reply.data[offset], settings.sensor_threshold[pad * 4 + sensor]);
                offset += 2;
            }
        }
        command_reply.length += 0x20;
        return REPLY_SEND;
    }
    if (request[3] != WRITE || request[4] > 7) {
        return REPLY_ERROR;
    }
    command_reply.data[0] = request[3];
    command_reply.data[1] = request[4];
    command_reply.length = 4;
    offset = 2;
    for (pad = request[4] << 2; pad < request[4] * 4U + 4; pad++) {
        for (sensor = 0; sensor < SENSORS_PER_PAD; sensor++) {
            settings.sensor_threshold[pad * 4 + sensor] = be16(request + offset + 3);
            offset += 2;
        }
    }
    if (request[4] == 7) {
        touch_calibration_compute();
    }
    return REPLY_SEND;
}

/* CMD_KEY_MAP: the key map of a mode (the map of mode 5 is what follows the maps in the settings). */
static enum reply key_map_command(const uint8_t *request)
{
    volatile uint8_t *maps = settings.key_map;

    if (request[3] == READ && request[4] < 6) {
        command_reply.data[0] = request[3];
        command_reply.data[1] = request[4];
        command_reply.length = 4;
        memcpy((void *)&command_reply.data[2], (const void *)&maps[request[4] * KEY_MAP_SIZE], KEY_MAP_SIZE);
        command_reply.length += KEY_MAP_SIZE;
        return REPLY_SEND;
    }
    if (request[3] != WRITE || request[4] > 5) {
        return REPLY_ERROR;
    }
    command_reply.data[0] = request[3];
    command_reply.data[1] = request[4];
    command_reply.length = 4;
    memcpy((void *)&maps[command_reply.data[1] * KEY_MAP_SIZE], request + 5, KEY_MAP_SIZE);
    return REPLY_SEND;
}

/* CMD_GLOBAL_THRESHOLDS: one threshold per touch controller. */
static void global_thresholds_command(const uint8_t *request)
{
    uint32_t i;

    if (request[3] == READ) {
        command_reply.data[0] = request[3];
        command_reply.length = 3;
        for (i = 0; i < 4; i = (i + 1) & 0xff) {
            put_be16(&command_reply.data[1 + i * 2], settings.global_threshold[i]);
        }
        command_reply.length += 8;
    } else if (request[3] == WRITE) {
        settings.global_threshold[0] = be16(request + 4);
        settings.global_threshold[1] = be16(request + 6);
        settings.global_threshold[2] = be16(request + 8);
        settings.global_threshold[3] = be16(request + 10);
    }
}

/* CMD_RESTORE_DEFAULTS: restore what request[3] says (0 the thresholds, 1 to 4 the key map of a
 * mode) and save. */
static enum reply restore_defaults_command(const uint8_t *request)
{
    if (request[3] == 0) {
        thresholds_reset();
    } else if (request[3] == 1) {
        key_map_reset_4k();
    } else if (request[3] == 2) {
        key_map_reset_8k();
    } else if (request[3] == 3) {
        key_map_reset_16k();
    } else if (request[3] == 4) {
        key_map_reset_32k();
    } else {
        return REPLY_ERROR;
    }
    settings_flush(1);
    return REPLY_SEND;
}

/* CMD_PAIR_TABLE: two tables of 20 words (set 0 or 1). */
static enum reply pair_table_command(const uint8_t *request)
{
    uint32_t i;
    uint32_t set = request[4];

    if (request[3] == READ && set < 2) {
        command_reply.data[0] = 0;
        command_reply.data[1] = request[4];
        command_reply.length = 4;
        for (i = 0; i < 0x14; i = (i + 1) & 0xff) {
            put_be16(&command_reply.data[2 + i * 2], settings.pair_table[i + set * 0x14]);
        }
        command_reply.length += 0x28;
        return REPLY_SEND;
    }
    if (request[3] != WRITE || set > 1) {
        return REPLY_ERROR;
    }
    command_reply.data[0] = 0;
    command_reply.data[1] = request[4];
    command_reply.length = 4;
    for (i = 0; i < 0x14; i = (i + 1) & 0xff) {
        settings.pair_table[i + set * 0x14] = be16(request + 5 + i * 2);
    }
    return REPLY_SEND;
}

/* CMD_INPUT_DELAY: how late the inputs are reported, in milliseconds (at most 1024). */
static enum reply input_delay_command(const uint8_t *request)
{
    uint32_t i;

    if (request[3] == READ) {
        command_reply.data[0] = 0;
        put_be16(&command_reply.data[1], settings.input_delay);
        command_reply.length = 5;
    } else if (request[3] == WRITE) {
        uint16_t delay = be16(request + 4);

        if (delay > 0x400) {
            return REPLY_ERROR;
        }
        settings.input_delay = delay;
        command_reply.length = 2;
        settings_flush(1);
        input_queue_write = settings.input_delay;
        input_queue_read = 0;
        for (i = 0; i < (uint32_t)settings.input_delay; i = (i + 1) & 0xffff) {
            memset((void *)&report_delay_queue[i], 0, sizeof(input_state_t));
        }
    }
    return REPLY_SEND;
}

/* CMD_BRIGHTNESS: the brightness of the towers and of the slider, in percent. */
static enum reply brightness_command(const uint8_t *request)
{
    if (request[3] == READ) {
        command_reply.data[0] = 0;
        command_reply.data[1] = settings.air_brightness;
        command_reply.data[2] = settings.slider_brightness_percent;
        command_reply.length = 5;
    } else {
        if (request[3] != WRITE) {
            return REPLY_ERROR;
        }
        settings.air_brightness = request[4];
        settings.slider_brightness_percent = request[5];
        command_reply.data[0] = request[3];
        command_reply.length = 3;
        apply_brightness();
        if (led_mode == LED_LOCAL) {
            settings.brightness_dirty = 1;
        }
    }
    return REPLY_SEND;
}

/* CMD_TUNING: 18 words that are sent to the touch controllers when they start. */
static enum reply tuning_command(const uint8_t *request)
{
    uint32_t i;

    command_reply.length = 2;
    if (request[3] == READ) {
        for (i = 0; i < 0x12; i = (i + 1) & 0xff) {
            put_be16(&command_reply.data[i * 2], settings.tuning[i]);
        }
        command_reply.length += 0x24;
    } else {
        if (request[3] != WRITE) {
            return REPLY_ERROR;
        }
        for (i = 0; i < 0x12; i = (i + 1) & 0xff) {
            settings.tuning[i] = be16(request + 4 + i * 2);
        }
        settings_flush(1);
    }
    return REPLY_SEND;
}

/* CMD_TOUCH_UPDATE_START: put the touch controllers in their bootloaders (reset them with their
 * reset line low) and forward what they say to the host (see touch_uart_task). */
static void touch_update_start_command(void)
{
    uint8_t reset_frame[5] = { 0xde, 0x02, 0x03, 0x00, 0xe3 };

    GPIO_PC5_PDIO = 0;
    clear_touch_rings();
    touch_update_replies = 0;
    touch_update_mode = 1;
    command_reply.length = 2;
    delay_ms(100);
    touch_rx_flush();
    send_frame_all(reset_frame);
    GPIO_PC5_PDIO = 1;
}

/* CMD_TOUCH_UPDATE_DATA: [total size, offset, size, data ...]: collect the pieces of a message for the
 * bootloaders, and send it to all of them when it is complete. */
static void touch_update_data_command(const uint8_t *request)
{
    command_reply.length = 2;
    memcpy((void *)&touch_update_buffer[request[4]], request + 6, request[5]);
    if ((uint32_t)request[5] + request[4] == request[3]) {
        clear_touch_rings();
        touch_update_replies = 0;
        UART_Write(&UART2_DAT, (const uint8_t *)touch_update_buffer, request[3]);
        UART_Write(&UART3_DAT, (const uint8_t *)touch_update_buffer, request[3]);
        UART_Write(&UART4_DAT, (const uint8_t *)touch_update_buffer, request[3]);
        UART_Write(&UART5_DAT, (const uint8_t *)touch_update_buffer, request[3]);
        touch_update_mode = 1;
    }
}

/* CMD_RESTART_TOUCH: start the touch start-up again. */
static void restart_touch_command(void)
{
    touch_update_mode = 0;
    command_reply.length = 2;
    touch_rx_flush();
    touch_start_tick = 0;
    touch_state = TOUCH_STATE_DETECT;
    slider_fill(0.0f, 0.0f, 0.0f);
    usb_already_started = 1;
    touch_start_attempts = 0;
}

/* The commands from CMD_HARDWARE_INFO on. Returns -1 when `command` is not one of them. */
static int extended_command(uint8_t *request, uint32_t command)
{
    switch (command) {
    case CMD_HARDWARE_INFO:
        command_reply.length = 2;
        memcpy((void *)command_reply.data, (const void *)hardware_info, strlen((const char *)hardware_info) + 1);
        command_reply.length += strlen((const char *)hardware_info) + 1;
        return REPLY_SEND;
    case CMD_TOUCH_NAME:
        command_reply.length = 2;
        memcpy((void *)command_reply.data, (const void *)touch_name, strlen((const char *)touch_name) + 1);
        command_reply.length += strlen((const char *)touch_name) + 1;
        return REPLY_SEND;
    case CMD_DEVICE_MODE:
        if (request[3] == READ) {
            command_reply.data[0] = 0;
            /* The official tool only knows modes 0 to 4 and sees the IO4-Emulation mode as WinUSB, which it is. The Supernova
             * setup tool asks for the real mode with 0xa5 after the read command. */
            command_reply.data[1] = request[4] == MODE_QUERY_REAL && settings_io4() ? MODE_IO4 : settings.device_mode;
            command_reply.length = 4;
        } else if (request[3] == WRITE) {
            if (request[4] == MODE_IO4) {
                settings.device_mode = MODE_WINUSB;
                settings.spare[SNV_MODE_BYTE] = SNV_MODE_IO4;
            } else {
                settings.device_mode = request[4];
                settings.spare[SNV_MODE_BYTE] = SNV_MODE_SEEN;
            }
            command_reply.length = 2;
            settings_flush(1);
        }
        return REPLY_SEND;
    case CMD_SAVE_AND_RESET:
        command_reply.length = 2;
        settings_flush(1);
        NVIC_SystemReset();
        return REPLY_SEND;
    case CMD_SENSOR_THRESHOLDS:
        return sensor_thresholds_command(request);
    case CMD_KEY_MAP:
        return key_map_command(request);
    case CMD_DEFAULT_THRESHOLDS:
        command_reply.data[0] = 0;
        command_reply.data[1] = DEFAULT_THRESHOLD;
        command_reply.data[2] = 0;
        command_reply.data[3] = DEFAULT_THRESHOLD;
        command_reply.data[4] = 0;
        command_reply.data[5] = DEFAULT_THRESHOLD;
        command_reply.data[6] = 0;
        command_reply.data[7] = DEFAULT_THRESHOLD;
        command_reply.length = 10;
        return REPLY_SEND;
    case CMD_SAVE:
        command_reply.length = 2;
        settings_flush(1);
        apply_brightness();
        return REPLY_SEND;
    case CMD_GLOBAL_THRESHOLDS:
        global_thresholds_command(request);
        return REPLY_SEND;
    case CMD_RESTORE_DEFAULTS:
        command_reply.length = 2;
        return restore_defaults_command(request);
    case CMD_RESTART_USB:
        usb_restart_countdown = 1500;
        USBD_SE0 |= 1;                      /* disconnect, usb_restart_task connects again */
        touch_state = TOUCH_STATE_FADE_OUT;
        command_reply.length = 2;
        personality_select();
        return REPLY_SEND;
    case CMD_PAIR_TABLE:
        return pair_table_command(request);
    case CMD_INPUT_DELAY:
        return input_delay_command(request);
    case CMD_TOUCH_CAP_STATUS:
        command_reply.data[0] = 0;
        command_reply.data[1] = touch_cap_status;
        command_reply.length = 4;
        return REPLY_SEND;
    case CMD_RESTART_TOUCH:
        restart_touch_command();
        return REPLY_SEND;
    case CMD_BRIGHTNESS:
        return brightness_command(request);
    case CMD_TUNING:
        return tuning_command(request);
    case CMD_DUMP:
        return dump_command(request);
    case CMD_SETTINGS:
        return settings_transfer_command(request);
    case CMD_LED:
        return led_ext_command(request);
    case CMD_TOUCH_UPDATE_START:
        touch_update_start_command();
        return REPLY_SEND;
    case CMD_TOUCH_UPDATE_DATA:
        touch_update_data_command(request);
        return REPLY_SEND;
    case CMD_RESET:
        command_reply.length = 2;
        NVIC_SystemReset();
        return REPLY_SEND;
    default:
        return -1;
    }
}

/* ---- The channel ------------------------------------------------------------------------------ */

/* Answer with an error: [3][5][sequence][1]. */
static uint32_t send_error_reply(const uint8_t *request)
{
    command_reply.length = 3;
    command_reply.code = ERROR_CODE;
    command_reply.sequence = request[2];
    command_reply.data[0] = 1;
    command_reply.checksum = checksum8((const uint8_t *)&command_reply.code, 3);
    aes_encrypt((uint8_t *)&command_reply, 0x40);
    return REPLY_ERROR;
}

/* Decrypt the command in `request` (64 bytes), carry it out and prepare command_reply. Returns
 * 0 when command_reply is to be sent, 1 for an error (command_reply is the error), 2 when there is nothing to
 * send now. */
uint32_t command_process(uint8_t *request)
{
    uint32_t command;
    int status;

    if (checksum8(request + 1, request[0]) != request[0x3f] || request[1] != CMD_SET_KEY) {
        aes_decrypt(request, 0x40);
    }
    if (checksum8(request + 1, request[0]) != request[0x3f]) {
        return send_error_reply(request);
    }
    command_reply.code = request[1];
    command_reply.sequence = request[2];
    command_reply.length = 2;
    command = request[1];

    if (command > 0x24) {
        status = extended_command(request, command);
    } else {
        status = basic_command(request, command);
    }
    if (status == REPLY_SEND) {
        seal_reply();
        return REPLY_SEND;
    }
    if (status == REPLY_NONE) {
        return REPLY_NONE;
    }
    return send_error_reply(request);
}

/* Main loop task: when a command arrived, carry it out and send the answer. */
void command_task(void)
{
    uint32_t buffer;

    if (command_pending != 1) {
        return;
    }
    buffer = USBD_BUFSEG3;
    command_pending = 0;
    if (command_process((uint8_t *)&command_request) != REPLY_NONE) {
        uint32_t i;

        for (i = 0; i < 0x40; i++) {
            USB_SRAM[i + buffer] = ((volatile uint8_t *)&command_reply)[i];
        }
        USBD_MXPLD3 = 0x40;
    }
}

/* Send an answer of our own, outside of a command: `code`, then `length` bytes of `data`. Skipped
 * when the previous answer has not been taken yet. */
void command_send_reply(int code, const uint8_t *data, int length)
{
    uint32_t buffer;
    int i;

    if (ep3_ready != 1) {
        return;
    }
    buffer = USBD_BUFSEG3;
    ep3_ready = 0;
    command_reply.length = 2;
    command_reply.code = code;
    memcpy((void *)command_reply.data, data, length);
    command_reply.length += length;
    command_reply.checksum = checksum8((const uint8_t *)&command_reply.code, command_reply.length);
    aes_encrypt((uint8_t *)&command_reply, 0x40);
    for (i = 0; i < 0x40; i++) {
        USB_SRAM[i + buffer] = ((volatile uint8_t *)&command_reply)[i];
    }
    USBD_MXPLD3 = 0x40;
}
