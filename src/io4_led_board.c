/* The two LED boards of the IO4-Emulation personality. The game lights the "side devices" of the cabinet through two LED
 * controller boards (837-15093-06), one on the left and one on the right, each on a serial port of its own (COM2 and COM3 for the CVT cabinet in the
 * segatools emulation). They are two more virtual serial ports of the device here, and the last three colors the game sends to
 * each board, which are the three groups of the air tower on that side, light the air towers. Based on segatools'
 * emulation of the board (common/board/led15093.c).
 *
 * A frame is  E0  destination  source  length  payload ...  checksum. The payload is a command and its data, the checksum is the
 * sum of everything after E0 (before escaping). E0 and D0 are escaped inside a frame: D0 and the byte minus one. The board
 * answers with the same framing, destination = the host (1), source = its own address, and a payload of status (1 is OK),
 * command and report (1 is OK), then the data of the command. */

#include "ss321.h"

#define SYNC                    0xe0
#define ESCAPE                  0xd0

#define CMD_RESET               0x10
#define CMD_SET_TIMEOUT         0x11
#define CMD_SET_DISABLE_RESPONSE 0x14
#define CMD_SET_ID              0x18
#define CMD_CLEAR_ID            0x19
#define CMD_SET_MAX_BRIGHT      0x3f
#define CMD_UPDATE_LED          0x80
#define CMD_SET_LED             0x81
#define CMD_SET_IMM_LED         0x82
#define CMD_SET_FADE_LED        0x83
#define CMD_SET_FADE_LEVEL      0x84
#define CMD_SET_FADE_SHIFT      0x85
#define CMD_SET_AUTO_SHIFT      0x86
#define CMD_GET_BOARD_INFO      0xf0
#define CMD_GET_BOARD_STATUS    0xf1
#define CMD_GET_FW_SUM          0xf2
#define CMD_GET_PROTOCOL_VER    0xf3
#define CMD_SET_BOOTMODE        0xfd
#define CMD_FW_UPDATE           0xfe

#define STATUS_OK               1
#define REPORT_OK               1
#define HOST_ADDRESS            1       /* with two boards the host is 1 and each board 2 */
#define BOARD_ADDRESS           2
#define LED_COUNT_MAX           66
#define FRAME_MAX_PAYLOAD       200
#define TX_STUCK_MS             50
#define NEW_SESSION_MS          10000   /* a port opened after so long without any data starts a new session */

/* The identity of the board (the defaults of segatools) */
#define BOARD_NUMBER            "15093-06"
#define CHIP_NUMBER             "6710 "
#define BOOT_CHIP_NUMBER        "6709 "
#define FIRMWARE_VERSION        0x90
#define FIRMWARE_SUM            0xadf7

/* Where the three colors of the air tower are in the data of the LEDs: the board on the left has 53 LEDs and the last three are the
 * left air tower, the board on the right has 63 and the last three are the right one. */
#define TOWER_FIRST_BYTE_LEFT   150
#define TOWER_FIRST_BYTE_RIGHT  180
#define AIR_TOWER_GAIN          4       /* the game gives the colors on a scale that stops at about 61 */

enum { PARSE_SYNC, PARSE_FRAME };

#define PACKET_SIZE 32      /* the bulk packets of the LED boards: the USB SRAM (512 bytes) has no room for more */
#define RX_SIZE     256
#define TX_SIZE     256

#define EP_REGISTER(slot, n)    ((&USBD_BUFSEG0)[4 * (slot) + (n)])         /* n: 0 buffer, 1 payload length */

static const char board_number[8] = BOARD_NUMBER;
static const char chip_number[5] = CHIP_NUMBER;
static const char boot_chip_number[5] = BOOT_CHIP_NUMBER;

/* ---- Bytes to and from the host ------------------------------------------------------------- */

static uint32_t tx_filled(const volatile io4_led_board_t *b)
{
    return (b->tx_head - b->tx_tail) & (TX_SIZE - 1);
}

