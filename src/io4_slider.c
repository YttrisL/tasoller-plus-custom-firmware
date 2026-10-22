/* The slider of the IO4-Emulation personality: the serial protocol of Chunithm's slider board over the CDC interface. The
 * game polls the pressure of the 32 cells and sets the slider LEDs. Based on the protocol described in
 * docs/ of host-aprom.
 *
 * A packet is  FF  command  length  data ...  checksum, where the checksum makes the sum of all the bytes 0 (mod 256). The byte FF
 * starts a packet and FD is the escape: inside a packet FF is sent as FD FE and FD as FD FC. */

#include "ss321.h"

#define SYNC                    0xff
#define MARK                    0xfd
#define MARKED_SYNC             0xfe
#define MARKED_MARK             0xfc

/* Commands of the game */
#define CMD_REPORT              0x01        /* one report of the pressure */
#define CMD_LED                 0x02        /* brightness and the colours of the LEDs */
#define CMD_REPORT_ENABLE       0x03        /* a report every 15 ms */
#define CMD_REPORT_DISABLE      0x04
#define CMD_LED_REPORT          0x05        /* the LEDs, and a report as the answer */
#define CMD_RESET               0x10
#define CMD_CPU_STATUS          0xe0
#define CMD_HARDWARE_INFO       0xf0
#define CMD_EXCEPTION           0xee        /* the answer to what is not understood */

#define EXCEPTION_CHECKSUM      1
#define EXCEPTION_BUS_ERROR     2

#define LED_COMMAND_LENGTH      97          /* the brightness and 32 colours of blue, red, green */
#define SLIDER_LEDS_LOGICAL     31          /* 16 cells and 15 dividers; the 32nd colour is not used */
#define REPORT_PERIOD_MS        15          /* the real board is at 15.4 ms */
#define TX_STUCK_MS             50          /* give up waiting for the host to take a packet */

enum { PARSE_SYNC, PARSE_COMMAND, PARSE_LENGTH, PARSE_DATA, PARSE_CHECKSUM };

/* What the board answers to the hardware information request: the model of Chunithm's slider board. */
static const uint8_t slider_hardware_info[18] = {
    '1', '5', '3', '3', '0', ' ', ' ', ' ', 0xa0, '0', '6', '7', '1', '2', 0xff, 0x90, 0, 0,
};

/* ---- Bytes to and from the host ------------------------------------------------------------- */

#define RX_SIZE     256
#define TX_SIZE     512

static uint32_t tx_filled(void)
{
    return (io4.tx_head - io4.tx_tail) & (TX_SIZE - 1);
}

static void tx_put(uint8_t byte)
{
    if (tx_filled() < TX_SIZE - 1) {
        io4.tx[io4.tx_head] = byte;
        io4.tx_head = (io4.tx_head + 1) & (TX_SIZE - 1);
    }
}

/* Bytes of the host, from the USB interrupt (the data of the bulk OUT endpoint). */
void io4_cdc_out(uint8_t *data, uint32_t length)
{
    uint32_t i;

    for (i = 0; i < length; i++) {
        uint16_t next = (io4.rx_head + 1) & (RX_SIZE - 1);

        if (next != io4.rx_tail) {
            io4.rx[io4.rx_head] = data[i];
            io4.rx_head = next;
        }
    }
}

/* One byte of a packet, escaped when it has to be. */
static void put_escaped(uint8_t byte)
{
    if (byte == SYNC || byte == MARK) {
        tx_put(MARK);
        byte--;
    }
    tx_put(byte);
}

static void respond(uint8_t command, const uint8_t *data, uint32_t length)
{
    uint8_t sum = SYNC + command + length;
    uint32_t i;

    tx_put(SYNC);
    put_escaped(command);
    put_escaped((uint8_t)length);
    for (i = 0; i < length; i++) {
        sum += data[i];
        put_escaped(data[i]);
    }
    put_escaped((uint8_t)-sum);
}

static void exception(uint8_t command, uint8_t error)
{
    uint8_t data[2] = { command, error };

    respond(CMD_EXCEPTION, data, 2);
}

/* ---- The slider ----------------------------------------------------------------------------- */

/* The report: the pressure of the 32 cells, 0 to 255. The stream of the official firmware has them the other way round (the
 * game of the official software turns them), so a touch on the right is on the left in the game without this. */
static void send_report(void)
{
    uint8_t pressure[32];
    uint32_t i;

    for (i = 0; i < 32; i++) {
        pressure[i] = dbt_packet.pad[31 - i];
    }
    respond(0x01, pressure, sizeof pressure);
}

/* The LEDs of the slider: through the packet of the official firmware, which dims the colours and decides when the device
 * lights the slider by itself again (no packet for 3 s). The game counts the 31 LEDs from the right, like the packet
 * (the first color is the rightmost LED). */
static void set_leds(const uint8_t *data)
{
    uint32_t k;

    if (touch_state != TOUCH_STATE_RUNNING) {
        return;
    }
    led_packet.tag[0] = 'D';
    led_packet.tag[1] = 'L';
    led_packet.type = 2;                /* the tower colors in the packet are the ones of the side device LED boards, see io4_led_board.c */
    for (k = 0; k < SLIDER_LEDS_LOGICAL; k++) {
        const uint8_t *colour = &data[1 + 3 * k];   /* blue, red, green */

        led_packet.slider[k][0] = colour[1];
        led_packet.slider[k][1] = colour[2];
        led_packet.slider[k][2] = colour[0];
    }
    led_dirty[LED_STRIP_SLIDER] = 2;
    led_mode = LED_HOST_PACKET;
}

