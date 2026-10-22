#pragma once
/* The IO4-Emulation personality: what io4.c and io4_slider.c define, and the state they share. */

/* One of the two LED boards of the game, the "side devices" left and right (837-15093-06, a framed serial protocol on its own
 * virtual serial port). See io4_led_board.c. */
typedef struct {
    uint8_t line_coding[7];
    uint8_t board_address;          /* 2 when the game starts, the game may change it with SET_ID */
    uint8_t enable_response;
    uint8_t boot_mode;
    uint8_t led_count;
    uint8_t parse_state, parse_escape, parse_position, parse_sum;
    volatile uint8_t in_ready;      /* the host has taken the last packet of the IN endpoint */
    uint8_t last_tx_length;
    uint16_t tx_head, tx_tail;
    volatile uint16_t rx_head, rx_tail;     /* bytes from the host, written by the USB interrupt */
    uint32_t tx_ms;
    uint32_t active_ms;             /* when the host last wrote to the port or opened it */
    volatile uint8_t open_request;  /* the host opened the port: 1 start a new session, 2 as after a power up (handled by the main loop) */
    uint8_t tower[9];               /* the three colors of the air tower the last LED command set (red, green, blue) */
    uint8_t rx[256];
    uint8_t tx[256];
    uint8_t frame[208];             /* destination, source, length, the command and its data, the checksum */
} io4_led_board_t;

/* All the state of the personality is in RAM above the stack (the firmware defines no variables of its own, see
 * build-tools/fw.ld). io4_personality_init clears it. */
typedef struct {
    /* the IO4 HID interface */
    uint8_t report[64];             /* the input report the host reads from endpoint 4 */
    uint8_t out_report[64];         /* a report the host wrote with SET_REPORT on the control endpoint */
    uint8_t out_length;             /* its length; 0 when there is none to handle */
    uint8_t system_status;          /* bits 0..3 the reset reason, 4 the time-out is set, 5 the sampling count is set */
    uint8_t usb_status;             /* bit 2: a time-out occurred */
    uint8_t digital_out[3];         /* the 20 general purpose outputs; the first bit of the first byte is the coin blocker */
    uint8_t pwm_scale;              /* 0..255, of the PWM outputs */
    uint8_t pwm[6];                 /* the PWM outputs: for Chunithm the light gate, blue, red, green on 2, 3 and 4 */
    uint16_t coins[2];
    /* the slider on the serial port (CDC) */
    uint8_t line_coding[7];
    volatile uint16_t rx_head, rx_tail;     /* bytes from the host, written by the USB interrupt */
    uint16_t tx_head, tx_tail;              /* bytes for the host */
    uint8_t rx[256];
    uint8_t tx[512];
    uint8_t parse_state, parse_mark, parse_command, parse_length, parse_read, parse_sum;
    uint8_t packet[0x61];
    uint8_t auto_report;            /* the host asked for a report every 15 ms */
    uint8_t last_tx_length;
    uint32_t auto_ms;
    uint32_t tx_ms;                 /* when the packet in the endpoint was handed over */
    /* string descriptors (UTF-16) that depend on the build */
    uint8_t string_manufacturer[40];
    uint8_t string_product[64];
    uint8_t string_cdc[64];
    uint8_t string_io4[200];
    uint8_t string_led[2][64];
    io4_led_board_t led_board[2];
    uint8_t fn_coin;                /* a function button sends the coin signal at the moment (to add one credit per press) */
} io4_state_t;

#define io4                 (*(volatile io4_state_t *)0x20022000u)
_Static_assert(sizeof(io4_state_t) <= 0x1000, "the IO4 state does not fit in its RAM");

/* The interface numbers of the configuration */
#define IO4_INTERFACE_IO4       0
#define IO4_INTERFACE_COMMAND   1
#define IO4_INTERFACE_CDC       2       /* the slider; the two LED boards follow, with interfaces 4 and 6 */
#define IO4_INTERFACE_LED_L     4
#define IO4_INTERFACE_LED_R     6

#define IO4_MS_OS_SIZE      156     /* the Microsoft OS 2.0 descriptor set: header, configuration, and 3 functions of 46 bytes */
#define IO4_MS_OS_STATIC_SIZE   224 /* with the static light: header, configuration, the slider (46 bytes) and the WinUSB interface (160) */
#define IO4_CONFIG_STATIC_SIZE  148
extern const uint8_t io4_bos_descriptor[33];
extern const uint8_t io4_bos_static_descriptor[33];
extern const uint8_t io4_ms_os_descriptor[IO4_MS_OS_SIZE];
extern const uint8_t io4_ms_os_static_descriptor[IO4_MS_OS_STATIC_SIZE];
void io4_personality_init(void);
uint8_t *io4_report_in(uint32_t unused, uint16_t *length);
void io4_cdc_out(uint8_t *data, uint32_t length);
bool io4_class_request(const uint8_t *setup);
void io4_task(void);
uint32_t io4_buttons_command(const uint8_t *request);

void io4_slider_init(void);
void io4_slider_task(void);
void io4_led_boards_init(void);
void io4_led_boards_task(void);
void io4_led_board_out(uint32_t board);
void io4_led_board_in(uint32_t board);
void io4_led_board_line_state(uint32_t board, uint16_t value);
