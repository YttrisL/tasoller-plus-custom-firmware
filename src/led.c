/* LEDs: colours, effects and the packets the host sends.
 *
 * There are four strips: the slider (48 LEDs, three per key), two towers (24 LEDs each, four per air
 * beam) and an auxiliary strip of 28. Each has its colours as hue, saturation and value (led_*_hsv)
 * and, converted, as the bytes the LEDs want (led_*_rgb, green first). Changing the colours of a
 * strip is done by changing the hsv values and setting its led_dirty entry; led_hsv_to_rgb_task
 * converts, led_dma_task sends. */

#include "ss321.h"

/* How the LEDs follow the touch pads: a lit key is almost white, an unlit one is amber, the
 * LED between two keys is magenta. */
#define HUE_AMBER           40.0f
#define HUE_SEPARATOR       330.0f
#define HUE_AIR_A           180.0f
#define HUE_AIR_B           330.0f
#define WHITE_HUE           0.416f
#define WHITE_SATURATION    0.016f

/* Hue of LED n of the slider rainbow: 360 degrees over 47.2 LEDs. */
#define RAINBOW_STEP        7.63f

/* Fade speed: from 0 to full in 300 steps of 6 ms. */
#define FADE_STEPS          300.0f
#define FADE_PERIOD_MS      6

/* ---- Conversions --------------------------------------------------------------------------- */

/* Convert `count` colours from hue (0..360), saturation and value (0..1) to the bytes of the LEDs. */
void hsv_to_rgb(const volatile hsv_t *hsv, int count, volatile rgb_t *rgb)
{
    uint32_t i;
    float r = 0.0f, g = 0.0f, b = 0.0f;

    for (i = 0; (int)i < count; i = (i + 1) & 0xffff) {
        float hue = hsv[i].hue;
        float saturation = hsv[i].saturation;
        float value = hsv[i].value;
        int sector = ((int)hue / 60) % 6;
        float fraction = hue / 60.0f - (float)sector;
        float p = (1.0f - saturation) * value;
        float q = (1.0f - fraction * saturation) * value;
        float t = (1.0f - (1.0f - fraction) * saturation) * value;

        switch (sector) {
        case 0:
            r = value;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = value;
            b = p;
            break;
        case 2:
            r = p;
            g = value;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = value;
            break;
        case 4:
            r = t;
            g = p;
            b = value;
            break;
        case 5:
            r = value;
            g = p;
            b = q;
            break;
        }
        rgb[i].green = (uint8_t)(uint32_t)(g * 255.0f);
        rgb[i].red = (uint8_t)(uint32_t)(r * 255.0f);
        rgb[i].blue = (uint8_t)(uint32_t)(b * 255.0f);
    }
}

/* Convert `count` colours from bytes to hue (degrees), saturation and value (0..1). The arithmetic is in
 * double precision, as in the official firmware. */
int32_t rgb_to_hsv(const volatile rgb_t *rgb, int count, volatile hsv_t *hsv)
{
    int i;

    for (i = 0; i < count; i++) {
        uint32_t red = rgb[i].red;
        uint32_t green = rgb[i].green;
        uint32_t blue = rgb[i].blue;
        double lowest = (double)(int32_t)((int32_t)red < (int32_t)green ? red : green);
        double highest = (double)(int32_t)((int32_t)red > (int32_t)green ? red : green);
        double delta;
        float value, hue, saturation;

        if ((double)(int32_t)blue <= lowest) {
            lowest = (double)(int32_t)blue;
        }
        if (highest <= (double)(int32_t)blue) {
            highest = (double)(int32_t)blue;
        }
        value = (float)highest;
        delta = highest - lowest;
        if (!(dbl_from(0x80000000, 0x3ee4f8b5) <= delta) || highest <= 0.0) {
            /* grey: no hue */
            hsv[i].hue = 0.0f;
            hsv[i].saturation = 0.0f;
            hsv[i].value = value / 255.0f;
            continue;
        }
        saturation = (float)(delta / highest);
        if (!((double)(int32_t)red < highest)) {
            hue = (float)((double)(int32_t)(green - blue) / delta);
        } else if (!((double)(int32_t)green < highest)) {
            hue = (float)((double)(int32_t)(blue - red) / delta + 2.0);
        } else {
            hue = (float)((double)(int32_t)(red - green) / delta + 4.0);
        }
        hue = hue * 60.0f;
        if (hue < 0.0f) {
            hue = hue + 360.0f;
        }
        hsv[i].hue = hue;
        hsv[i].saturation = saturation;
        hsv[i].value = value / 255.0f;
    }
    return 0;
}

