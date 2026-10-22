/* The IO4-Emulation personality: the controller presents itself to the game the way the arcade hardware does.
 *
 *   - an I/O board ("I/O CONTROL BD", SEGA's IO4) on a HID interface: air beams, the TEST and SERVICE buttons, the coins, and
 *     the commands of the game, among them the PWM outputs that light the light gate;
 *   - the slider on a virtual serial port (CDC): the pressure of the 32 pads and the slider LEDs (io4_slider.c);
 *   - the two LED boards of the "side devices" on two more serial ports: the air towers (io4_led_board.c);
 *   - the command channel of Supernova, on HID interface 1 as in every mode, so that the Toolkit keeps working.
 *
 * The game finds the I/O board by its USB ID, which has to be the one of the real board. Both devices are standard
 * classes of Windows, which needs no driver of its own. It is based on the dump of the real board in the
 * documentation of host-aprom and on tasoller-plus. */

#include "ss321.h"

/* IO4 commands of the host (report 16) */
#define IO4_CMD_SET_COMM_TIMEOUT        0x01
#define IO4_CMD_SET_SAMPLING_COUNT      0x02
#define IO4_CMD_CLEAR_BOARD_STATUS      0x03
#define IO4_CMD_SET_GENERAL_OUTPUT      0x04
#define IO4_CMD_SET_PWM_OUTPUT          0x05
#define IO4_CMD_SET_UNIQUE_OUTPUT       0x41

#define IO4_REPORT_ID_INPUT             1
#define IO4_REPORT_ID_OUTPUT            16

#define IO4_STATUS_RESET_POWER_ON       1
#define IO4_STATUS_TIMEOUT_SET          0x10
#define IO4_STATUS_SAMPLING_SET         0x20

#define IO4_BUTTON_SERVICE              (1 << 6)
#define IO4_BUTTON_TEST                 (1 << 9)


/* Class requests */
#define HID_GET_REPORT                  1
#define HID_GET_IDLE                    2
#define HID_GET_PROTOCOL                3
#define HID_SET_REPORT                  9
#define HID_SET_IDLE                    10
#define HID_SET_PROTOCOL                11
#define CDC_SET_LINE_CODING             0x20
#define CDC_GET_LINE_CODING             0x21
#define CDC_SET_CONTROL_LINE_STATE      0x22

/* The identity of the real board: its product string says what it can do (see docs/IO4.md of host-aprom). */
#define IO4_VENDOR_STRING   "SEGA INTERACTIVE"
#define IO4_PRODUCT_STRING                                                                                    \
    "I/O CONTROL BD;15257   ;01;91;3EEE;6679B;00;GOUT=14_ADIN=8,E_ROTIN=4_COININ=2_SWIN=2,E_UQ1=41,6;"
#define CDC_NAME_STRING     "Supernova Slider - COM1"           /* the number the game needs: it is the name Windows shows for the port */
#define LED_NAME_STRING_LEFT    "Supernova Side LED L - COM2"
#define LED_NAME_STRING_RIGHT   "Supernova Side LED R - COM3"

/* ---- Descriptors ---------------------------------------------------------------------------- */

static const uint8_t io4_device_descriptor[18] = {
    0x12, 0x01, 0x01, 0x02,             /* USB 2.01: the host asks for the BOS descriptor */
    0xef, 0x02, 0x01,                   /* a composite device with interface association descriptors */
    0x08,                               /* the control endpoint has 8 byte packets, as in the other modes */
    0xa3, 0x0c, 0x21, 0x00,             /* 0CA3:0021, the ID of the IO4 board */
    0x00, 0x01,                         /* device 1.00 */
    0x01, 0x02, 0x00,                   /* manufacturer, product, no serial number */
    0x01,
};

/* The same with the static light: another device version. Windows keeps what it learned of a device (its interfaces, its COM ports) by its
 * USB ID and version, so a configuration with other interfaces under the same version may not be taken: with its own version this one is a
 * new device for it, enumerated from scratch, and the WinUSB interface of the setup tool is found. */
static const uint8_t io4_device_static_descriptor[18] = {
    0x12, 0x01, 0x01, 0x02, 0xef, 0x02, 0x01, 0x08, 0xa3, 0x0c, 0x21, 0x00,
    0x01, 0x01,                         /* device 1.01 */
    0x01, 0x02, 0x00, 0x01,
};

