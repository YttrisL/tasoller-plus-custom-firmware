/* Individual tower LEDs. The LED packet of the official firmware gives each tower three colours, one per group of
 * eight LEDs. Supernova also accepts the "extended LED packet" below, which sets any of the 24 LEDs of a tower on
 * its own. The official packet keeps working as it always did: nothing here changes unless a host sends extended
 * packets, and the official tool never does.
 *
 * An extended packet is sent to the same endpoint as the LED packet and is always exactly 64 bytes:
 *
 *     'D' 'X' version tower first count [red green blue] ... (padded with zeros)
 *
 * version is LED_EXT_PROTOCOL. tower is 0 for the left tower (the one of the first three colours of the LED packet) and
 * 1 for the right tower. first is the first LED, counted from the bottom of the tower (0 to 23), and count the number of
 * colours that follow (1 to LED_EXT_MAX_COUNT). The official LED packet is sent in two transfers of 64 and 50 bytes, so
 * no part of it can be taken for an extended packet. While extended packets keep coming (the last one less than
 * LED_EXT_HOLD_MS ago) the LED packet no longer changes the towers, only the slider; when they stop the towers follow the LED
 * packet again. A host asks first whether the firmware knows them with CMD_LED:
 *
 *     CMD_LED [0] -> [0, protocol, number of LEDs of a tower]
 *
 * and CMD_LED [1, tower, 9 bytes] sets the three colors of an air tower (0 left, 1 right) without the LED packet: the setup tool uses
 * it in the IO4-Emulation mode, which has no endpoint for it.
 *
 * The USB interrupt only stores what it receives; the colours are converted by the main loop, like the rest. */

#include "ss321.h"

#define LED_EXT_PROTOCOL        1
#define LED_EXT_MAX_COUNT       19
#define LED_EXT_HOLD_MS         200

#define REPLY_SEND              0       /* what command.c's enum reply says */
#define REPLY_ERROR             1

/* Called with every transfer of the LED endpoint, in the USB interrupt. True when it was an extended packet. */
bool led_ext_packet(const uint8_t *data, uint32_t length)
{
    uint32_t tower, first, count, i, k;

    if (length != 64 || data[0] != 'D' || data[1] != 'X' || data[2] != LED_EXT_PROTOCOL) {
        return false;
    }
    tower = data[3];
    first = data[4];
    count = data[5];
    if (tower > 1 || count == 0 || count > LED_EXT_MAX_COUNT || first + count > TOWER_LEDS) {
        return true;
    }
    for (i = 0; i < count; i++) {
        for (k = 0; k < 3; k++) {
            led_ext_raw[(tower * TOWER_LEDS + first + i) * 3 + k] = data[6 + i * 3 + k];
        }
    }
    led_ext_until = tick_ms + LED_EXT_HOLD_MS;
    led_dirty[LED_STRIP_TOWER_A + tower] = 3;
    return true;
}

/* Is a host driving the tower LEDs one by one at the moment? */
bool led_ext_active(void)
{
    return led_ext_until != 0 && (int32_t)(led_ext_until - tick_ms) > 0;
}

/* Load the colours of tower `tower` (0 left, 1 right) from the extended packets, dimmed to the brightness settings like
 * led_load_packet does. The left tower is wired from its top, the right one from its bottom. */
void led_ext_load(int tower)
{
    volatile rgb_t *rgb = tower == 0 ? led_tower_a_rgb : led_tower_b_rgb;
    volatile hsv_t *hsv = tower == 0 ? led_tower_a_hsv : led_tower_b_hsv;
    int led;

    for (led = 0; led < TOWER_LEDS; led++) {
        volatile uint8_t *colour = &led_ext_raw[(tower * TOWER_LEDS + (tower == 0 ? TOWER_LEDS - 1 - led : led)) * 3];

        rgb[led].red = colour[0];
        rgb[led].green = colour[1];
        rgb[led].blue = colour[2];
    }
    rgb_to_hsv(rgb, TOWER_LEDS, hsv);
    for (led = 0; led < TOWER_LEDS; led++) {
        hsv[led].value = hsv[led].value * settings.tower_brightness;
    }
    hsv_to_rgb(hsv, TOWER_LEDS, rgb);
}

/* Set the three colors (red, green, blue each) of the air tower `tower` (0 left, 1 right) in the LED packet, as if the host had sent
 * them in it, and make the host the one in charge of the lights. */
void led_ext_towers(uint32_t tower, const uint8_t *colors)
{
    uint32_t i;

    led_packet.tag[0] = 'D';
    led_packet.tag[1] = 'L';
    for (i = 0; i < 9; i++) {
        led_packet.tower[tower * 3 + i / 3][i % 3] = colors[i];
    }
    led_dirty[LED_STRIP_SLIDER] = 2;
    led_mode = LED_HOST_PACKET;
}

/* CMD_LED. Returns the status command_process expects: 0 to send command_reply, 1 for an error. */
uint32_t led_ext_command(const uint8_t *request)
{
    if (request[3] == 1 && request[4] <= 1 && request[0] >= 2 + 2 + 9) {
        led_ext_towers(request[4], request + 5);
        command_reply.data[0] = 1;
        command_reply.length += 1;
        return REPLY_SEND;
    }
    if (request[3] != 0) {
        return REPLY_ERROR;
    }
    command_reply.data[0] = 0;
    command_reply.data[1] = LED_EXT_PROTOCOL;
    command_reply.data[2] = TOWER_LEDS;
    command_reply.length += 3;
    return REPLY_SEND;
}
