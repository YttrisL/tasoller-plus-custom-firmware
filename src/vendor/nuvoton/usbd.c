/* USB device stack: the control endpoint and the interrupt, as in Nuvoton's usbd.c for the M480
 * (the names of the functions and variables are theirs). The endpoints of the device itself are in
 * usbd_ep.c, the descriptors it presents in personality_*.c. */

#include "ss321.h"

/* Request types (setup packet byte 0) */
#define REQ_DEVICE_TO_HOST      0x80
#define REQ_TYPE_MASK           0x60
#define REQ_TYPE_STANDARD       0x00
#define REQ_TYPE_CLASS          0x20
#define REQ_TYPE_VENDOR         0x40

/* Standard requests (setup packet byte 1) */
#define GET_STATUS              0
#define CLEAR_FEATURE           1
#define SET_FEATURE             3
#define SET_ADDRESS             5
#define GET_DESCRIPTOR          6
#define GET_CONFIGURATION       8
#define SET_CONFIGURATION       9
#define GET_INTERFACE           10
#define SET_INTERFACE           11

/* Descriptor types (setup packet byte 3 of GET_DESCRIPTOR) */
#define DESC_DEVICE             1
#define DESC_CONFIG             2
#define DESC_STRING             3
#define DESC_BOS                0x0f
#define DESC_HID                0x21
#define DESC_HID_REPORT         0x22

#define EP_COUNT                12

/* Registers */
#define USBD_INTSTS_BUS         0x00000001u     /* reset, suspend, resume, time-out: see USBD_ATTR */
#define USBD_INTSTS_USB         0x00000002u     /* a transfer: see the bits of USBD_INTSTS_EP */
#define USBD_INTSTS_VBDET       0x00000004u
#define USBD_INTSTS_NEVWK       0x00000008u
#define USBD_INTSTS_SETUP       0x80000000u
#define USBD_INTSTS_EP(n)       (0x00010000u << (n))

#define USBD_ATTR_USBRST        0x01u
#define USBD_ATTR_SUSPEND       0x02u
#define USBD_ATTR_RESUME        0x04u
#define USBD_ATTR_PHY_EN        0x10u
#define USBD_ATTR_DPPU_EN       0x80u           /* the D+ pull-up: connected to the host */
#define USBD_ATTR_RUN           0x7d0u          /* PHY, USB, pull-up, power-down, byte mode, LPM */
#define USBD_ATTR_OPEN          0x6d0u

#define USBD_CFG_STATE_STALL    0x02u           /* in USBD_CFGP: the endpoint answers STALL */
#define USBD_CFGP_CLRRDY        0x01u
#define USBD_CFG_DSQ_SYNC       0x80u           /* in USBD_CFG: next packet is DATA1 */
#define USBD_SE0_ENABLE         0x01u
#define USBD_INT_ALL            0x10fu          /* bus, USB, VBUS and wake-up events */

#define ENDPOINT_CFG(n)         ((&USBD_CFG0)[(n) * 4])
#define ENDPOINT_CFGP(n)        ((&USBD_CFGP0)[(n) * 4])

/* ---- Setup ---------------------------------------------------------------------------------- */

/* Open the USB device with the descriptors in `info` and the callbacks for the class requests and
 * for SET_INTERFACE, and connect to the bus. */
void USBD_Open(const volatile usbd_info_t *info, usbd_request_fn pfnClassReq, usbd_set_interface_fn pfnSetInterface)
{
    g_usbd_sInfo = (volatile usbd_info_t *)info;
    g_usbd_pfnClassRequest = pfnClassReq;
    g_usbd_pfnSetInterface = pfnSetInterface;
    g_usbd_CtrlMaxPktSize = info->gu8DevDesc[7];        /* bMaxPacketSize0 */
    USBD_ATTR = USBD_ATTR_OPEN;
    USBD_SE0 |= USBD_SE0_ENABLE;
}

/* Let the host see the device: release the bus and enable the interrupts. */
void USBD_Start(void)
{
    USBD_SE0 &= ~USBD_SE0_ENABLE;
    USBD_ATTR = USBD_ATTR_RUN;
    USBD_INTSTS = USBD_INT_ALL;
    USBD_INTEN |= USBD_INT_ALL;
}

/* Set the handler of the vendor requests. */
void USBD_SetVendorRequest(usbd_request_fn pfnVendorReq)
{
    g_usbd_pfnVendorRequest = pfnVendorReq;
}