/* ---- Slider effects ------------------------------------------------------------------------ */

/* Every LED of the slider to one colour. */
void slider_fill(float hue, float saturation, float value)
{
    int i;

    for (i = 0; i < SLIDER_LEDS; i++) {
        led_slider_hsv[i].hue = hue;
        led_slider_hsv[i].saturation = saturation;
        led_slider_hsv[i].value = value;
    }
    led_dirty[LED_STRIP_SLIDER] = 1;
}

/* Black slider. */
void slider_off(void)
{
    int i;

    for (i = 0; i < SLIDER_LEDS; i++) {
        led_slider_hsv[i].value = 0.0f;
    }
    led_dirty[LED_STRIP_SLIDER] = 1;
}

/* Clear the colours of the slider without flagging a refresh. */
void slider_clear(void)
{
    int i;

    for (i = 0; i < SLIDER_LEDS; i++) {
        led_slider_hsv[i].hue = 0.0f;
        led_slider_hsv[i].saturation = 0.0f;
        led_slider_hsv[i].value = 0.0f;
    }
}

/* A rainbow over the 16 keys: two of every three LEDs follow a hue ramp, the third is green. The
 * towers are cleared and everything is flagged for refresh. */
void slider_rainbow(void)
{
    int i;

    for (i = 0; i < SLIDER_LEDS; i += 3) {
        float hue = (float)((double)(((i + 1) / 3) << 1) * 11.25);

        led_slider_hsv[i].hue = hue;
        led_slider_hsv[i].saturation = 1.0f;
        led_slider_hsv[i].value = settings.slider_brightness;
        led_slider_hsv[i + 1].hue = hue;
        led_slider_hsv[i + 1].saturation = 1.0f;
        led_slider_hsv[i + 1].value = settings.slider_brightness;
        led_slider_hsv[i + 2].hue = 120.0f;
        led_slider_hsv[i + 2].saturation = 1.0f;
        led_slider_hsv[i + 2].value = settings.slider_brightness;
    }
    led_slider_hsv[SLIDER_LEDS - 1].hue = (float)((i / 3) * 22);
    led_slider_hsv[SLIDER_LEDS - 1].saturation = 1.0f;
    led_slider_hsv[SLIDER_LEDS - 1].value = 0.0f;
    led_dirty[LED_STRIP_SLIDER] = 1;
    memset((void *)led_tower_a_rgb, 0, 0x48);
    memset((void *)led_tower_b_rgb, 0, 0x48);
    led_pending[LED_STRIP_TOWER_A] = 2;
    led_pending[LED_STRIP_TOWER_B] = 2;
}

/* One step of the fade-in of the slider, every 6 ms: each of the first 47 LEDs gets the base colour
 * of the mode and a little more brightness, up to the slider brightness. Returns 1 when the first LED is
 * at full brightness. */
bool slider_fade_in_step(void)
{
    uint32_t i;

    if (slider_fade_in_tick != tick_ms && tick_ms % FADE_PERIOD_MS == 0) {
        slider_fade_in_tick = tick_ms;
        for (i = 0; i < SLIDER_LEDS - 1; i = (i + 1) & 0xff) {
            if (settings.device_mode == MODE_WINUSB) {
                led_slider_hsv[i].hue = (float)i * RAINBOW_STEP;
            } else {
                led_slider_hsv[i].hue = HUE_AMBER;
                if (settings.device_mode == MODE_KEYBOARD_4K) {
                    if ((i + 1) % 12 == 0) {
                        led_slider_hsv[i].hue = HUE_SEPARATOR;
                    }
                } else if (settings.device_mode == MODE_KEYBOARD_8K) {
                    if ((i + 1) % 6 == 0) {
                        led_slider_hsv[i].hue = HUE_SEPARATOR;
                    }
                } else if (settings.device_mode == MODE_KEYBOARD_16K || settings.device_mode == MODE_KEYBOARD_32K) {
                    if ((i + 1) % 3 == 0) {
                        led_slider_hsv[i].hue = HUE_SEPARATOR;
                    }
                }
            }
            led_slider_hsv[i].saturation = 1.0f;
            led_slider_hsv[i].value = led_slider_hsv[i].value + (settings.slider_brightness / FADE_STEPS) * 6.0f;
            if (led_slider_hsv[i].value > settings.slider_brightness) {
                led_slider_hsv[i].value = settings.slider_brightness;
            }
            led_dirty[LED_STRIP_SLIDER] = 1;
        }
    }
    return led_slider_hsv[0].value >= settings.slider_brightness - 0.001f;
}

