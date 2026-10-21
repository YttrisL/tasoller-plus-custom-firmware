/* The USB device of the controller: its endpoints and the requests it answers beyond the standard
 * ones. The control endpoint and the interrupt are in usbd.c.
 *
 * Endpoints (the numbers are those of the USB controller, "EPn"; the address is what the host sees):
 *   EP0, EP1  control, IN and OUT
 *   EP2       OUT 0x06  commands from the host (command_process)
 *   EP3       IN  0x85  the answers
 *   EP4       IN  0x84  the report of the personality (the pads)
 *   EP5       OUT 0x03  data for the personality (the LED packets)
 *   EP6       IN  0x81  the stream of touch frames
 *   EP7       OUT 0x02  start and stop of the stream
 */

#include "ss321.h"

/* Endpoint configuration (USBD_CFGn) */
#define CFG_OUT                 0x20u
#define CFG_IN                  0x40u
#define CFG_CSTALL              0x200u

#define EP_BUFFER_SIZE          0x40

/* Where the endpoints keep their packets in the USB SRAM */
#define EP0_BUFFER              8       /* control endpoint, both directions */
#define EP2_BUFFER              0x10
#define EP3_BUFFER              0x50
#define EP4_BUFFER              0x90
#define EP5_BUFFER              0xd0
#define EP6_BUFFER              0x110
#define EP7_BUFFER              0x150

/* USB interrupt number */
#define USBD_IRQ                53

/* The WebUSB landing page URL descriptor (bLength, bDescriptorType 3, scheme, then the rest of the URL) */
static const struct {
    uint8_t length;
    uint8_t type;
    uint8_t scheme;
    char url[sizeof(WEBUSB_URL_REST) - 1];
} __attribute__((packed)) webusb_url_descriptor_data = {
    3 + sizeof(WEBUSB_URL_REST) - 1, 3, WEBUSB_URL_SCHEME, WEBUSB_URL_REST
};
#define webusb_url_descriptor   ((const uint8_t *)&webusb_url_descriptor_data)

/* Descriptors the vendor requests ask for */
#define VENDOR_REQUEST_MS_OS    0xfc    /* Microsoft OS 2.0 descriptor set, wIndex 7 */
#define VENDOR_REQUEST_WEBUSB   0xfd    /* WebUSB landing page, wIndex 1 or 2 */
#define MS_OS_DESCRIPTOR_SIZE   0xb2

/* Set up the endpoints. */
static void usb_endpoints_init(void)
{
    USBD_STBUFSEG = 0;
    USBD_CFG0 = CFG_IN | CFG_CSTALL | 0;
    USBD_BUFSEG0 = EP0_BUFFER;
    USBD_CFG1 = CFG_OUT | CFG_CSTALL | 0;
    USBD_BUFSEG1 = EP0_BUFFER;
    USBD_CFG2 = CFG_OUT | 6;
    USBD_BUFSEG2 = EP2_BUFFER;
    USBD_MXPLD2 = EP_BUFFER_SIZE;
    USBD_CFG3 = CFG_IN | 5;
    USBD_BUFSEG3 = EP3_BUFFER;
    USBD_CFG5 = CFG_OUT | 3;
    USBD_BUFSEG5 = EP5_BUFFER;
    USBD_MXPLD5 = EP_BUFFER_SIZE;
    USBD_CFG4 = CFG_IN | 4;
    USBD_BUFSEG4 = EP4_BUFFER;
    USBD_CFG6 = CFG_IN | 1;
    USBD_BUFSEG6 = EP6_BUFFER;
    USBD_CFG7 = CFG_OUT | 2;
    USBD_BUFSEG7 = EP7_BUFFER;
    USBD_MXPLD7 = EP_BUFFER_SIZE;
}

/* HID class requests on the control endpoint (the Nuvoton sample's HID_ClassRequest). */
void HID_ClassRequest(void)
{
    uint8_t setup[8];

    USBD_GetSetupPacket(setup);
    if (setup[0] & 0x80) {
        USBD_SetStall(0);                   /* GET_REPORT, GET_IDLE, GET_PROTOCOL: stall */
    } else if (setup[1] == 9) {             /* SET_REPORT */
        if (setup[3] == 3) {                /* feature report */
            USBD_CFG1 |= 0x80;
            USBD_MXPLD1 = 0;
        }
    } else if (setup[1] == 10) {            /* SET_IDLE */
        USBD_CFG0 |= 0x80;
        USBD_MXPLD0 = 0;
    } else {                                /* SET_PROTOCOL and anything else: stall */
        USBD_SetStall(0);
    }
}