/* Bus reset: forget the control transfer in progress and disable all the endpoints. */
void USBD_SwReset(void)
{
    uint32_t ep;

    g_usbd_CtrlInPointer = 0;
    g_usbd_CtrlInSize = 0;
    g_usbd_CtrlOutPointer = 0;
    g_usbd_CtrlOutSize = 0;
    g_usbd_CtrlOutSizeLimit = 0;
    g_u32EpStallLock = 0;
    *(volatile uint32_t *)&g_usbd_SetupPacket[0] = 0;
    *(volatile uint32_t *)&g_usbd_SetupPacket[4] = 0;
    for (ep = 0; ep < EP_COUNT; ep++) {
        ENDPOINT_CFG(ep) &= ~USBD_CFG_DSQ_SYNC;
    }
    USBD_FADDR = 0;
}

/* ---- Helpers -------------------------------------------------------------------------------- */

/* Copy n bytes. */
void USBD_MemCopy(uint8_t *dest, const uint8_t *src, int n)
{
    uint32_t i = 0;

    while (n-- != 0) {
        dest[i] = src[i];
        i++;
    }
}

/* Copy the 8 byte setup packet of the control transfer in progress. */
void USBD_GetSetupPacket(uint8_t *buf)
{
    USBD_MemCopy(buf, (uint8_t *)g_usbd_SetupPacket, 8);
}

/* Stall the endpoint with the number `epnum`. */
void USBD_SetStall(uint32_t epnum)
{
    uint32_t i;

    for (i = 0; i < EP_COUNT; i++) {
        if ((ENDPOINT_CFG(i) & 0xf) == epnum) {
            ENDPOINT_CFGP(i) |= USBD_CFG_STATE_STALL;
            return;
        }
    }
}

/* Stall the control endpoint (both directions). */
static void stall_control(void)
{
    USBD_CFGP0 |= USBD_CFG_STATE_STALL;
    USBD_CFGP1 |= USBD_CFG_STATE_STALL;
}

/* Answer a request that has no data stage: an empty packet. */
static void status_in(void)
{
    USBD_CFG0 |= USBD_CFG_DSQ_SYNC;
    USBD_MXPLD0 = 0;
}

/* Send `size` bytes of `buf` in answer to the request, in packets of the maximum size. */
void USBD_PrepareCtrlIn(const uint8_t *buf, uint32_t size)
{
    if (size > g_usbd_CtrlMaxPktSize) {
        g_usbd_CtrlInPointer = buf + g_usbd_CtrlMaxPktSize;
        g_usbd_CtrlInSize = size - g_usbd_CtrlMaxPktSize;
        USBD_CFG0 |= USBD_CFG_DSQ_SYNC;
        USBD_MemCopy((uint8_t *)&USB_SRAM[USBD_BUFSEG0], buf, g_usbd_CtrlMaxPktSize);
        USBD_MXPLD0 = g_usbd_CtrlMaxPktSize;
    } else {
        g_usbd_CtrlInPointer = 0;
        g_usbd_CtrlInSize = 0;
        USBD_CFG0 |= USBD_CFG_DSQ_SYNC;
        USBD_MemCopy((uint8_t *)&USB_SRAM[USBD_BUFSEG0], buf, size);
        USBD_MXPLD0 = size;
    }
}

/* Prepare to receive `size` bytes (the data stage of a request) into `buf`. */
void USBD_PrepareCtrlOut(uint8_t *buf, uint32_t size)
{
    g_usbd_CtrlOutPointer = buf;
    g_usbd_CtrlOutSize = 0;
    g_usbd_CtrlOutSizeLimit = size;
    USBD_MXPLD1 = g_usbd_CtrlMaxPktSize;
}

/* ---- Control transfers ----------------------------------------------------------------------- */

/* The data of a control read was taken by the host: send the next packet of it, or finish. The address
 * set by SET_ADDRESS takes effect after its status stage. */
void USBD_CtrlIn(void)
{
    uint32_t address;

    if (g_usbd_CtrlInSize == 0) {
        if (g_usbd_SetupPacket[0] == 0 && g_usbd_SetupPacket[1] == SET_ADDRESS) {
            address = USBD_FADDR;
            if (address != g_usbd_UsbAddr && address == 0) {
                USBD_FADDR = g_usbd_UsbAddr;
            }
        }
        if (g_usbd_CtrlInZeroFlag != 0) {
            USBD_MXPLD0 = 0;
            g_usbd_CtrlInZeroFlag = 0;
        }
    } else if (g_usbd_CtrlMaxPktSize < g_usbd_CtrlInSize) {
        USBD_MemCopy((uint8_t *)&USB_SRAM[USBD_BUFSEG0], g_usbd_CtrlInPointer, g_usbd_CtrlMaxPktSize);
        USBD_MXPLD0 = g_usbd_CtrlMaxPktSize;
        g_usbd_CtrlInPointer += g_usbd_CtrlMaxPktSize;
        g_usbd_CtrlInSize -= g_usbd_CtrlMaxPktSize;
    } else {
        USBD_MemCopy((uint8_t *)&USB_SRAM[USBD_BUFSEG0], g_usbd_CtrlInPointer, g_usbd_CtrlInSize);
        USBD_MXPLD0 = g_usbd_CtrlInSize;
        g_usbd_CtrlInPointer = 0;
        g_usbd_CtrlInSize = 0;
    }
}