/* One step of the fade-out, every 6 ms. Returns 1 when the first LED is dark. */
bool slider_fade_out_step(void)
{
    int i;

    if (slider_fade_out_tick != tick_ms && tick_ms % FADE_PERIOD_MS == 0) {
        slider_fade_out_tick = tick_ms;
        for (i = 0; i < SLIDER_LEDS; i++) {
            led_slider_hsv[i].value = (float)led_slider_hsv[i].value - (settings.slider_brightness / FADE_STEPS) * 6.0f;
            if ((float)led_slider_hsv[i].value < 0.0f) {
                led_slider_hsv[i].value = 0.0f;
            }
        }
        led_dirty[LED_STRIP_SLIDER] = 1;
    }
    return led_slider_hsv[0].value <= 1e-08f;
}

/* Show a progress bar on the slider: `percent` of its 47 LEDs are lit. When the start-up is over this
 * is the end of the boot animation and the rainbow takes over. */
void slider_progress(uint32_t percent)
{
    int lit = (int)((((float)(int32_t)percent * 1.0f) / 100.0f) * 48.0f);
    int i;

    if (slider_progress_leds == lit) {
        return;
    }
    if (lit < SLIDER_LEDS - 1 || touch_state == TOUCH_STATE_RUNNING) {
        if (lit > SLIDER_LEDS - 2 && touch_state == TOUCH_STATE_RUNNING) {
            boot_progress_done = 1;
            slider_rainbow();
            return;
        }
    } else {
        lit = SLIDER_LEDS - 2;
    }
    slider_progress_leds = lit;
    if (lit == 0) {
        for (i = 0; i < SLIDER_LEDS; i++) {
            led_slider_hsv[i].value = 0.0f;
        }
    } else {
        for (i = 0; i < lit; i++) {
            led_slider_hsv[i].value = settings.slider_brightness;
        }
    }
    led_dirty[LED_STRIP_SLIDER] = 1;
}

/* ---- Lighting of the keys ------------------------------------------------------------------- */

/* Light the LEDs of slider key `key` (16 keys, three LEDs each) as pressed (level 1) or released, in
 * the WinUSB mode. Only when the device lights the keys itself (led_mode LED_LOCAL). A pressed key is
 * almost white and also whitens the separator next to a pressed neighbour; a released key shows the
 * rainbow again. */
uint32_t winusb_slider_key_led(float level, int key)
{
    uint32_t first = 0;

    if (led_mode == LED_LOCAL) {
        if ((float)key_pressed[key] != level) {
            key_pressed[key] = (uint32_t)level;
            led_dirty[LED_STRIP_SLIDER] = 1;
            first = (key * 3) & 0xff;
            if (level == 1.0f) {
                led_slider_hsv[first].hue = WHITE_HUE;
                led_slider_hsv[first].saturation = WHITE_SATURATION;
                led_slider_hsv[first].value = settings.slider_brightness;
                led_slider_hsv[first + 1].hue = WHITE_HUE;
                led_slider_hsv[first + 1].saturation = WHITE_SATURATION;
                led_slider_hsv[first + 1].value = settings.slider_brightness;
                if (key > 0 && key_pressed[key - 1] == 1) {
                    led_slider_hsv[first - 1].hue = WHITE_HUE;
                    led_slider_hsv[first - 1].saturation = WHITE_SATURATION;
                    led_slider_hsv[first - 1].value = settings.slider_brightness;
                }
                if (key < 15 && key_pressed[key + 1] == 1) {
                    led_slider_hsv[first + 2].hue = WHITE_HUE;
                    led_slider_hsv[first + 2].saturation = WHITE_SATURATION;
                    led_slider_hsv[first + 2].value = settings.slider_brightness;
                }
            } else {
                led_slider_hsv[first].hue = (float)first * RAINBOW_STEP;
                led_slider_hsv[first].saturation = 1.0f;
                led_slider_hsv[first].value = settings.slider_brightness;
                led_slider_hsv[first + 1].hue = (float)(int32_t)(first + 1) * RAINBOW_STEP;
                led_slider_hsv[first + 1].saturation = 1.0f;
                led_slider_hsv[first + 1].value = settings.slider_brightness;
                led_slider_hsv[first + 2].hue = (float)(int32_t)(first + 2) * RAINBOW_STEP;
                led_slider_hsv[first + 2].saturation = 1.0f;
                led_slider_hsv[first + 2].value = settings.slider_brightness;
                if (key < 1 || key > 14) {
                    if (key == 0) {
                        led_slider_hsv[2].hue = (float)(int32_t)2 * RAINBOW_STEP;
                        led_slider_hsv[2].saturation = 1.0f;
                        led_slider_hsv[2].value = settings.slider_brightness;
                    } else if (key == 15) {
                        led_slider_hsv[44].hue = (float)(int32_t)0x2c;
                        led_slider_hsv[44].hue = led_slider_hsv[44].hue * RAINBOW_STEP;
                        led_slider_hsv[44].saturation = 1.0f;
                        led_slider_hsv[44].value = settings.slider_brightness;
                    }
                } else {
                    led_slider_hsv[first - 1].hue = (float)(int32_t)(first - 1) * RAINBOW_STEP;
                    led_slider_hsv[first - 1].saturation = 1.0f;
                    led_slider_hsv[first - 1].value = settings.slider_brightness;
                }
            }
            led_slider_hsv[SLIDER_LEDS - 1].value = 0.0f;
        }
    }
    return first;
}