/* The vendor requests that let Windows install the WinUSB driver and browsers find the landing page. */
void WinUSB_VendorRequest(void)
{
    uint16_t index = g_usbd_SetupPacket[4] + g_usbd_SetupPacket[5] * 0x100;
    uint32_t length = g_usbd_SetupPacket[7] * 0x100 + g_usbd_SetupPacket[6];

    if (g_usbd_SetupPacket[1] == VENDOR_REQUEST_MS_OS) {
        if (index == 7) {
            if (length > MS_OS_DESCRIPTOR_SIZE) {
                length = MS_OS_DESCRIPTOR_SIZE;
            }
            USBD_PrepareCtrlIn((const uint8_t *)usb_ms_os_descriptor, length);
            USBD_PrepareCtrlOut(0, 0);
        }
    } else if (g_usbd_SetupPacket[1] == VENDOR_REQUEST_WEBUSB && (index == 2 || index == 1)) {
        uint32_t size = webusb_url_descriptor[0];

        if (size < length) {
            length = size;
            if ((webusb_url_descriptor[0] & 7) == 0) {
                g_usbd_CtrlInZeroFlag = 1;
            }
        }
        USBD_PrepareCtrlIn(webusb_url_descriptor, length);
        USBD_PrepareCtrlOut(0, 0);
    }
}

/* Start the USB device. */
void usb_start(void)
{
    USBD_Open(&usbd_info, HID_ClassRequest, 0);
    USBD_SetVendorRequest(WinUSB_VendorRequest);
    usb_endpoints_init();
    EP4_Handler();
    NVIC_SetPriority(USBD_IRQ, 0);
    NVIC_EnableIRQ(USBD_IRQ);
    USBD_Start();
}

/* Build the string descriptor of `text` (ASCII) in `descriptor`: length, type 3, UTF-16 characters. */
void usb_string_descriptor_set(uint8_t *descriptor, const char *text)
{
    size_t length = strlen(text);
    uint32_t i;

    descriptor[0] = (uint8_t)(length << 1) + 2;
    descriptor[1] = 3;
    for (i = 0; i < strlen(text); i++) {
        descriptor[i * 2 + 2] = text[i];
        descriptor[i * 2 + 3] = 0;
    }
}

/* ---- Endpoint interrupts -------------------------------------------------------------------- */

/* EP2: a command arrived: copy it for command_task. */
void EP2_Handler(void)
{
    uint32_t buffer = USBD_BUFSEG2;
    uint32_t length = USBD_MXPLD2;
    uint32_t i;

    for (i = 0; i < length; i++) {
        ((volatile uint8_t *)&command_request)[i] = USB_SRAM[i + buffer];
    }
    command_pending = 1;
    USBD_MXPLD2 = EP_BUFFER_SIZE;
}

/* EP3: the answer was taken by the host. */
void EP3_Handler(void)
{
    ep3_ready = 1;
}

/* EP4: the host took the report: queue the next one from the personality. */
void EP4_Handler(void)
{
    uint8_t *dst = (uint8_t *)(USBD_BUFSEG4 + (uint32_t)USB_SRAM);
    uint16_t length = 0;
    uint8_t *src = 0;
    uint8_t i;

    if (report_in_hook != 0) {
        src = report_in_hook(0, &length);
    }
    if (length != 0) {
        for (i = 0; i < length; i++) {
            dst[i] = src[i];
        }
        USBD_MXPLD4 = length;
    }
}

/* EP5: data from the host for the personality (the LED packets). */
void EP5_Handler(void)
{
    uint32_t buffer = USBD_BUFSEG5;
    uint32_t length = USBD_MXPLD5;
    uint32_t i;

    for (i = 0; i < (length & 0xffff); i++) {
        ep5_out_buffer[i] = USB_SRAM[i + buffer];
    }
    if (report_out_hook != 0) {
        report_out_hook((uint8_t *)ep5_out_buffer, length & 0xff);
    }
    USBD_MXPLD5 = EP_BUFFER_SIZE;
}

/* EP6: the host took the stream packet. */
void EP6_Handler(void)
{
    ep6_ready = 1;
}

/* EP7: a start or stop of the stream: copy it for stream_control_task. */
void EP7_Handler(void)
{
    uint32_t buffer = USBD_BUFSEG7;
    uint32_t length = USBD_MXPLD7;
    uint32_t i;

    stream_ctl_length = (uint8_t)length;
    for (i = 0; i < stream_ctl_length; i = (i + 1) & 0xffff) {
        stream_ctl_buffer[i] = USB_SRAM[i + buffer];
    }
    stream_ctl_pending = 1;
    USBD_MXPLD7 = EP_BUFFER_SIZE;
}