/* A packet of the data stage of a control write arrived: take it and ask for the next. */
void USBD_CtrlOut(void)
{
    uint32_t length;

    if (g_usbd_CtrlOutSize < g_usbd_CtrlOutSizeLimit) {
        length = USBD_MXPLD1;
        USBD_MemCopy(g_usbd_CtrlOutPointer, (uint8_t *)&USB_SRAM[USBD_BUFSEG1], length);
        g_usbd_CtrlOutPointer += length;
        g_usbd_CtrlOutSize += length;
        if (g_usbd_CtrlOutSize < g_usbd_CtrlOutSizeLimit) {
            USBD_MXPLD1 = g_usbd_CtrlMaxPktSize;
        }
    }
}

/* Send a descriptor: at most wLength bytes of it; when it is shorter than that and a multiple of the packet
 * size, the transfer ends with an empty packet. */
static void send_descriptor(const volatile uint8_t *descriptor, uint32_t descriptor_length, uint32_t requested)
{
    if (descriptor_length < requested) {
        requested = descriptor_length;
        if (descriptor_length == g_usbd_CtrlMaxPktSize * (descriptor_length / g_usbd_CtrlMaxPktSize)) {
            g_usbd_CtrlInZeroFlag = 1;
        }
    }
    USBD_PrepareCtrlIn((const uint8_t *)descriptor, requested);
}

/* GET_DESCRIPTOR. */
void USBD_GetDescriptor(void)
{
    volatile usbd_info_t *info = g_usbd_sInfo;
    uint32_t requested;
    uint8_t type = g_usbd_SetupPacket[3];
    uint8_t index = g_usbd_SetupPacket[2];
    uint8_t interface = g_usbd_SetupPacket[4];

    g_usbd_CtrlInZeroFlag = 0;
    requested = g_usbd_SetupPacket[7] * 0x100 + g_usbd_SetupPacket[6];
    switch (type) {
    case DESC_BOS:
        send_descriptor(info->gu8BosDesc, info->gu8BosDesc[2] + info->gu8BosDesc[3] * 0x100, requested);
        break;
    case DESC_DEVICE:
        if (requested > 0x11) {
            requested = 0x12;
        }
        USBD_PrepareCtrlIn((const uint8_t *)info->gu8DevDesc, requested);
        break;
    case DESC_CONFIG:
        send_descriptor(info->gu8ConfigDesc, info->gu8ConfigDesc[2] + info->gu8ConfigDesc[3] * 0x100, requested);
        break;
    case DESC_STRING:
        if (index < 0xff) {
            send_descriptor(info->gu8StringDesc[index], info->gu8StringDesc[index][0], requested);
        } else {
            stall_control();
        }
        break;
    case DESC_HID:
        if (requested > 8) {
            requested = 9;
        }
        {
            uint32_t offset = info->gu32ConfigHidDescIdx[interface];     /* where the HID descriptor is in the configuration */

            USBD_PrepareCtrlIn((const uint8_t *)info->gu8ConfigDesc + offset, requested);
        }
        break;
    case DESC_HID_REPORT:
        if (info->gu32HidReportSize[interface] < requested) {
            requested = info->gu32HidReportSize[interface];
            if (requested == g_usbd_CtrlMaxPktSize * (requested / g_usbd_CtrlMaxPktSize)) {
                g_usbd_CtrlInZeroFlag = 1;
            }
        }
        USBD_PrepareCtrlIn((const uint8_t *)info->gu8HidReportDesc[interface], requested);
        break;
    default:
        stall_control();
        break;
    }
}

