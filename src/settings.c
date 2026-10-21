/* Settings: kept in the last page of flash (SETTINGS_FLASH), loaded at start-up, written back
 * when the setup tool changed something. */

#include "ss321.h"

/* The settings page was written by a firmware with another layout (or never): the marker in the
 * first byte of the key map of mode 0, which that mode does not use, tells. */
#define SETTINGS_MARKER     0x22

/* Value of an erased flash word. */
#define ERASED              0xff
#define ERASED16            0xffff

/* Key codes of the keyboard modes (USB HID usage ids). */
#define KEY_BACKSPACE       0x2a
#define KEY_ESCAPE          0x29
#define KEY_LEFT_BRACKET    0x2f
#define KEY_RIGHT_BRACKET   0x30
#define KEY_SEMICOLON       0x33
#define KEY_PERIOD          0x37
#define KEY_QUOTE           0x34
#define KEY_SLASH           0x38

/* Write the settings to flash when a change is pending, or when `force` is 1. */
void settings_flush(int force)
{
    uint32_t page[0x200];
    uint32_t i;

    if (settings_dirty != 1 && force != 1) {
        return;
    }
    settings_dirty = 0;
    memset(page, ERASED, sizeof page);
    memcpy(page, (const void *)&settings.device_mode, SETTINGS_SIZE);

    SYS_UnlockReg();
    FMC_Open();
    FMC_ISPCTL |= 8;                        /* allow updating the flash */
    FMC_Erase(SETTINGS_FLASH);
    for (i = 0; i < 0x200; i = (i + 1) & 0xffff) {
        FMC_Write(SETTINGS_FLASH + i * 4, page[i]);
    }
    FMC_Close();
    SYS_REGLCTL = 0;                        /* lock the protected registers again */
}

/* The key code of every pad in `pad_keys`, then the keys of the test and service buttons and the six
 * air beams, which are the same in every mode. */
static void key_map_set_buttons(volatile uint8_t *map)
{
    map[KEY_MAP_TEST] = KEY_BACKSPACE;
    map[KEY_MAP_SERVICE] = KEY_ESCAPE;
    map[KEY_MAP_AIR + 5] = KEY_LEFT_BRACKET;
    map[KEY_MAP_AIR + 4] = KEY_RIGHT_BRACKET;
    map[KEY_MAP_AIR + 3] = KEY_SEMICOLON;
    map[KEY_MAP_AIR + 2] = KEY_PERIOD;
    map[KEY_MAP_AIR + 1] = KEY_QUOTE;
    map[KEY_MAP_AIR + 0] = KEY_SLASH;
}

/* Factory key maps. 4K mode: four keys, eight pads each (K, J, F, D). */
void key_map_reset_4k(void)
{
    static const uint8_t keys[4] = { 0x0e, 0x0d, 0x09, 0x07 };
    volatile uint8_t *map = &settings.key_map[MODE_KEYBOARD_4K * KEY_MAP_SIZE];
    uint32_t pad;

    for (pad = 0; pad < PAD_COUNT; pad++) {
        map[pad] = keys[pad / 8];
    }
    key_map_set_buttons(map);
}

/* 8K mode: eight keys, four pads each (; L K J F D S A). */
void key_map_reset_8k(void)
{
    static const uint8_t keys[8] = { 0x33, 0x0f, 0x0e, 0x0d, 0x09, 0x07, 0x16, 0x04 };
    volatile uint8_t *map = &settings.key_map[MODE_KEYBOARD_8K * KEY_MAP_SIZE];
    uint32_t pad;

    for (pad = 0; pad < PAD_COUNT; pad++) {
        map[pad] = keys[pad / 4];
    }
    key_map_set_buttons(map);
}

/* 16K mode: sixteen keys, two pads each. */
void key_map_reset_16k(void)
{
    static const uint8_t keys[16] = {
        0x36, 0x0e, 0x10, 0x0d, 0x11, 0x0b, 0x05, 0x0a, 0x19, 0x09, 0x06, 0x07, 0x1b, 0x16, 0x1d, 0x04
    };
    volatile uint8_t *map = &settings.key_map[MODE_KEYBOARD_16K * KEY_MAP_SIZE];
    uint32_t pad;

    for (pad = 0; pad < PAD_COUNT; pad++) {
        map[pad] = keys[pad / 2];
    }
    key_map_set_buttons(map);
}

