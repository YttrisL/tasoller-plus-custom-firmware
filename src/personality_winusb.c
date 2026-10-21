/* WinUSB personality: a vendor device. The host reads the pads in a "DBT" report, gets them as a
 * stream on a bulk endpoint, and sets the LEDs with "DL" packets. */

#include "ss321.h"

/* ---- The report ---------------------------------------------------------------------------- */

/* Accessor of pad n of the WinUSB report: it reads the pad when `write` is 0, else stores `value`.
 * winusb_decode_touch reaches the pads through a table of these. */
#define WINUSB_PAD(n)                                                       \
uint8_t winusb_pad_##n(int write, uint8_t value)                            \
{                                                                           \
    if (write == 0) {                                                       \
        return winusb_report.pad[n];                                        \
    }                                                                       \
    winusb_report.pad[n] = value;                                           \
    return 0;                                                               \
}

WINUSB_PAD(0)  WINUSB_PAD(1)  WINUSB_PAD(2)  WINUSB_PAD(3)
WINUSB_PAD(4)  WINUSB_PAD(5)  WINUSB_PAD(6)  WINUSB_PAD(7)
WINUSB_PAD(8)  WINUSB_PAD(9)  WINUSB_PAD(10) WINUSB_PAD(11)
WINUSB_PAD(12) WINUSB_PAD(13) WINUSB_PAD(14) WINUSB_PAD(15)
WINUSB_PAD(16) WINUSB_PAD(17) WINUSB_PAD(18) WINUSB_PAD(19)
WINUSB_PAD(20) WINUSB_PAD(21) WINUSB_PAD(22) WINUSB_PAD(23)
WINUSB_PAD(24) WINUSB_PAD(25) WINUSB_PAD(26) WINUSB_PAD(27)
WINUSB_PAD(28) WINUSB_PAD(29) WINUSB_PAD(30) WINUSB_PAD(31)

/* Pad value `index` of the stream packet. */
void dbt_set_pad_value(int index, uint16_t value)
{
    dbt_packet.pad[index] = (uint8_t)value;
}

/* Air beam `beam` (0..5) is interrupted (`blocked` not 0) or free. It is bit `beam` of the buttons of
 * the stream packet and bit 5 - `beam` of those of the report. */
uint32_t winusb_air_beam(uint32_t beam, int blocked)
{
    uint8_t packet_bit = 1 << (beam & 0xff);
    uint8_t report_bit = 1 << (5 - beam & 0xff);

    if (blocked == 0) {
        dbt_packet.buttons &= ~packet_bit;
        winusb_report.buttons &= ~report_bit;
    } else {
        winusb_report.buttons |= report_bit;
        dbt_packet.buttons |= packet_bit;
    }
    return 1;
}

/* The service (PC0) and test (PD3) buttons: bit 6 and bit 7 of the buttons. */
uint32_t winusb_service_button(int unused, int pressed)
{
    if (pressed == 0) {
        dbt_packet.buttons &= ~BUTTON_SERVICE;
        winusb_report.buttons &= ~BUTTON_SERVICE;
    } else {
        dbt_packet.buttons |= BUTTON_SERVICE;
        winusb_report.buttons |= BUTTON_SERVICE;
    }
    return 1;
}

uint32_t winusb_test_button(int unused, int pressed)
{
    if (pressed == 0) {
        dbt_packet.buttons &= ~BUTTON_TEST;
        winusb_report.buttons &= ~BUTTON_TEST;
    } else {
        dbt_packet.buttons |= BUTTON_TEST;
        winusb_report.buttons |= BUTTON_TEST;
    }
    return 1;
}

/* The report the host reads: the live one, or with an input delay a copy whose pads come from the
 * delay queue. */
uint8_t *winusb_report_in(uint32_t unused, uint16_t *length)
{
    *length = sizeof(dbt_report_t);
    if (settings.input_delay == 0) {
        return (uint8_t *)&winusb_report;
    }
    memcpy((void *)winusb_report_delayed.pad, (const void *)report_delay_queue[input_queue_read].pad, PAD_COUNT);
    winusb_report_delayed.buttons = winusb_report.buttons;
    return (uint8_t *)&winusb_report_delayed;
}