/* Requests that every device answers. */
void USBD_StandardRequest(void)
{
    uint32_t i;

    g_usbd_CtrlInPointer = 0;
    g_usbd_CtrlInSize = 0;
    if ((g_usbd_SetupPacket[0] & REQ_DEVICE_TO_HOST) == REQ_DEVICE_TO_HOST) {
        switch (g_usbd_SetupPacket[1]) {
        case GET_STATUS: {
            volatile usbd_info_t *info = g_usbd_sInfo;

            if (g_usbd_SetupPacket[0] == 0x80) {            /* device: self powered, remote wakeup */
                uint8_t status = (info->gu8ConfigDesc[7] & 0x40) == 0x40;

                if ((info->gu8ConfigDesc[7] & 0x20) == 0x20) {
                    status = status | g_usbd_RemoteWakeupEn << 1;
                }
                USB_SRAM[USBD_BUFSEG0] = status;
            } else if (g_usbd_SetupPacket[0] == 0x81) {     /* interface */
                USB_SRAM[USBD_BUFSEG0] = 0;
            } else if (g_usbd_SetupPacket[0] == 0x82) {     /* endpoint: is it stalled? */
                uint32_t buffer = USBD_BUFSEG0;
                volatile uint32_t *cfgp = (volatile uint32_t *)&USB_SRAM[0];

                for (i = 0; i < EP_COUNT; i++) {
                    cfgp = &ENDPOINT_CFG(i);
                    if ((*cfgp & 0xf) == (g_usbd_SetupPacket[4] & 0xf)) {
                        cfgp = &ENDPOINT_CFGP(i);
                        break;
                    }
                }
                USB_SRAM[buffer] = (*cfgp & USBD_CFG_STATE_STALL) != 0;
            }
            USB_SRAM[USBD_BUFSEG0 + 1] = 0;
            USBD_CFG0 |= USBD_CFG_DSQ_SYNC;
            USBD_MXPLD0 = 2;
            USBD_PrepareCtrlOut(0, 0);
            break;
        }
        case GET_DESCRIPTOR:
            USBD_GetDescriptor();
            USBD_PrepareCtrlOut(0, 0);
            break;
        case GET_CONFIGURATION:
            USB_SRAM[USBD_BUFSEG0] = (uint8_t)g_usbd_UsbConfig;
            USBD_CFG0 |= USBD_CFG_DSQ_SYNC;
            USBD_MXPLD0 = 1;
            USBD_PrepareCtrlOut(0, 0);
            break;
        case GET_INTERFACE:
            USB_SRAM[USBD_BUFSEG0] = (uint8_t)g_usbd_UsbAltInterface;
            USBD_CFG0 |= USBD_CFG_DSQ_SYNC;
            USBD_MXPLD0 = 1;
            USBD_PrepareCtrlOut(0, 0);
            break;
        default:
            stall_control();
            break;
        }
        return;
    }
    switch (g_usbd_SetupPacket[1]) {
    case SET_ADDRESS:
        g_usbd_UsbAddr = g_usbd_SetupPacket[2];
        status_in();
        break;
    case CLEAR_FEATURE:
        if (g_usbd_SetupPacket[2] == 0) {                   /* ENDPOINT_HALT */
            uint32_t epnum = g_usbd_SetupPacket[4];

            for (i = 0; i < EP_COUNT; i++) {
                if ((ENDPOINT_CFG(i) & 0xf) == (epnum & 0xf) && (1 << (i & 0xff) & g_u32EpStallLock) == 0) {
                    ENDPOINT_CFGP(i) &= ~USBD_CFG_STATE_STALL;
                    ENDPOINT_CFG(i) &= ~USBD_CFG_DSQ_SYNC;
                }
            }
        } else if (g_usbd_SetupPacket[2] == 1) {            /* DEVICE_REMOTE_WAKEUP */
            g_usbd_RemoteWakeupEn = 0;
        }
        status_in();
        break;
    case SET_CONFIGURATION:
        g_usbd_UsbConfig = g_usbd_SetupPacket[2];
        if (g_usbd_pfnSetConfigCallback != 0) {
            g_usbd_pfnSetConfigCallback();
        }
        status_in();
        break;
    case SET_INTERFACE:
        g_usbd_UsbAltInterface = g_usbd_SetupPacket[2];
        if (g_usbd_pfnSetInterface != 0) {
            g_usbd_pfnSetInterface(g_usbd_UsbAltInterface);
        }
        status_in();
        break;
    case SET_FEATURE:
        if ((g_usbd_SetupPacket[0] & 0xf) == 0 && g_usbd_SetupPacket[2] == 3 && g_usbd_SetupPacket[3] == 0) {
            OTG_CTL |= 6;                                   /* test mode request */
        }
        if (g_usbd_SetupPacket[2] == 0) {                   /* ENDPOINT_HALT */
            for (i = 0; i < EP_COUNT; i++) {
                if ((ENDPOINT_CFG(i) & 0xf) == (g_usbd_SetupPacket[4] & 0xf)) {
                    ENDPOINT_CFGP(i) |= USBD_CFG_STATE_STALL;
                    break;
                }
            }
        } else if (g_usbd_SetupPacket[2] == 1) {
            g_usbd_RemoteWakeupEn = g_usbd_SetupPacket[2];
        }
        status_in();
        break;
    default:
        stall_control();
        break;
    }
}

