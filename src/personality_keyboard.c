/* Keyboard personality: a HID keyboard. A touched pad, a pressed button or an interrupted air beam
 * presses its key, as set in the key map of the mode (settings.key_map). */

#include "ss321.h"

/* Key `index` of the report (pads 0..31, then KEYBOARD_KEY_TEST ...): read it when `write` is 0, else
 * store `value`. */
uint8_t keyboard_pad_key(int write, uint8_t value, int index)
{
    if (write == 0) {
        return keyboard_report.key[index];
    }
    keyboard_report.key[index] = value;
    return 0;
}

/* The key of air beam `beam`: pressed when `blocked`. */
uint8_t keyboard_air_key(int write, int blocked, int beam)
{
    uint8_t value;

    if (write == 0) {
        return keyboard_report.key[KEYBOARD_KEY_AIR + beam];
    }
    if (blocked < 1) {
        value = 0;
    } else {
        value = KEY_MAP(settings.device_mode, KEY_MAP_AIR + beam);
    }
    keyboard_report.key[KEYBOARD_KEY_AIR + beam] = value;
    return 0;
}

/* The service (PC0) and test (PD3) buttons also set their bits in the stream packet. */
uint8_t keyboard_service_button(int write, int pressed)
{
    uint8_t value;

    if (write == 0) {
        return keyboard_report.key[KEYBOARD_KEY_SERVICE];
    }
    if (pressed < 1) {
        value = 0;
    } else {
        value = KEY_MAP(settings.device_mode, KEY_MAP_SERVICE);
    }
    keyboard_report.key[KEYBOARD_KEY_SERVICE] = value;
    winusb_service_button(write, pressed);
    return 0;
}

uint8_t keyboard_test_button(int write, int pressed)
{
    uint8_t value;

    if (write == 0) {
        return keyboard_report.key[KEYBOARD_KEY_TEST];
    }
    if (pressed < 1) {
        value = 0;
    } else {
        value = KEY_MAP(settings.device_mode, KEY_MAP_TEST);
    }
    keyboard_report.key[KEYBOARD_KEY_TEST] = value;
    winusb_test_button(write, pressed);
    return 0;
}

/* The report is always the same buffer. */
uint8_t *keyboard_report_in(uint32_t unused, uint16_t *length)
{
    *length = keyboard_report_length;
    return (uint8_t *)&keyboard_report;
}

/* Nothing to do with what the host writes. */
void keyboard_report_out(void)
{
}

/* Install the keyboard descriptors: the product id and the names depend on the number of keys. */
void keyboard_personality_init(void)
{
    uint8_t product_low;

    if (settings.device_mode == MODE_KEYBOARD_32K) {
        usb_string_descriptor_set(usb_string_device, USB_NAME_KEYBOARD_32K);
        usb_string_descriptor_set(usb_string_serial, USB_NAME_KEYBOARD_32K);
        product_low = USB_PRODUCT_KEYBOARD_32K & 0xff;
    } else if (settings.device_mode == MODE_KEYBOARD_16K) {
        usb_string_descriptor_set(usb_string_device, USB_NAME_KEYBOARD_16K);
        usb_string_descriptor_set(usb_string_serial, USB_NAME_KEYBOARD_16K);
        product_low = USB_PRODUCT_KEYBOARD_16K & 0xff;
    } else if (settings.device_mode == MODE_KEYBOARD_8K) {
        usb_string_descriptor_set(usb_string_device, USB_NAME_KEYBOARD_8K);
        usb_string_descriptor_set(usb_string_serial, USB_NAME_KEYBOARD_8K);
        product_low = USB_PRODUCT_KEYBOARD_8K & 0xff;
    } else {
        usb_string_descriptor_set(usb_string_device, USB_NAME_KEYBOARD_4K);
        usb_string_descriptor_set(usb_string_serial, USB_NAME_KEYBOARD_4K);
        product_low = USB_PRODUCT_KEYBOARD_4K & 0xff;
    }
    usb_keyboard_device_descriptor[10] = product_low;
    usb_keyboard_device_descriptor[11] = USB_PRODUCT_KEYBOARD_4K >> 8;
    usb_keyboard_device_descriptor[9] = USB_VENDOR_ID >> 8;
    usb_keyboard_device_descriptor[8] = USB_VENDOR_ID & 0xff;
    usbd_info.gu8DevDesc = usb_keyboard_device_descriptor;
    usb_hid_report_table[0] = usb_keyboard_report_descriptor;
    usb_hid_report_sizes[0] = 0x41;
    usb_hid_report_table[1] = usb_hid_report_vendor;
    usb_hid_report_sizes[1] = 0x1f;
    usb_hid_report_table[2] = usb_hid_report_vendor;
    usb_hid_report_sizes[2] = 0x1f;
    usbd_info.gu8ConfigDesc = usb_keyboard_config_descriptor;
    keyboard_report_length = KEYBOARD_REPORT_SIZE;
    keyboard_report.id = 2;
}