static void tx_put(volatile io4_led_board_t *b, uint8_t byte)
{
    if (tx_filled(b) < TX_SIZE - 1) {
        b->tx[b->tx_head] = byte;
        b->tx_head = (b->tx_head + 1) & (TX_SIZE - 1);
    }
}

static void tx_escaped(volatile io4_led_board_t *b, uint8_t byte)
{
    if (byte == SYNC || byte == ESCAPE) {
        tx_put(b, ESCAPE);
        byte--;
    }
    tx_put(b, byte);
}

/* The data of the host for board `board`, in the USB interrupt. */
void io4_led_board_out(uint32_t board)
{
    volatile io4_led_board_t *b = &io4.led_board[board];
    uint32_t slot = 8 + 2 * board, buffer = EP_REGISTER(slot, 0), length = EP_REGISTER(slot, 1) & 0x7f, i;

    for (i = 0; i < length; i++) {
        uint16_t next = (b->rx_head + 1) & (RX_SIZE - 1);

        if (next != b->rx_tail) {
            b->rx[b->rx_head] = USB_SRAM[buffer + i];
            b->rx_head = next;
        }
    }
    EP_REGISTER(slot, 1) = PACKET_SIZE;
    b->active_ms = tick_ms;
}

/* SET_CONTROL_LINE_STATE: the host raises DTR when it opens the port. The board keeps its state when the game closes the port (the game
 * opens and closes it several times while it starts), but a game that starts after a pause meets a board as after a power up, as
 * with the real cabinet: what the previous game left (queued answers, "no answers", another address, the boot loader) would fail
 * its start. The main loop does the work. */
void io4_led_board_line_state(uint32_t board, uint16_t value)
{
    volatile io4_led_board_t *b = &io4.led_board[board];

    if (value & 1) {
        b->open_request = (tick_ms - b->active_ms > NEW_SESSION_MS) ? 2 : 1;
        b->active_ms = tick_ms;
    }
}

/* The host took the last packet of board `board`. */
void io4_led_board_in(uint32_t board)
{
    io4.led_board[board].in_ready = 1;
}

/* ---- The board ------------------------------------------------------------------------------ */

/* Answer a command: status, command and report, then `length` bytes of `data`. */
static void respond(volatile io4_led_board_t *b, uint8_t command, const uint8_t *data, uint32_t length)
{
    uint32_t i;
    uint8_t header[3] = { HOST_ADDRESS, b->board_address, (uint8_t)(3 + length) };
    uint8_t sum = 0;

    tx_put(b, SYNC);
    for (i = 0; i < 3; i++) {
        sum += header[i];
        tx_escaped(b, header[i]);
    }
    sum += STATUS_OK + command + REPORT_OK;
    tx_escaped(b, STATUS_OK);
    tx_escaped(b, command);
    tx_escaped(b, REPORT_OK);
    for (i = 0; i < length; i++) {
        sum += data[i];
        tx_escaped(b, data[i]);
    }
    tx_escaped(b, sum);
}

/* The last three LEDs of a board are the colors of the air tower on that side: they are the three zones of the packet of the
 * official firmware (the three groups of eight LEDs). */
static void set_leds(uint32_t board, const uint8_t *data, uint32_t length)
{
    uint32_t first = board == 0 ? TOWER_FIRST_BYTE_LEFT : TOWER_FIRST_BYTE_RIGHT;
    uint32_t i;
    uint8_t colors[9];

    if (touch_state != TOUCH_STATE_RUNNING || length < first + 9) {
        return;
    }
    for (i = 0; i < 9; i++) {
        uint32_t level = data[first + i] * AIR_TOWER_GAIN;

        colors[i] = level > 255 ? 255 : (uint8_t)level;
    }
    led_ext_towers(board, colors);
}