/* Colour of the LEDs of a group in a keyboard mode: amber when none of its keys is pressed,
 * almost white otherwise. */
static void keyboard_set_leds(uint32_t first, uint32_t end, bool pressed)
{
    uint32_t led;

    for (led = first; led = led & 0xff, led < end; led++) {
        led_slider_hsv[led].hue = pressed ? WHITE_HUE : HUE_AMBER;
        led_slider_hsv[led].saturation = pressed ? WHITE_SATURATION : 1.0f;
        led_slider_hsv[led].value = settings.slider_brightness;
    }
}

/* Light the slider for the keyboard modes: the keys are grouped (4 groups of 4 keys, 8 of 2, or one
 * each) and a group shows white while any of its keys is pressed. The LED before every group is a
 * magenta separator (the first group has none). */
void keyboard_slider_key_led(float level, int key)
{
    uint32_t group, key_in_group;

    if (led_mode != LED_LOCAL || (float)key_pressed[key] == level) {
        return;
    }
    key_pressed[key] = (uint32_t)level;
    led_dirty[LED_STRIP_SLIDER] = 1;
    if (settings.device_mode == MODE_KEYBOARD_4K) {
        for (group = 0; group < 4; group = (group + 1) & 0xff) {
            uint8_t sum = 0;

            for (key_in_group = group * 4; key_in_group = key_in_group & 0xff, key_in_group < group * 4 + 4; key_in_group++) {
                sum += key_pressed[key_in_group];
            }
            keyboard_set_leds(group * 12, (group * 4 + 4) * 3, sum != 0);
            if (group > 0) {
                led_slider_hsv[group * 12 - 1].hue = HUE_SEPARATOR;
                led_slider_hsv[group * 12 - 1].saturation = 1.0f;
                led_slider_hsv[group * 12 - 1].value = settings.slider_brightness;
            }
        }
    } else if (settings.device_mode == MODE_KEYBOARD_8K) {
        for (group = 0; group < 8; group = (group + 1) & 0xff) {
            uint8_t sum = 0;

            for (key_in_group = group * 2; key_in_group = key_in_group & 0xff, key_in_group < group * 2 + 2; key_in_group++) {
                sum += key_pressed[key_in_group];
            }
            keyboard_set_leds(group * 6, (group * 2 + 2) * 3, sum != 0);
            if (group > 0) {
                led_slider_hsv[group * 6 - 1].hue = HUE_SEPARATOR;
                led_slider_hsv[group * 6 - 1].saturation = 1.0f;
                led_slider_hsv[group * 6 - 1].value = settings.slider_brightness;
            }
        }
    } else if (settings.device_mode == MODE_KEYBOARD_16K || settings.device_mode == MODE_KEYBOARD_32K) {
        for (group = 0; group < 16; group = (group + 1) & 0xff) {
            keyboard_set_leds(group * 3, (group + 1) * 3, key_pressed[group] != 0);
            if (group > 0) {
                led_slider_hsv[group * 3 - 1].hue = HUE_SEPARATOR;
                led_slider_hsv[group * 3 - 1].saturation = 1.0f;
                led_slider_hsv[group * 3 - 1].value = settings.slider_brightness;
            }
        }
    }
    led_slider_hsv[SLIDER_LEDS - 1].value = 0.0f;
}