/* The HID report descriptor of the board (docs/IO4.md of host-aprom): 14 analog values, 48 buttons, 29 bytes that are
 * not used, and the command report with the ID 16. */
static const uint8_t io4_report_descriptor[] = {
    0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x09, 0x30, 0x09, 0x31, 0x09, 0x30, 0x09, 0x31,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34, 0x09, 0x33, 0x09, 0x34, 0x09, 0x36, 0x09, 0x36,
    0x15, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00, 0x35, 0x00, 0x47, 0xff, 0xff, 0x00, 0x00, 0x95, 0x0e, 0x75, 0x10, 0x81, 0x02,
    0xc0, 0x05, 0x02, 0x05, 0x09, 0x19, 0x01, 0x29, 0x30, 0x15, 0x00, 0x25, 0x01, 0x45, 0x01, 0x75, 0x01, 0x95, 0x30, 0x81,
    0x02, 0x09, 0x00, 0x75, 0x08, 0x95, 0x1d, 0x81, 0x01, 0x06, 0xa0, 0xff, 0x09, 0x00, 0x85, 0x10, 0xa1, 0x01, 0x09, 0x00,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x3f, 0x91, 0x02, 0xc0, 0xc0,
};
_Static_assert(sizeof(io4_report_descriptor) == 0x71, "the HID report descriptor of the IO4 board is 113 bytes");

/* Interface 0 the I/O board, interface 1 the command channel of Supernova (the same endpoints and report as in the
 * other modes), interfaces 2 and 3 the serial port of the slider. */
#define IO4_DESC_COMMON \
    /* interface 0: HID, the I/O board; the commands of the host come as SET_REPORT on the control endpoint */ \
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x04, \
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x71, 0x00, \
    0x07, 0x05, 0x84, 0x03, 0x40, 0x00, 0x08,                               /* IN 4, every 8 ms */ \
    /* interface 1: HID, the command channel */ \
    0x09, 0x04, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x05, \
    0x09, 0x21, 0x10, 0x01, 0x00, 0x01, 0x22, 0x1f, 0x00, \
    0x07, 0x05, 0x85, 0x03, 0x40, 0x00, 0x01,                               /* IN 5 */ \
    0x07, 0x05, 0x06, 0x03, 0x40, 0x00, 0x01,                               /* OUT 6 */ \
    /* interfaces 2 and 3: the virtual serial port of the slider (CDC ACM) */ \
    0x08, 0x0b, 0x02, 0x02, 0x02, 0x02, 0x00, 0x03,                         /* association of the two interfaces */ \
    0x09, 0x04, 0x02, 0x00, 0x01, 0x02, 0x02, 0x00, 0x00, \
    0x05, 0x24, 0x00, 0x10, 0x01, \
    0x05, 0x24, 0x01, 0x00, 0x03, \
    0x04, 0x24, 0x02, 0x02, \
    0x05, 0x24, 0x06, 0x02, 0x03, \
    0x07, 0x05, 0x87, 0x03, 0x10, 0x00, 0x0a,                               /* IN 7: notifications, not used */ \
    0x09, 0x04, 0x03, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x00, \
    0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,                               /* IN 1: bulk, to the host */ \
    0x07, 0x05, 0x03, 0x02, 0x40, 0x00, 0x00,                               /* OUT 3: bulk, from the host */

/* With the LED boards emulated (the default): interfaces 4 and 5 the serial port of the LED board on the left, 6 and 7 the one on
 * the right. The LED boards have no notification endpoint: there are not enough endpoints in the controller. */
