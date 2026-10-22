#pragma once
/* Every text the firmware shows or sends out, in one place: USB strings, the bootloader tag it looks for
 * and the messages of the debug port. Plain ASCII.
 *
 * The version string (SS321_CFW_SNV_<build date>, read by the updater) is not here: build-tools/build.py
 * passes it to version.c as FW_VERSION. */

/* ---- USB ---- */

/* The name of the HID interfaces (string descriptor 4, iInterface of interfaces 0 and 1).
 * The official firmware says "Sky Star". Plain ASCII, at most 62 characters (the slot is 0x80 bytes). */
#ifndef USB_NAME
#define USB_NAME "Supernova"
#endif

#define USB_NAME_CHARS   (sizeof(USB_NAME) - 1)

/* Byte j of the string descriptor: bLength, bDescriptorType (3), then the text as UTF-16LE, then zeros. */
#define USB_NAME_BYTE(j) ((j) == 0 ? (uint32_t)(2 + 2 * USB_NAME_CHARS) : (j) == 1 ? 3u : \
    ((j) >= 2 && ((j) & 1) == 0 && (((j) - 2) >> 1) < USB_NAME_CHARS) ? (uint32_t)(uint8_t)USB_NAME[((j) - 2) >> 1] : 0u)

/* Little-endian word i of the slot. */
#define USB_NAME_WORD(i) (USB_NAME_BYTE(4 * (i)) | (USB_NAME_BYTE(4 * (i) + 1) << 8) | \
    (USB_NAME_BYTE(4 * (i) + 2) << 16) | (USB_NAME_BYTE(4 * (i) + 3) << 24))

/* The product and serial strings of the keyboard personality, by number of keys. */
#define USB_NAME_KEYBOARD_32K   "Supernova 32K mode"
#define USB_NAME_KEYBOARD_16K   "Supernova 16K mode"
#define USB_NAME_KEYBOARD_8K    "Supernova 8K mode"
#define USB_NAME_KEYBOARD_4K    "Supernova 4K mode"

/* The same for the WinUSB personality. */
#define USB_NAME_WINUSB         "Supernova WINUSB mode"

/* The IO4-Emulation mode: the WinUSB personality under another name. */
#define USB_NAME_IO4            "Supernova IO4 emulation mode"

/* The WebUSB landing page, https://github.com/YttrisL/tasoller-plus-custom-firmware: the scheme (0 is
 * http://, 1 is https://) and the rest of the URL. The descriptor is built from them in usb_device.c. */
#define WEBUSB_URL_SCHEME       1
#define WEBUSB_URL_REST         "github.com/YttrisL/tasoller-plus-custom-firmware"

/* ---- Bootloader ---- */

/* The version string of the bootloader, looked for in the first 16 KiB of flash. */
#define BOOTLOADER_TAG          "SS321_BOOT_020_20240511"

/* ---- Debug port (UART1) ---- */

#define MSG_UNEXPECTED_IRQ_A    "Un-expected interrupts.A\n"
#define MSG_UNEXPECTED_IRQ_B    "Un-expected interrupts B.\n"
#define MSG_UNEXPECTED_IRQ_C    "Un-expected interrupts.C\n"
#define MSG_UNEXPECTED_IRQ_D    "Un-expected interrupts.D\n"
#define MSG_UNEXPECTED_IRQ_F    "Un-expected interrupts.F\n"