/* ---- LED packets --------------------------------------------------------------------------- */

/* The host sends the LED packet (114 bytes) as two transfers on endpoint 5: the first starts with
 * "DL", the second is the rest. The packet is complete, and the slider has to be redrawn, with the second.
 * Any data from the host means that it is in charge of the lights. */
void winusb_report_out(uint8_t *data, uint32_t length)
{
    if (touch_state != TOUCH_STATE_RUNNING) {
        return;
    }
    if (data[0] == 'D' && data[1] == 'L') {
        memcpy((void *)&led_packet, data, length);
    } else {
        memcpy((void *)((volatile uint8_t *)&led_packet + 0x40), data, length);
        led_dirty[LED_STRIP_SLIDER] = 2;
    }
    led_mode = LED_HOST_PACKET;
}

/* ---- Descriptors --------------------------------------------------------------------------- */

#define LED_NAME_COUNT      28
#define LED_NAME_SIZE       16      /* bytes of the string descriptor "LED_01R" */
#define LED_NAME_DIGIT_HIGH 10      /* where the characters of the name are, in the template */
#define LED_NAME_DIGIT_LOW  12
#define LED_NAME_COLOUR     14
#define LED_NAME_FIRST_STRING   6   /* the first string index of the names */
#define LED_NAME_FILLER_FIRST   0x5a   /* the 32 indexes after the names show the last name */
#define LED_NAME_FILLER_COUNT   32

/* Install the WinUSB descriptors: vendor and product id, the names, the HID reports and the 28 x 3
 * string descriptors "LED_01G", "LED_01R", "LED_01B" ... that name the LED channels. */
void winusb_personality_init(void)
{
    int led, colour;
    static const char colours[3] = { 'G', 'R', 'B' };

    usb_winusb_device_descriptor[8] = USB_VENDOR_ID & 0xff;
    usb_winusb_device_descriptor[9] = USB_VENDOR_ID >> 8;
    usb_winusb_device_descriptor[10] = USB_PRODUCT_WINUSB & 0xff;
    usb_winusb_device_descriptor[11] = USB_PRODUCT_WINUSB >> 8;
    usb_string_descriptor_set(usb_string_serial, USB_NAME_WINUSB);
    usb_string_descriptor_set(usb_string_device, USB_NAME_WINUSB);
    usbd_info.gu8DevDesc = usb_winusb_device_descriptor;
    usb_hid_report_table[1] = usb_hid_report_vendor;
    usb_hid_report_sizes[1] = 0x1f;
    usb_hid_report_table[2] = usb_hid_report_vendor;
    usb_hid_report_sizes[2] = 0x1f;
    usbd_info.gu8ConfigDesc = usb_winusb_config_descriptor;

    for (led = 1; led <= LED_NAME_COUNT; led++) {
        for (colour = 0; colour < 3; colour++) {
            int first = (led - 1) * 3 + colour;

            usb_led_name_template[LED_NAME_DIGIT_HIGH] = led / 10 + '0';
            usb_led_name_template[LED_NAME_DIGIT_LOW] = led % 10 + '0';
            usb_led_name_template[LED_NAME_COLOUR] = colours[colour];
            memcpy((void *)&usb_led_names[first * LED_NAME_SIZE], (const void *)usb_led_name_template, LED_NAME_SIZE);
        }
        for (colour = 0; colour < 3; colour++) {
            int first = (led - 1) * 3 + colour;

            usb_string_table[(LED_NAME_FIRST_STRING + first) & 0xff] = &usb_led_names[first * LED_NAME_SIZE];
        }
    }
    for (led = 0; led < LED_NAME_FILLER_COUNT; led++) {
        usb_string_table[(led + LED_NAME_FILLER_FIRST) & 0xff] = usb_led_name_template;
    }
}