static const uint8_t io4_config_descriptor[250] = {
    0x09, 0x02, 0xfa, 0x00, 0x08, 0x01, 0x00, 0x80, 0xfa,                   /* configuration: 8 interfaces */
    IO4_DESC_COMMON
    /* interfaces 4 and 5: the LED board on the left */
    0x08, 0x0b, 0x04, 0x02, 0x02, 0x02, 0x00, 0x06,
    0x09, 0x04, 0x04, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x05,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x04, 0x05,
    0x09, 0x04, 0x05, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x88, 0x02, 0x20, 0x00, 0x00,                               /* IN 8 */
    0x07, 0x05, 0x02, 0x02, 0x20, 0x00, 0x00,                               /* OUT 2 */
    /* interfaces 6 and 7: the LED board on the right */
    0x08, 0x0b, 0x06, 0x02, 0x02, 0x02, 0x00, 0x07,
    0x09, 0x04, 0x06, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x07,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x06, 0x07,
    0x09, 0x04, 0x07, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x8a, 0x02, 0x20, 0x00, 0x00,                               /* IN 10 */
    0x07, 0x05, 0x09, 0x02, 0x20, 0x00, 0x00,                               /* OUT 9 */
};

/* With the static light of the air towers: no LED boards, and interface 4 a vendor interface (WinUSB) with one bulk endpoint, OUT 2, for
 * the LED packets of the setup tool (the same as the ones of the WinUSB mode). The game never uses it. */
static const uint8_t io4_config_static_descriptor[IO4_CONFIG_STATIC_SIZE] = {
    0x09, 0x02, IO4_CONFIG_STATIC_SIZE & 0xff, IO4_CONFIG_STATIC_SIZE >> 8, 0x05, 0x01, 0x00, 0x80, 0xfa,        /* configuration: 5 interfaces */
    IO4_DESC_COMMON
    /* interface 4: vendor, the LED packets of the setup tool */
    0x09, 0x04, 0x04, 0x00, 0x01, 0xff, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,                               /* OUT 2 */
};

/* The BOS descriptor and the Microsoft OS 2.0 descriptor set (vendor request 0xfc): they give each serial port the COM number the
 * game needs (the registry property PortName of its function). Windows may ignore this; the numbers can always be set by hand.
 * With the static light the set also tells Windows to use WinUSB for interface 4. */
#define BOS_DESCRIPTOR(size)                                                                                       \
    0x05, 0x0f, 0x21, 0x00, 0x01,                                                                                  \
    0x1c, 0x10, 0x05, 0x00,                                                                                        \
    0xdf, 0x60, 0xdd, 0xd8, 0x89, 0x45, 0xc7, 0x4c, 0x9c, 0xd2, 0x65, 0x9d, 0x9e, 0x64, 0x8a, 0x9f,                \
    0x00, 0x00, 0x03, 0x06,                                 /* Windows 8.1 and later */                            \
    (size) & 0xff, (size) >> 8,                                                                                    \
    0xfc, 0x00                                              /* the vendor request, no alternate enumeration */

const uint8_t io4_bos_descriptor[33] = { BOS_DESCRIPTOR(IO4_MS_OS_SIZE) };
const uint8_t io4_bos_static_descriptor[33] = { BOS_DESCRIPTOR(IO4_MS_OS_STATIC_SIZE) };

#define PORT_NAME(first_interface, digit)                                                                          \
    0x08, 0x00, 0x02, 0x00, first_interface, 0x00, 0x2e, 0x00,          /* function subset */                      \
    0x26, 0x00, 0x04, 0x00, 0x01, 0x00, 0x12, 0x00,                     /* registry property, REG_SZ, name 18 bytes */ \
    'P', 0, 'o', 0, 'r', 0, 't', 0, 'N', 0, 'a', 0, 'm', 0, 'e', 0, 0, 0,                                          \
    0x0a, 0x00, 'C', 0, 'O', 0, 'M', 0, digit, 0, 0, 0

#define MS_OS_HEADER(size)                                                                                         \
    0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, (size) & 0xff, (size) >> 8,                                    \
    0x08, 0x00, 0x01, 0x00, 0x00, 0x00, ((size) - 10) & 0xff, ((size) - 10) >> 8

const uint8_t io4_ms_os_descriptor[IO4_MS_OS_SIZE] = {
    MS_OS_HEADER(IO4_MS_OS_SIZE),
    PORT_NAME(2, '1'),                  /* the slider */
    PORT_NAME(4, '2'),                  /* the LED board on the left */
    PORT_NAME(6, '3'),                  /* the LED board on the right */
};