static void process(uint8_t command, const uint8_t *data, uint32_t length)
{
    uint8_t status[2];

    switch (command) {
    case CMD_RESET:
        io4.auto_report = 0;
        respond(CMD_RESET, 0, 0);
        break;
    case CMD_HARDWARE_INFO:
        respond(CMD_HARDWARE_INFO, slider_hardware_info, sizeof slider_hardware_info);
        break;
    case CMD_CPU_STATUS:
        status[0] = 0x01;           /* interrupts on, no reset since the start-up */
        status[1] = 0x30;           /* the "ECO exists" bits of the real board, and not "slow IMO" (host-aprom) */
        respond(CMD_CPU_STATUS, status, sizeof status);
        break;
    case CMD_LED:
    case CMD_LED_REPORT:
        if (length == LED_COMMAND_LENGTH) {
            set_leds(data);
        }
        if (command == CMD_LED_REPORT) {
            send_report();
        }
        break;
    case CMD_REPORT:
        send_report();
        break;
    case CMD_REPORT_ENABLE:
        io4.auto_report = 1;
        io4.auto_ms = tick_ms;
        break;
    case CMD_REPORT_DISABLE:
        io4.auto_report = 0;
        io4.tx_head = io4.tx_tail;      /* what was queued is of no use any more */
        respond(CMD_REPORT_DISABLE, 0, 0);
        break;
    default:
        exception(command, EXCEPTION_BUS_ERROR);
        break;
    }
}

/* One byte of the host. */
static void parse(uint8_t byte)
{
    if (byte == MARK) {
        io4.parse_mark = 1;
        return;
    }
    if (io4.parse_mark) {
        io4.parse_mark = 0;
        if (byte == MARKED_SYNC || byte == MARKED_MARK) {
            byte++;
        }
    }
    io4.parse_sum += byte;
    switch (io4.parse_state) {
    case PARSE_SYNC:
        io4.parse_sum = SYNC;
        if (byte == SYNC) {
            io4.parse_state = PARSE_COMMAND;
        }
        break;
    case PARSE_COMMAND:
        io4.parse_command = byte;
        io4.parse_state = PARSE_LENGTH;
        break;
    case PARSE_LENGTH:
        io4.parse_length = byte;
        io4.parse_read = 0;
        io4.parse_state = byte == 0 ? PARSE_CHECKSUM : PARSE_DATA;
        if (byte > sizeof io4.packet) {
            io4.parse_state = PARSE_SYNC;
        }
        break;
    case PARSE_DATA:
        io4.packet[io4.parse_read++] = byte;
        if (io4.parse_read == io4.parse_length) {
            io4.parse_state = PARSE_CHECKSUM;
        }
        break;
    case PARSE_CHECKSUM:
        if (io4.parse_sum == 0) {
            process(io4.parse_command, (const uint8_t *)io4.packet, io4.parse_length);
        } else {
            exception(io4.parse_command, EXCEPTION_CHECKSUM);
        }
        io4.parse_state = PARSE_SYNC;
        break;
    }
}

/* Hand the next bytes to the endpoint when the host has taken the last packet. A packet that fills the endpoint exactly is
 * followed by an empty one, which tells the host that the transfer is over. */
static void pump(void)
{
    uint32_t count, i;
    volatile uint8_t *buffer;

    if (!ep6_ready) {
        if (tick_ms - io4.tx_ms < TX_STUCK_MS) {
            return;
        }
        ep6_ready = 1;                  /* the host never took it (unplugged, reset): carry on */
    }
    count = tx_filled();
    if (count == 0) {
        if (io4.last_tx_length == 64) {
            io4.last_tx_length = 0;
            ep6_ready = 0;
            io4.tx_ms = tick_ms;
            USBD_MXPLD6 = 0;
        }
        return;
    }
    if (count > 64) {
        count = 64;
    }
    buffer = &USB_SRAM[USBD_BUFSEG6];
    for (i = 0; i < count; i++) {
        buffer[i] = io4.tx[io4.tx_tail];
        io4.tx_tail = (io4.tx_tail + 1) & (TX_SIZE - 1);
    }
    io4.last_tx_length = (uint8_t)count;
    ep6_ready = 0;
    io4.tx_ms = tick_ms;
    USBD_MXPLD6 = count;
}

void io4_slider_init(void)
{
    io4.line_coding[0] = 0x00;          /* 115200 baud, 8 data bits, no parity, 1 stop bit */
    io4.line_coding[1] = 0xc2;
    io4.line_coding[2] = 0x01;
    io4.line_coding[3] = 0x00;
    io4.line_coding[6] = 8;
    report_in_hook = (report_in_fn_t)io4_report_in;
    report_out_hook = (report_out_fn_t)io4_cdc_out;
    ep6_ready = 1;
}

/* Main loop task. */
void io4_slider_task(void)
{
    while (io4.rx_tail != io4.rx_head) {
        uint8_t byte = io4.rx[io4.rx_tail];

        io4.rx_tail = (io4.rx_tail + 1) & (RX_SIZE - 1);
        parse(byte);
    }
    if (io4.auto_report && tick_ms - io4.auto_ms >= REPORT_PERIOD_MS) {
        io4.auto_ms = tick_ms;
        if (tx_filled() < TX_SIZE - 80) {
            send_report();
        }
    }
    pump();
}