static void handle(uint32_t board, volatile io4_led_board_t *b, const uint8_t *payload, uint32_t length)
{
    uint8_t data[24];
    uint32_t i;
    uint8_t command = payload[0];

    switch (command) {
    case CMD_RESET:
        b->enable_response = 1;
        b->boot_mode = 0;
        respond(b, command, 0, 0);
        break;
    case CMD_SET_TIMEOUT:
        data[0] = length > 1 ? payload[1] : 0;
        data[1] = length > 2 ? payload[2] : 0;
        respond(b, command, data, 2);
        break;
    case CMD_SET_DISABLE_RESPONSE:
        if (length > 1) {
            b->enable_response = !payload[1];
        }
        data[0] = !b->enable_response;
        respond(b, command, data, 1);
        break;
    case CMD_SET_ID:
        if (length > 1 && payload[1] >= 1 && payload[1] <= 8) {
            b->board_address = payload[1];
            respond(b, command, 0, 0);
        }
        break;
    case CMD_CLEAR_ID:
        b->board_address = 0;
        respond(b, command, 0, 0);
        break;
    case CMD_SET_MAX_BRIGHT:
    case CMD_UPDATE_LED:
    case CMD_SET_LED:
    case CMD_SET_FADE_LEVEL:
    case CMD_SET_FADE_SHIFT:
    case CMD_FW_UPDATE:
        if (command == CMD_FW_UPDATE || b->enable_response) {
            respond(b, command, 0, 0);
        }
        break;
    case CMD_SET_IMM_LED:
    case CMD_SET_FADE_LED:
        set_leds(board, payload + 1, length - 1);
        if (b->enable_response) {
            respond(b, command, 0, 0);
        }
        break;
    case CMD_SET_AUTO_SHIFT:
        if (length > 1) {
            b->led_count = payload[1];
        }
        if (b->enable_response) {
            data[0] = b->led_count;
            respond(b, command, data, 1);
        }
        break;
    case CMD_GET_BOARD_INFO:
        for (i = 0; i < 8; i++) {
            data[i] = board_number[i];
        }
        data[8] = 0x0a;
        for (i = 0; i < 5; i++) {
            data[9 + i] = b->boot_mode ? boot_chip_number[i] : chip_number[i];
        }
        data[14] = 0xff;
        data[15] = FIRMWARE_VERSION;
        data[16] = 204;                 /* the size of the receive buffer, 204 whatever the number of LEDs, as a little endian 16 bit number */
        data[17] = 0;
        respond(b, command, data, 18);
        break;
    case CMD_GET_BOARD_STATUS:
        data[0] = 0;
        data[1] = 0;
        data[2] = 0;
        data[3] = 1;                    /* DIPSW 1 on */
        respond(b, command, data, b->boot_mode ? 3 : 4);
        break;
    case CMD_GET_FW_SUM:
        data[0] = FIRMWARE_SUM >> 8;
        data[1] = (uint8_t)FIRMWARE_SUM;
        respond(b, command, data, 2);
        break;
    case CMD_GET_PROTOCOL_VER:
        data[0] = b->boot_mode ? 0 : 1; /* boot mode 1.1, application 1.4 */
        data[1] = 1;
        data[2] = b->boot_mode ? 1 : 4;
        respond(b, command, data, 3);
        break;
    case CMD_SET_BOOTMODE:
        b->boot_mode = 1;
        data[0] = 1;
        respond(b, command, data, 1);
        break;
    default:                            /* the board does not answer what it does not know */
        break;
    }
}