const uint8_t io4_ms_os_static_descriptor[IO4_MS_OS_STATIC_SIZE] = {
    MS_OS_HEADER(IO4_MS_OS_STATIC_SIZE),
    PORT_NAME(2, '1'),                  /* the slider */
    0x08, 0x00, 0x02, 0x00, 0x04, 0x00, 0xa0, 0x00,                     /* function subset: interface 4, WinUSB */
    0x14, 0x00, 0x03, 0x00, 0x57, 0x49, 0x4e, 0x55, 0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x84, 0x00, 0x04, 0x00, 0x07, 0x00, 0x2a, 0x00, 0x44, 0x00, 0x65, 0x00,
    0x76, 0x00, 0x69, 0x00, 0x63, 0x00, 0x65, 0x00, 0x49, 0x00, 0x6e, 0x00,
    0x74, 0x00, 0x65, 0x00, 0x72, 0x00, 0x66, 0x00, 0x61, 0x00, 0x63, 0x00,
    0x65, 0x00, 0x47, 0x00, 0x55, 0x00, 0x49, 0x00, 0x44, 0x00, 0x73, 0x00,
    0x00, 0x00, 0x50, 0x00, 0x7b, 0x00, 0x31, 0x00, 0x44, 0x00, 0x34, 0x00,
    0x42, 0x00, 0x32, 0x00, 0x33, 0x00, 0x36, 0x00, 0x35, 0x00, 0x2d, 0x00,
    0x34, 0x00, 0x37, 0x00, 0x34, 0x00, 0x39, 0x00, 0x2d, 0x00, 0x34, 0x00,
    0x38, 0x00, 0x45, 0x00, 0x41, 0x00, 0x2d, 0x00, 0x42, 0x00, 0x33, 0x00,
    0x38, 0x00, 0x41, 0x00, 0x2d, 0x00, 0x37, 0x00, 0x43, 0x00, 0x36, 0x00,
    0x46, 0x00, 0x44, 0x00, 0x44, 0x00, 0x44, 0x00, 0x44, 0x00, 0x37, 0x00,
    0x45, 0x00, 0x32, 0x00, 0x36, 0x00, 0x7d, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* The strings: 0 is the language, 1 to 5 are set below, the rest are empty so that an unusual request gets an answer. */
static const uint8_t empty_string[2] = { 2, 3 };
#define IO4_FIELD(name)         ((const volatile uint8_t *)(0x20022000u + offsetof(io4_state_t, name)))
#define STRING_LANGUAGE         ((const volatile uint8_t *)0x20000184u)         /* stock string 0: English (US) */
static const volatile uint8_t * const io4_string_table[256] = {
    [0 ... 255] = empty_string,
    [0] = STRING_LANGUAGE,
    [1] = IO4_FIELD(string_manufacturer),
    [2] = IO4_FIELD(string_product),
    [3] = IO4_FIELD(string_cdc),
    [4] = IO4_FIELD(string_io4),
    [5] = IO4_FIELD(string_command),            /* the command channel of Supernova: named after the mode, the host shows it as the description */
    [6] = IO4_FIELD(string_led[0]),
    [7] = IO4_FIELD(string_led[1]),
};

/* ---- The IO4 board -------------------------------------------------------------------------- */

/* ---- The function buttons ---------------------------------------------------------------------- */

/* A function button sends any signals of the board: a mask of its 32 buttons (the two 16-bit words of the report, the first player in the low one,
 * as in the pin list of the documentation of host-aprom), and the coin, which is not a button: each press adds a credit. The air beams are the
 * buttons PUSH 3, 4 and 5 of both players; their bit is set while the beam is free, so a function button that sends one of them interrupts that beam. */
#define IO4_SIGNAL_TEST         (1u << 9)
#define IO4_SIGNAL_SERVICE      (1u << 6)
#define IO4_AIR_BITS            0x38003800u

/* The signals of FN1 (0), FN2 (1) or both together (2): added to *mask and *coin. */
static void io4_signals_of(uint32_t which, uint32_t *mask, uint32_t *coin)
{
    uint32_t i;
    volatile uint8_t *bytes = &settings.spare[SNV_FN_FIRST + 5 * which];

    if (settings.spare[SNV_FN_MARKER] != SNV_FN_MARK) {
        *mask |= which == 0 ? IO4_SIGNAL_TEST : which == 1 ? IO4_SIGNAL_SERVICE : 0;
        return;
    }
    for (i = 0; i < 4; i++) {
        *mask |= (uint32_t)bytes[i] << (8 * i);
    }
    if (bytes[4] == 1) {
        *coin = 1;
    }
}

/* The buttons of the board that the function buttons send, given which of them is pressed: both together send the signals for both when there are
 * some, else each sends its own. Each press that sends the coin adds a credit. */
static uint32_t io4_function_buttons(uint32_t fn1, uint32_t fn2)
{
    uint32_t mask = 0, coin = 0, both_mask = 0, both_coin = 0;

    io4_signals_of(2, &both_mask, &both_coin);
    if (fn1 && fn2 && (both_mask != 0 || both_coin != 0)) {
        mask = both_mask;
        coin = both_coin;
    } else {
        if (fn1) {
            io4_signals_of(0, &mask, &coin);
        }
        if (fn2) {
            io4_signals_of(1, &mask, &coin);
        }
    }
    if (coin && !io4.fn_coin) {
        io4.coins[0]++;
    }
    io4.fn_coin = (uint8_t)coin;
    return mask;
}

/* CMD_IO4_BUTTONS. [0] -> [0, 15 bytes]; [1, 15 bytes, save] sets them (the report uses them at once) and replies [1]. The 15 bytes are those of
 * the settings, five for each of FN1, FN2 and both together (see SNV_FN_FIRST). */
uint32_t io4_buttons_command(const uint8_t *request)
{
    uint32_t i, which;

    if (request[3] == 0) {
        command_reply.data[0] = 0;
        for (which = 0; which < 3; which++) {
            uint32_t mask = 0, coin = 0;

            io4_signals_of(which, &mask, &coin);
            for (i = 0; i < 4; i++) {
                command_reply.data[1 + 5 * which + i] = (uint8_t)(mask >> (8 * i));
            }
            command_reply.data[1 + 5 * which + 4] = (uint8_t)coin;
        }
        command_reply.length += 16;
        return 0;
    }
    if (request[3] != 1 || request[0] < 2 + 1 + 15 + 1 || request[8] > 1 || request[13] > 1 || request[18] > 1) {
        return 1;
    }
    settings.spare[SNV_FN_MARKER] = SNV_FN_MARK;
    for (i = 0; i < 15; i++) {
        settings.spare[SNV_FN_FIRST + i] = request[4 + i];
    }
    if (request[19]) {
        settings_flush(1);
    }
    command_reply.data[0] = 1;
    command_reply.length += 1;
    return 0;
}

/* The input report: the state of the controller as the board reports it (docs/IO4.md of host-aprom). The pads are not part
 * of it, they go to the serial port. Called in the USB interrupt each time the host has taken the previous report. */
uint8_t *io4_report_in(uint32_t unused, uint16_t *length)
{
    uint16_t buttons[2] = { 0, 0 };
    uint8_t beams = dbt_packet.buttons;
    uint32_t beam, i, signals;

    for (i = 0; i < sizeof io4.report; i++) {
        io4.report[i] = 0;
    }
    io4.report[0] = IO4_REPORT_ID_INPUT;
    /* the six air beams, from the bottom, are the buttons 13, 12 and 11 of the two players. Their bit is set while the beam is
     * free and clear while it is interrupted, the other way round from the TEST and SERVICE buttons: the game takes a board
     * that reports them all interrupted for a board that is not there. */
    for (beam = 0; beam < AIR_BEAMS; beam++) {
        if (!(beams & (1u << beam))) {
            buttons[beam & 1] |= 1u << (13 - beam / 2);
        }
    }
    signals = io4_function_buttons(beams & BUTTON_TEST, beams & BUTTON_SERVICE);     /* FN1, FN2 */
    buttons[0] |= (uint16_t)(signals & ~IO4_AIR_BITS);
    buttons[1] |= (uint16_t)((signals & ~IO4_AIR_BITS) >> 16);
    buttons[0] &= (uint16_t)~(signals & IO4_AIR_BITS);                          /* a beam that a function button sends is interrupted */
    buttons[1] &= (uint16_t)~((signals & IO4_AIR_BITS) >> 16);
    io4.report[25] = (uint8_t)(io4.coins[0] >> 8);              /* the coin counters are big endian */
    io4.report[26] = (uint8_t)io4.coins[0];
    io4.report[27] = (uint8_t)(io4.coins[1] >> 8);
    io4.report[28] = (uint8_t)io4.coins[1];
    io4.report[29] = (uint8_t)buttons[0];
    io4.report[30] = (uint8_t)(buttons[0] >> 8);
    io4.report[31] = (uint8_t)buttons[1];
    io4.report[32] = (uint8_t)(buttons[1] >> 8);
    io4.report[33] = io4.system_status;
    io4.report[34] = io4.usb_status;
    *length = sizeof io4.report;
    return (uint8_t *)io4.report;
}

/* One command of the host, in `data` with `size` bytes of arguments. */
static void io4_command(uint8_t command, uint32_t size, const volatile uint8_t *data)
{
    uint32_t i;

    switch (command) {
    case IO4_CMD_SET_COMM_TIMEOUT:
        if (size >= 1) {
            io4.system_status |= IO4_STATUS_TIMEOUT_SET;        /* the time-out itself is not used: the board would reset */
        }
        break;
    case IO4_CMD_SET_SAMPLING_COUNT:
        if (size >= 1) {
            io4.system_status |= IO4_STATUS_SAMPLING_SET;       /* the inputs are debounced already */
        }
        break;
    case IO4_CMD_CLEAR_BOARD_STATUS:
        io4.system_status = 0;
        io4.usb_status = 0;
        break;
    case IO4_CMD_SET_GENERAL_OUTPUT:
        if (size >= 3) {
            for (i = 0; i < 3; i++) {
                io4.digital_out[i] = data[i];
            }
        }
        break;
    case IO4_CMD_SET_UNIQUE_OUTPUT:
        /* [0] the PWM outputs that are enabled (bit 7 is the first), [1] the brightness (0 is 256), [2..7] the six outputs.
         * Chunithm uses the third to the fifth for the light gate: blue, red, green. */
        if (size >= 62) {
            io4.pwm_scale = data[1] == 0 ? 255 : data[1] - 1;
            for (i = 0; i < 6; i++) {
                io4.pwm[i] = (data[0] & (0x80 >> i)) ? data[2 + i] : 0;     /* the "gate LED": it is the cabinet's, nothing to light here */
            }
        }
        break;
    default:                            /* SET_PWM_OUTPUT has no data on this board, the others are for the updater */
        break;
    }
}

/* The data stage of a SET_REPORT arrived (the USB interrupt): the main loop handles it. */
static void io4_out_done(void)
{
    io4.out_length = (uint8_t)g_usbd_CtrlOutSize;
}

/* A report written by the host with SET_REPORT: report 16, then the command and its arguments. */
static void io4_handle_out(void)
{
    uint8_t report[64];
    uint32_t length = io4.out_length, i;

    for (i = 0; i < 64; i++) {
        report[i] = i < length ? io4.out_report[i] : 0;
    }
    io4.out_length = 0;
    if (length >= 2 && report[0] == IO4_REPORT_ID_OUTPUT) {
        io4_command(report[1], length - 2, &report[2]);
    }
}

/* ---- Class requests ------------------------------------------------------------------------- */

/* Answer the request with the status stage of a transfer that has no data to send. */
static void io4_acknowledge(void)
{
    USBD_CFG0 |= 0x80;
    USBD_MXPLD0 = 0;
}

/* The class requests of the interfaces of this personality (HID_ClassRequest calls it first). False when the request is for
 * another interface, which keeps its usual handling. */
bool io4_class_request(const uint8_t *setup)
{
    uint8_t request = setup[1];
    uint32_t interface = setup[4];
    uint32_t length = setup[6] | (setup[7] << 8);
    bool to_host = (setup[0] & 0x80) != 0;
    uint16_t report_length;

    if (interface == IO4_INTERFACE_IO4) {
        if (to_host) {
            if (request == HID_GET_REPORT && setup[2] == IO4_REPORT_ID_INPUT) {
                io4_report_in(0, &report_length);
                USBD_PrepareCtrlIn((const uint8_t *)io4.report, length < report_length ? length : report_length);
                USBD_PrepareCtrlOut(0, 0);
            } else if (request == HID_GET_IDLE || request == HID_GET_PROTOCOL) {
                io4.out_report[63] = request == HID_GET_PROTOCOL;       /* idle 0, report protocol */
                USBD_PrepareCtrlIn((const uint8_t *)&io4.out_report[63], 1);
                USBD_PrepareCtrlOut(0, 0);
            } else {
                USBD_SetStall(0);
            }
        } else if (request == HID_SET_REPORT && setup[3] == 2) {        /* an output report */
            usbd_ctrl_out_done = io4_out_done;
            USBD_PrepareCtrlOut((uint8_t *)io4.out_report, length < sizeof io4.out_report ? length : sizeof io4.out_report);
            io4_acknowledge();
        } else {                                                        /* SET_IDLE, SET_PROTOCOL */
            io4_acknowledge();
        }
        return true;
    }
    if (interface == IO4_INTERFACE_CDC || (!settings_static_light() && (interface == IO4_INTERFACE_LED_L || interface == IO4_INTERFACE_LED_R))) {
        volatile uint8_t *line_coding = interface == IO4_INTERFACE_CDC ? io4.line_coding : io4.led_board[interface == IO4_INTERFACE_LED_R].line_coding;

        if (to_host && request == CDC_GET_LINE_CODING) {
            USBD_PrepareCtrlIn((const uint8_t *)line_coding, length < 7 ? length : 7);
            USBD_PrepareCtrlOut(0, 0);
        } else if (!to_host && request == CDC_SET_LINE_CODING) {
            USBD_PrepareCtrlOut((uint8_t *)line_coding, 7);
            io4_acknowledge();
        } else if (!to_host && request == CDC_SET_CONTROL_LINE_STATE) {
            if (interface != IO4_INTERFACE_CDC) {
                io4_led_board_line_state(interface == IO4_INTERFACE_LED_R, setup[2] | (setup[3] << 8));
            }
            io4_acknowledge();
        } else {
            USBD_SetStall(0);
        }
        return true;
    }
    return false;
}

/* ---- The personality ------------------------------------------------------------------------ */

/* Install the descriptors and clear the state. */
void io4_personality_init(void)
{
    uint32_t i;

    for (i = 0; i < sizeof(io4_state_t); i++) {
        ((volatile uint8_t *)&io4)[i] = 0;
    }
    io4.system_status = IO4_STATUS_RESET_POWER_ON;
    usb_string_descriptor_set((uint8_t *)io4.string_manufacturer, IO4_VENDOR_STRING);
    usb_string_descriptor_set((uint8_t *)io4.string_product, USB_NAME_IO4);
    usb_string_descriptor_set((uint8_t *)io4.string_cdc, CDC_NAME_STRING);
    usb_string_descriptor_set((uint8_t *)io4.string_io4, IO4_PRODUCT_STRING);
    usb_string_descriptor_set((uint8_t *)io4.string_command, USB_NAME_IO4);
    usb_string_descriptor_set((uint8_t *)io4.string_led[0], LED_NAME_STRING_LEFT);
    usb_string_descriptor_set((uint8_t *)io4.string_led[1], LED_NAME_STRING_RIGHT);
    usbd_info.gu8DevDesc = settings_static_light_saved() ? io4_device_static_descriptor : io4_device_descriptor;
    usbd_info.gu8ConfigDesc = settings_static_light_saved() ? io4_config_static_descriptor : io4_config_descriptor;
    usbd_info.gu8BosDesc = settings_static_light_saved() ? io4_bos_static_descriptor : io4_bos_descriptor;
    usbd_info.gu8StringDesc = io4_string_table;
    usb_hid_report_table[IO4_INTERFACE_IO4] = io4_report_descriptor;
    usb_hid_report_sizes[IO4_INTERFACE_IO4] = sizeof io4_report_descriptor;
    usb_hid_report_table[IO4_INTERFACE_COMMAND] = usb_hid_report_vendor;
    usb_hid_report_sizes[IO4_INTERFACE_COMMAND] = 0x1f;
    io4_slider_init();
    if (!settings_static_light_saved()) {
        io4_led_boards_init();
    }
}

/* Main loop task: the commands of the game and the slider. */
void io4_task(void)
{
    if (!settings_io4()) {
        return;
    }
    if (io4.out_length != 0) {
        io4_handle_out();
    }
    io4_slider_task();
    io4_led_boards_task();
}