/* 32K mode: one key per pad. */
void key_map_reset_32k(void)
{
    static const uint8_t keys[PAD_COUNT] = {
        0x36, 0x0c, 0x0e, 0x25, 0x10, 0x18, 0x0d, 0x24, 0x11, 0x1c, 0x0b, 0x23, 0x05, 0x17, 0x0a, 0x22,
        0x19, 0x15, 0x09, 0x21, 0x06, 0x08, 0x07, 0x20, 0x1b, 0x1a, 0x16, 0x1f, 0x1d, 0x14, 0x04, 0x1e
    };
    volatile uint8_t *map = &settings.key_map[MODE_KEYBOARD_32K * KEY_MAP_SIZE];
    uint32_t pad;

    for (pad = 0; pad < PAD_COUNT; pad++) {
        map[pad] = keys[pad];
    }
    key_map_set_buttons(map);
}

/* Factory thresholds. */
void thresholds_reset(void)
{
    uint32_t i;

    for (i = 0; i < 4; i++) {
        settings.global_threshold[i] = DEFAULT_THRESHOLD;
    }
    for (i = 0; i < PAD_COUNT * SENSORS_PER_PAD; i++) {
        settings.sensor_threshold[i] = DEFAULT_THRESHOLD;
    }
    for (i = 0; i < 0x21; i++) {
        settings.pair_table[i] = 0;
    }
}

/* Read the settings from flash, replace what is missing or out of range by defaults and derive
 * the LED brightness factors. */
void settings_load(void)
{
    uint32_t page[0x200];
    uint32_t i;

    SYS_UnlockReg();
    FMC_Open();
    FMC_ISPCTL |= 8;
    for (i = 0; i < 0x200; i = (i + 1) & 0xffff) {
        page[i] = FMC_Read(SETTINGS_FLASH + i * 4);
    }
    FMC_Close();
    SYS_REGLCTL = 0;
    memcpy((void *)&settings.device_mode, page, SETTINGS_SIZE);

    if (settings.device_mode > 5) {
        settings.device_mode = MODE_WINUSB;
    }
    if (settings.spare[0] == ERASED) {
        for (i = 0; i < 28; i++) {
            settings.spare[i] = 0x3c;
        }
    }
    if (settings.spare[29] == ERASED) {
        settings.spare[29] = 0x1e;
    }
    if (settings.spare[28] == ERASED) {
        settings.spare[28] = 0x0f;
    }
    for (i = 0; i < 4; i++) {
        if (settings.touch_param_bd[i] < 0x32 || settings.touch_param_bd[i] > 0x5000) {
            settings.touch_param_bd[i] = 300;
        }
    }
    if (settings.input_delay > 0x400) {
        settings.input_delay = 0;
    }
    if (settings.option_word == -1) {
        settings.option_word = 0;
    }
    for (i = 0; i < PAD_COUNT * SENSORS_PER_PAD; i++) {
        if (settings.sensor_threshold[i] == ERASED16) {
            settings.sensor_threshold[i] = DEFAULT_THRESHOLD;
        }
    }
    if ((uint8_t)settings.name[0] == ERASED) {
        memset((void *)settings.name, 0, sizeof settings.name);
    }
    for (i = 0; i < 4; i++) {
        if (settings.global_threshold[i] == ERASED16) {
            settings.global_threshold[i] = DEFAULT_THRESHOLD;
        }
    }
    if (settings.air_brightness > 100) {
        settings.air_brightness = 100;
    }
    if (settings.slider_brightness_percent > 100) {
        settings.slider_brightness_percent = 100;
    }
    if (settings.key_map[0] != SETTINGS_MARKER) {
        /* no valid settings: factory defaults */
        settings.input_delay = 0xb;
        settings.key_map[0] = SETTINGS_MARKER;
        settings.device_mode = MODE_WINUSB;
        key_map_reset_4k();
        key_map_reset_8k();
        key_map_reset_16k();
        key_map_reset_32k();
        thresholds_reset();
        settings_flush(1);
    }
    if (settings.touch_param_be == 0 || settings.touch_param_be > 0xff) {
        settings.touch_param_be = 0x23;
    }
    if (settings.tuning[2] > 0x19 || settings.tuning[2] < 7) {
        settings.tuning[0] = 0x23;
        settings.tuning[1] = 10;
        settings.tuning[2] = 7;
        settings.tuning[3] = 0x80;
    }
    input_queue_write = settings.input_delay;
    input_queue_read = 0;
    touch_calibration_compute();
    settings.tower_brightness = (((float)settings.air_brightness * 1.0f) / 100.0f) * 0.5f;
    settings.slider_brightness = ((float)settings.slider_brightness_percent * 1.0f) / 100.0f;
}