/* Show whether air beam `beam` (0..5) is interrupted: four LEDs of each tower, cyan on tower A and
 * magenta on tower B when it is, dark when it is not. */
void air_led_set(int beam, uint32_t blocked)
{
    int tower_a = (5 - beam) * 4;
    int tower_b = beam * 4;
    int i;

    if (led_mode != LED_LOCAL || air_blocked[beam] == blocked) {
        return;
    }
    air_blocked[beam] = (uint8_t)blocked;
    led_dirty[LED_STRIP_TOWER_B] = 1;
    led_dirty[LED_STRIP_TOWER_A] = 1;
    for (i = 0; i < 4; i++) {
        if (blocked == 1) {
            led_tower_a_hsv[tower_a + i].hue = HUE_AIR_A;
            led_tower_a_hsv[tower_a + i].saturation = 1.0f;
            led_tower_a_hsv[tower_a + i].value = settings.tower_brightness;
        }
    }
    for (i = 0; i < 4; i++) {
        if (blocked == 1) {
            led_tower_b_hsv[tower_b + i].hue = HUE_AIR_B;
            led_tower_b_hsv[tower_b + i].saturation = 1.0f;
            led_tower_b_hsv[tower_b + i].value = settings.tower_brightness;
        }
    }
    if (blocked != 1) {
        for (i = 0; i < 4; i++) {
            led_tower_b_hsv[tower_b + i].value = 0.0f;
        }
        for (i = 0; i < 4; i++) {
            led_tower_a_hsv[tower_a + i].value = 0.0f;
        }
    }
}

/* ---- Packets from the host ------------------------------------------------------------------ */

/* Load the LED packet (see led_packet_t) into the strips: the slider when `section` is 0, and the towers.
 * The colours are dimmed to the brightness settings on the way. */
void led_load_packet(int section)
{
    int led, group, k;

    if (led_packet.tag[0] != 'D' || led_packet.tag[1] != 'L') {
        return;
    }
    if (section == 0) {
        /* 16 keys: two LEDs of one colour, then the separator LED of the next colour; the host
         * counts from the right */
        for (k = 0; k < 16; k++) {
            const uint8_t *key_colour = led_packet.slider[k * 2];
            const uint8_t *separator_colour = led_packet.slider[k * 2 + 1];

            for (led = k * 3 + 1; led <= k * 3 + 2; led++) {
                led_slider_rgb[0x2f - led].red = key_colour[0];
                led_slider_rgb[0x2f - led].green = key_colour[1];
                led_slider_rgb[0x2f - led].blue = key_colour[2];
            }
            if (k * 3 + 3 < SLIDER_LEDS) {
                led_slider_rgb[0x2f - (k * 3 + 3)].red = separator_colour[0];
                led_slider_rgb[0x2f - (k * 3 + 3)].green = separator_colour[1];
                led_slider_rgb[0x2f - (k * 3 + 3)].blue = separator_colour[2];
            }
        }
        rgb_to_hsv(led_slider_rgb, SLIDER_LEDS, led_slider_hsv);
        for (led = 0; led < SLIDER_LEDS; led++) {
            led_slider_hsv[led].value = (float)led_slider_hsv[led].value * settings.slider_brightness;
        }
        hsv_to_rgb(led_slider_hsv, SLIDER_LEDS, led_slider_rgb);
    }
    /* towers: three groups of eight LEDs each */
    for (group = 2; group >= 0; group--) {
        for (led = group * 8; led < group * 8 + 8; led++) {
            led_tower_a_rgb[led].red = led_packet.tower[2 - group][0];
            led_tower_a_rgb[led].green = led_packet.tower[2 - group][1];
            led_tower_a_rgb[led].blue = led_packet.tower[2 - group][2];
        }
    }
    rgb_to_hsv(led_tower_a_rgb, TOWER_LEDS, led_tower_a_hsv);
    for (led = 0; led < TOWER_LEDS; led++) {
        led_tower_a_hsv[led].value = led_tower_a_hsv[led].value * settings.tower_brightness;
    }
    hsv_to_rgb(led_tower_a_hsv, TOWER_LEDS, led_tower_a_rgb);
    for (group = 2; group >= 0; group--) {
        for (led = group * 8; led < group * 8 + 8; led++) {
            led_tower_b_rgb[led].red = led_packet.tower[3 + group][0];
            led_tower_b_rgb[led].green = led_packet.tower[3 + group][1];
            led_tower_b_rgb[led].blue = led_packet.tower[3 + group][2];
        }
    }
    rgb_to_hsv(led_tower_b_rgb, TOWER_LEDS, led_tower_b_hsv);
    for (led = 0; led < TOWER_LEDS; led++) {
        led_tower_b_hsv[led].value = led_tower_b_hsv[led].value * settings.tower_brightness;
    }
    hsv_to_rgb(led_tower_b_hsv, TOWER_LEDS, led_tower_b_rgb);
}