/* One byte of the host. */
static void parse(uint32_t board, volatile io4_led_board_t *b, uint8_t byte)
{
    if (byte == SYNC) {                 /* a frame starts, also in the middle of another one */
        b->parse_state = PARSE_FRAME;
        b->parse_escape = 0;
        b->parse_position = 0;
        b->parse_sum = 0;
        return;
    }
    if (b->parse_state != PARSE_FRAME) {
        return;
    }
    if (byte == ESCAPE) {
        b->parse_escape = 1;
        return;
    }
    if (b->parse_escape) {
        b->parse_escape = 0;
        byte++;
    }
    if (b->parse_position >= sizeof b->frame) {
        b->parse_state = PARSE_SYNC;
        return;
    }
    b->frame[b->parse_position++] = byte;
    if (b->parse_position == 3 && byte > FRAME_MAX_PAYLOAD) {
        b->parse_state = PARSE_SYNC;
        return;
    }
    if (b->parse_position >= 4 && b->parse_position == 4u + b->frame[2]) {     /* the checksum is the last byte */
        uint32_t i;
        uint8_t sum = 0;

        for (i = 0; i < 3u + b->frame[2]; i++) {
            sum += b->frame[i];
        }
        if (sum == byte && b->frame[2] != 0) {
            handle(board, b, (const uint8_t *)&b->frame[3], b->frame[2]);
        }
        b->parse_state = PARSE_SYNC;
    }
}

/* Hand the next bytes to the IN endpoint when the host has taken the last packet. */
static void pump(uint32_t board, volatile io4_led_board_t *b)
{
    uint32_t slot = 9 + 2 * board, count, i;
    volatile uint8_t *buffer;

    if (!b->in_ready) {
        if (tick_ms - b->tx_ms < TX_STUCK_MS) {
            return;
        }
        b->in_ready = 1;
    }
    count = tx_filled(b);
    if (count == 0) {
        if (b->last_tx_length == PACKET_SIZE) {
            b->last_tx_length = 0;
            b->in_ready = 0;
            b->tx_ms = tick_ms;
            EP_REGISTER(slot, 1) = 0;
        }
        return;
    }
    if (count > PACKET_SIZE) {
        count = PACKET_SIZE;
    }
    buffer = &USB_SRAM[EP_REGISTER(slot, 0)];
    for (i = 0; i < count; i++) {
        buffer[i] = b->tx[b->tx_tail];
        b->tx_tail = (b->tx_tail + 1) & (TX_SIZE - 1);
    }
    b->last_tx_length = (uint8_t)count;
    b->in_ready = 0;
    b->tx_ms = tick_ms;
    EP_REGISTER(slot, 1) = count;
}

void io4_led_boards_init(void)
{
    uint32_t i;

    for (i = 0; i < 2; i++) {
        volatile io4_led_board_t *b = &io4.led_board[i];

        b->line_coding[1] = 0xc2;       /* 115200 baud, 8 data bits, no parity, 1 stop bit */
        b->line_coding[2] = 0x01;
        b->line_coding[6] = 8;
        b->board_address = BOARD_ADDRESS;
        b->enable_response = 1;
        b->led_count = LED_COUNT_MAX;
        b->in_ready = 1;
    }
}

/* Main loop task. */
void io4_led_boards_task(void)
{
    uint32_t board;

    for (board = 0; board < 2; board++) {
        volatile io4_led_board_t *b = &io4.led_board[board];

        if (b->open_request) {
            uint8_t request = b->open_request;

            b->open_request = 0;
            b->rx_tail = b->rx_head;                    /* forget what is queued, both ways, and a frame in progress */
            b->tx_tail = b->tx_head;
            b->parse_state = PARSE_SYNC;
            b->parse_escape = 0;
            b->in_ready = 1;
            b->last_tx_length = 0;
            if (request == 2) {
                b->board_address = BOARD_ADDRESS;
                b->enable_response = 1;
                b->boot_mode = 0;
                b->led_count = LED_COUNT_MAX;
            }
        }
        while (b->rx_tail != b->rx_head) {
            uint8_t byte = b->rx[b->rx_tail];

            b->rx_tail = (b->rx_tail + 1) & (RX_SIZE - 1);
            parse(board, b, byte);
        }
        pump(board, b);
    }
}
