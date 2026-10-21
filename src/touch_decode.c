/* Decoding of the touch frames. Each of the four touch controllers sends a 64 byte frame with
 * the readings of its eight pads: four groups of 16 bytes, each group holding eight big endian
 * 16 bit readings, four for each of two pads. The readings of a pad are added and turned into
 * a pad value, 0 (not touched) to 253, using the calibration of the sensors.
 *
 * What is done with the pad values depends on the device mode: the WinUSB personality puts them
 * in its input report, the keyboard personalities press keys. */

#include "ss321.h"

#define PADS_PER_CONTROLLER     8
#define FRAME_GROUP_SIZE        16
#define MAX_SUM                 0x515       /* a sum from here on is a full touch */
#define VALUE_MAX               0xfd

static uint32_t read_be16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | p[1];
}

/* The eight pad values of controller `chip` from its frame. */
static void decode_pads(const uint8_t *frame, int chip, uint32_t value[PADS_PER_CONTROLLER])
{
    uint32_t sum[PADS_PER_CONTROLLER] = { 0 };
    int used[PADS_PER_CONTROLLER] = { 0 };       /* readings that are not 0 */
    int group, pad, i;

    for (group = 0; group < 4; group++) {
        const uint8_t *g = frame + group * FRAME_GROUP_SIZE;
        /* the first pad of the group reads at offsets 0, 2, 8 and 10, the second at 4, 6, 12 and 14 */
        static const uint8_t offsets[2][4] = { { 0, 2, 8, 10 }, { 4, 6, 12, 14 } };

        for (i = 0; i < 2; i++) {
            int k;

            pad = group * 2 + i;
            for (k = 0; k < 4; k++) {
                uint32_t reading = read_be16(g + offsets[i][k]);

                if (reading != 0) {
                    used[pad]++;
                }
                sum[pad] += reading;
            }
        }
    }
    for (pad = 0; pad < PADS_PER_CONTROLLER; pad++) {
        int index;
        double scaled;

        if (used[pad] == 0) {
            value[pad] = 0;
            continue;
        }
        /* calibration entry: the number of sensors that read something selects among the four */
        index = (pad + chip * PADS_PER_CONTROLLER) * SENSORS_PER_PAD + used[pad] - 1;
        if ((int)sum[pad] < (int)settings.sensor_threshold[index]) {
            value[pad] = 0;
        } else if ((int)sum[pad] >= MAX_SUM) {
            value[pad] = VALUE_MAX;
        } else {
            scaled = (double)sum[pad] * (double)touch_gain[index] + (double)touch_bias[index];
            if (!(scaled < 19.5 || scaled > 20.0)) {
                value[pad] = 20;
            } else {
                value[pad] = (uint32_t)(int32_t)scaled;
            }
        }
    }
}

/* WinUSB personality: the pad values go to the input report, the LED of the slider key follows. */
uint32_t winusb_decode_touch(const uint8_t *frame, int chip)
{
    uint32_t value[PADS_PER_CONTROLLER];
    int i;

    decode_pads(frame, chip, value);
    for (i = 0; i < PADS_PER_CONTROLLER; i++) {
        int pad = i + chip * PADS_PER_CONTROLLER;

        if (i % 2 == 0) {
            if (value[i] != 0 || value[i + 1] != 0) {
                winusb_slider_key_led(1.0f, (15u - pad / 2) & 0xff);
            }
            if (value[i] == 0 && value[i + 1] == 0) {
                winusb_slider_key_led(0.0f, (15u - pad / 2) & 0xff);
            }
        }
        /* the two pads of a key are swapped in the report */
        winusb_pad[pad ^ 1](1, (uint8_t)value[i]);
        dbt_set_pad_value((31u - pad) & 0xff, (int16_t)value[i]);
    }
    return 0;
}

/* Keyboard personalities: a touched pad presses its key. */
uint32_t keyboard_decode_touch(const uint8_t *frame, int chip)
{
    uint32_t value[PADS_PER_CONTROLLER];
    int i;

    decode_pads(frame, chip, value);
    for (i = 0; i < PADS_PER_CONTROLLER; i++) {
        int pad = i + chip * PADS_PER_CONTROLLER;

        if (i % 2 == 0) {
            if (value[i] != 0 || value[i + 1] != 0) {
                keyboard_slider_key_led(1.0f, (15u - pad / 2) & 0xff);
            }
            if (value[i] == 0 && value[i + 1] == 0) {
                keyboard_slider_key_led(0.0f, (15u - pad / 2) & 0xff);
            }
        }
        if ((int32_t)value[i] < 1) {
            keyboard_pad_key(1, 0, pad & 0xff);
        } else {
            keyboard_pad_key(1, KEY_MAP(settings.device_mode, pad), pad & 0xff);
        }
        dbt_set_pad_value((31u - pad) & 0xff, (int16_t)value[i]);
    }
    return 0;
}