/* ---- Tasks ---------------------------------------------------------------------------------- */

/* Main loop task: when a strip was changed convert its colours, in a different millisecond phase for each
 * strip so that the work is spread out. Each conversion orders two transmissions. */
void led_hsv_to_rgb_task(void)
{
    if ((tick_ms & 7) == 3 && led_dirty[LED_STRIP_SLIDER] != 0) {
        if (led_dirty[LED_STRIP_SLIDER] == 1) {
            hsv_to_rgb(led_slider_hsv, SLIDER_LEDS, led_slider_rgb);
        } else if (led_dirty[LED_STRIP_SLIDER] == 2) {
            led_load_packet(0);
            led_pending[LED_STRIP_TOWER_A] = 2;
            led_pending[LED_STRIP_TOWER_B] = 2;
        }
        led_dirty[LED_STRIP_SLIDER] = 0;
        led_pending[LED_STRIP_SLIDER] = 2;
    } else if ((tick_ms & 7) == 1 && led_dirty[LED_STRIP_TOWER_A] != 0) {
        if (led_dirty[LED_STRIP_TOWER_A] == 1) {
            hsv_to_rgb(led_tower_a_hsv, TOWER_LEDS, led_tower_a_rgb);
        } else if (led_dirty[LED_STRIP_TOWER_A] == 2) {
            led_load_packet(1);
        }
        led_dirty[LED_STRIP_TOWER_A] = 0;
        led_pending[LED_STRIP_TOWER_A] = 2;
    } else if ((tick_ms & 7) == 7 && led_dirty[LED_STRIP_TOWER_B] != 0) {
        if (led_dirty[LED_STRIP_TOWER_B] == 1) {
            hsv_to_rgb(led_tower_b_hsv, TOWER_LEDS, led_tower_b_rgb);
        } else if (led_dirty[LED_STRIP_TOWER_B] == 2) {
            led_load_packet(2);
        }
        led_dirty[LED_STRIP_TOWER_B] = 0;
        led_pending[LED_STRIP_TOWER_B] = 2;
    } else if ((tick_ms & 7) == 5 && led_dirty[LED_STRIP_AUX] != 0) {
        if (led_dirty[LED_STRIP_AUX] == 1) {
            hsv_to_rgb(led_aux_hsv, AUX_LEDS, led_aux_rgb);
        } else if (led_dirty[LED_STRIP_AUX] == 2) {
            led_load_packet(3);
        }
        led_dirty[LED_STRIP_AUX] = 0;
        led_pending[LED_STRIP_AUX] = 2;
    }
}

/* Main loop task: after the setup tool changed a brightness, apply it to what is lit. */
void led_brightness_task(void)
{
    int i;

    if (settings.brightness_dirty == 0) {
        return;
    }
    settings.brightness_dirty = 0;
    led_dirty[LED_STRIP_SLIDER] = 1;
    led_dirty[LED_STRIP_TOWER_A] = 1;
    led_dirty[LED_STRIP_TOWER_B] = 1;
    for (i = 0; i < SLIDER_LEDS; i++) {
        led_slider_hsv[i].value = settings.slider_brightness;
    }
    for (i = 0; i < TOWER_LEDS; i++) {
        if (led_tower_a_hsv[i].value > 1e-06f) {
            led_tower_a_hsv[i].value = settings.tower_brightness;
        }
    }
    for (i = 0; i < TOWER_LEDS; i++) {
        if (led_tower_b_hsv[i].value > 1e-06f) {
            led_tower_b_hsv[i].value = settings.tower_brightness;
        }
    }
    led_slider_hsv[SLIDER_LEDS - 1].value = 0.0f;
}