/* A setup packet arrived: take it and hand it to the right handler. */
void USBD_ProcessSetupPacket(void)
{
    uint8_t type;

    USBD_MemCopy((uint8_t *)&g_usbd_SetupPacket[0], (const uint8_t *)USB_SRAM, 8);
    type = g_usbd_SetupPacket[0] & REQ_TYPE_MASK;
    if (type == REQ_TYPE_STANDARD) {
        USBD_StandardRequest();
    } else if (type == REQ_TYPE_CLASS) {
        if (g_usbd_pfnClassRequest != 0) {
            g_usbd_pfnClassRequest();
        }
    } else if (type == REQ_TYPE_VENDOR) {
        if (g_usbd_pfnVendorRequest != 0) {
            g_usbd_pfnVendorRequest();
        }
    } else {
        stall_control();
    }
}

/* ---- Interrupt -------------------------------------------------------------------------------- */

/* USB interrupt: bus events (VBUS, reset, suspend, resume) and the transfers of the endpoints. */
void USBD_IRQHandler(void)
{
    uint32_t status = USBD_INTSTS;
    uint32_t attr = USBD_ATTR;

    if ((status & USBD_INTSTS_VBDET) != 0) {
        USBD_INTSTS = USBD_INTSTS_VBDET;
        if ((USBD_VBUSDET & 1) == 0) {
            USBD_ATTR &= ~USBD_ATTR_DPPU_EN;            /* unplugged */
        } else {
            USBD_ATTR |= USBD_ATTR_RUN;
        }
    }
    if ((status & USBD_INTSTS_BUS) != 0) {
        USBD_INTSTS = USBD_INTSTS_BUS;
        if ((attr & USBD_ATTR_USBRST) != 0) {
            USBD_ATTR |= USBD_ATTR_RUN;
            USBD_SwReset();
        }
        if ((attr & USBD_ATTR_SUSPEND) != 0) {
            USBD_ATTR &= ~USBD_ATTR_PHY_EN;
        }
        if ((attr & USBD_ATTR_RESUME) != 0) {
            USBD_ATTR |= USBD_ATTR_RUN;
        }
    }
    if ((status & USBD_INTSTS_NEVWK) != 0) {
        USBD_INTSTS = USBD_INTSTS_NEVWK;
    }
    if ((status & USBD_INTSTS_USB) != 0) {
        if ((status & USBD_INTSTS_SETUP) != 0) {
            USBD_INTSTS = USBD_INTSTS_SETUP;
            USBD_CFGP0 |= USBD_CFGP_CLRRDY;
            USBD_CFGP1 |= USBD_CFGP_CLRRDY;
            USBD_ProcessSetupPacket();
        }
        if ((status & USBD_INTSTS_EP(0)) != 0) {
            USBD_INTSTS = USBD_INTSTS_EP(0);
            USBD_CtrlIn();
        }
        if ((status & USBD_INTSTS_EP(1)) != 0) {
            USBD_INTSTS = USBD_INTSTS_EP(1);
            USBD_CtrlOut();
        }
        if ((status & USBD_INTSTS_EP(2)) != 0) {
            USBD_INTSTS = USBD_INTSTS_EP(2);
            EP2_Handler();
        }
        if ((status & USBD_INTSTS_EP(3)) != 0) {
            USBD_INTSTS = USBD_INTSTS_EP(3);
            EP3_Handler();
        }
        if ((status & USBD_INTSTS_EP(4)) != 0) {
            USBD_INTSTS = USBD_INTSTS_EP(4);
            EP4_Handler();
        }
        if ((status & USBD_INTSTS_EP(5)) != 0) {
            USBD_INTSTS = USBD_INTSTS_EP(5);
            EP5_Handler();
        }
        if ((status & USBD_INTSTS_EP(6)) != 0) {
            USBD_INTSTS = USBD_INTSTS_EP(6);
            EP6_Handler();
        }
        if ((status & USBD_INTSTS_EP(7)) != 0) {
            USBD_INTSTS = USBD_INTSTS_EP(7);
            EP7_Handler();
        }
    }
}
