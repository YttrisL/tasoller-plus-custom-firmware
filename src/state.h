#pragma once
/* Firmware state in RAM. The variables live at the addresses the official firmware gave them
 * (nothing else may be placed in RAM, see build-tools/fw.ld), so each one is a macro for an object at a
 * fixed address. */

/* ---- Clocks and time ----------------------------------------------------------------------- */

#define SystemCoreClock         (*(volatile uint32_t *)0x20000000u)     /* Hz */
#define CyclesPerUs             (*(volatile uint32_t *)0x20000004u)
#define PllClock                (*(volatile uint32_t *)0x20000008u)
#define gau32ClkSrcTbl          ((volatile uint32_t *)0x2000000Cu)      /* clock source frequencies */
#define g_clk_timeout_flag      (*(volatile uint32_t *)0x20000FCCu)     /* set by CLK_WaitClockReady on time-out */

/* The millisecond counter of timer 0. tick_ack is cleared by the interrupt and not used. */
#define tick_ms                 (*(volatile uint32_t *)0x2000062Cu)
#define tick_ack                (*(volatile uint32_t *)0x20000630u)

/* The settings in RAM have to be saved when settings_dirty is set; settings_save_delay_ms counts down to it. */
#define settings_dirty          (*(volatile uint8_t *)0x20002921u)
#define settings_save_delay_ms  (*(volatile int32_t *)0x20002922u)

/* ---- USB event flags ------------------------------------------------------------------------- */

#define command_pending         (*(volatile uint8_t *)0x20000620u)      /* a command is in command_request */
#define ep6_ready               (*(volatile uint8_t *)0x20000621u)      /* the stream endpoint can take a packet */
#define ep3_ready               (*(volatile uint8_t *)0x20000622u)      /* the answer endpoint can take a packet */
#define stream_ctl_pending      (*(volatile uint8_t *)0x20000623u)      /* a start/stop is in stream_ctl_buffer */
#define stream_ctl_length       (*(volatile uint8_t *)0x20000624u)

/* The queue of stream packets: head is where the next goes, tail the next to send. stream_mode is 0 when
 * the stream is off. */
#define stream_queue_head       (*(volatile uint16_t *)0x20000634u)
#define stream_queue_tail       (*(volatile uint16_t *)0x20000636u)
#define stream_mode             (*(volatile uint8_t *)0x20000638u)
#define stream_pop_tick         (*(volatile uint32_t *)0x2000063Cu)
#define stream_push_tick        (*(volatile uint32_t *)0x20000640u)

/* ---- Touch start-up ----------------------------------------------------------------------------- */

#define touch_state             (*(volatile uint8_t *)0x200059D7u)      /* TOUCH_STATE_* */
#define touch_start_attempts    (*(volatile uint8_t *)0x20005A28u)
#define touch_start_tick        (*(volatile uint32_t *)0x20005A31u)     /* when the start-up began (not used) */
#define boot_progress_done      (*(volatile uint8_t *)0x20005C37u)      /* the progress bar reached its end */
#define usb_already_started     (*(volatile uint8_t *)0x2001659Eu)      /* a restart of the touch start-up keeps USB up */
#define touch_cap_status        (*(volatile uint8_t *)0x2001659Fu)      /* from the 0xb0 frame, command 0x34 */

/* Positions in the input delay queues (see below). */
#define input_queue_read        (*(volatile uint16_t *)0x20005A2Du)
#define input_queue_write       (*(volatile uint16_t *)0x20005A2Fu)

/* AES state of the device authentication: the S-box and the inverse S-box copied to RAM, and the
 * expanded key (11 round keys of 16 bytes). */
#define aes_sbox                ((volatile uint8_t *)0x20018994u)
#define aes_inv_sbox            ((volatile uint8_t *)0x20018A94u)
#define aes_round_keys          ((volatile uint8_t *)0x20018B94u)

/* ---- Settings ---------------------------------------------------------------------------- */

/* Number of bytes of the settings block: everything from `device_mode` on is kept in flash at
 * SETTINGS_FLASH (one 2 KiB page) and read back at start-up. */
#define SETTINGS_FLASH      0x38000u
#define SETTINGS_SIZE       0x2dbu

/* Number of pads: 16 keys, two touch pads each. They are read by four touch controllers, eight
 * pads each; each pad has four sensor values. */
#define PAD_COUNT           32
#define SENSORS_PER_PAD     4

/* Device modes (settings.device_mode): the controller can present itself as a WinUSB device
 * with the touch data as a stream, or as a keyboard with 4, 8, 16 or 32 keys. */
#define MODE_WINUSB         0
#define MODE_KEYBOARD_4K    1
#define MODE_KEYBOARD_8K    2
#define MODE_KEYBOARD_16K   3
#define MODE_KEYBOARD_32K   4

/* A keyboard mode maps pads and the other inputs to USB key codes: one table of KEY_MAP_SIZE
 * entries per mode, indexed by pad (0..31), then the test and service buttons and the six air
 * beams. */
#define KEY_MAP_SIZE        48
#define KEY_MAP_TEST        0x20
#define KEY_MAP_SERVICE     0x21
#define KEY_MAP_AIR         0x22

typedef struct {
    float tower_brightness;                 /* 0x200165a0: LED brightness of the air towers, 0..0.5 */
    float slider_brightness;                /* 0x200165a4: LED brightness of the slider, 0..1 */
    uint8_t brightness_dirty;               /* 0x200165a8: set when a brightness was changed */

    /* ---- the block saved to flash starts here ---- */
    uint8_t device_mode;                    /* 0x200165a9: MODE_* */
    uint8_t spare[30];                      /* 0x200165aa: only given a default, nothing reads it */
    uint16_t touch_param_bd[4];             /* 0x200165c8: per controller, sent to it with frame 0xbd
                                               (finger capacitance in the setup tool) */
    int16_t input_delay;                    /* 0x200165d0: length of the input delay queue, 0..1024 */
    int16_t option_word;                    /* 0x200165d2: a 16 bit option of the setup tool */
    uint16_t sensor_threshold[PAD_COUNT * SENSORS_PER_PAD];   /* 0x200165d4 */
    uint8_t key_map[5 * KEY_MAP_SIZE];      /* 0x200166d4: mode 5 would be the fields that follow */
    uint16_t global_threshold[4];           /* 0x200167c4: one per touch controller */
    uint16_t pair_table[40];                /* 0x200167cc: two tables of 20, command 0x30 */
    char name[64];                          /* 0x2001681c: device name shown by the setup tool */
    uint16_t touch_param_be;                /* 0x2001685c: sent to the controllers with frame 0xbe */
    uint8_t air_brightness;                 /* 0x2001685e: percent */
    uint8_t slider_brightness_percent;      /* 0x2001685f: percent */
    uint16_t tuning[18];                    /* 0x20016860: command 0x37 */
} settings_t;

#define settings (*(volatile settings_t *)0x200165A0u)

/* The IO4-Emulation mode (MODE_IO4, see io4.c) works like the WinUSB mode for everything but USB. It is not a value of
 * settings.device_mode: that stays MODE_WINUSB, so that the official firmware and tool, which only know modes 0 to 4,
 * read the same settings page as a plain WinUSB device. Supernova keeps its choice in one of the spare bytes, which the official firmware keeps as it
 * is: SNV_MODE_IO4 for the IO4-Emulation mode, SNV_MODE_SEEN when a mode was chosen with Supernova or the official tool.
 * Any other value means the page was never seen by Supernova, which then makes the IO4-Emulation mode the mode (see
 * settings_load). The setup tool sees the mode as MODE_IO4 (5) only when it asks for it with CMD_DEVICE_MODE. */
#define MODE_IO4            5
#define SNV_MODE_BYTE       27
#define SNV_MODE_IO4        0x49
#define SNV_MODE_SEEN       0x53
/* IO4-Emulation only: unless this spare byte holds SNV_LED_EMULATION the two LED boards are not offered (no COM ports for them) and the air towers
 * show one colour, whatever the game does; the slider is not affected. That is the default (a byte that was never set holds something else), and
 * SNV_LED_STATIC is what the setup tool writes for it. The colour is the three bytes from SNV_LED_COLOR once SNV_LED_COLOR_SET holds
 * SNV_LED_COLOR_MARK, white otherwise. */
/* IO4-Emulation: the signals the two function buttons send to the game. SNV_FN_MARKER holds SNV_FN_MARK once the setup tool wrote a choice (the
 * bytes hold the 0x3c of the official firmware before). Then, from SNV_FN_FIRST, five bytes for each of FN1, FN2 and both together: a mask of
 * the 32 buttons of the I/O board (four bytes, little endian) and 1 when the button also adds a credit. Any number of signals can be chosen.
 * Default: FN1 is TEST, FN2 is SERVICE, and nothing for both together (each sends its own). */
#define SNV_FN_MARKER       17
#define SNV_FN_MARK         0x47
#define SNV_FN_FIRST        2
#define SNV_LED_BYTE        26
#define SNV_LED_STATIC      0x53
#define SNV_LED_EMULATION   0x45
#define SNV_LED_COLOR_SET   22
#define SNV_LED_COLOR_MARK  0x43
#define SNV_LED_COLOR       23
#define snv_led_colour(k)   (settings.spare[SNV_LED_COLOR_SET] == SNV_LED_COLOR_MARK ? settings.spare[SNV_LED_COLOR + (k)] : 0xff)
#define settings_io4()      (settings.device_mode == MODE_WINUSB && settings.spare[SNV_MODE_BYTE] == SNV_MODE_IO4)
#define settings_static_light() (settings_io4() && settings.spare[SNV_LED_BYTE] != SNV_LED_EMULATION)

_Static_assert(offsetof(settings_t, device_mode) == 9, "settings layout");
_Static_assert(sizeof(settings_t) - offsetof(settings_t, device_mode) == SETTINGS_SIZE, "settings size");
_Static_assert(offsetof(settings_t, touch_param_bd) == 0x28, "settings layout");
_Static_assert(offsetof(settings_t, sensor_threshold) == 0x34, "settings layout");
_Static_assert(offsetof(settings_t, key_map) == 0x134, "settings layout");
_Static_assert(offsetof(settings_t, global_threshold) == 0x224, "settings layout");
_Static_assert(offsetof(settings_t, name) == 0x27c, "settings layout");
_Static_assert(offsetof(settings_t, tuning) == 0x2c0, "settings layout");

/* The key of mode `mode` for input `index`. Mode 5 is accepted by the commands and reads the
 * fields behind the tables, as in the official firmware. */
#define KEY_MAP(mode, index)  (((volatile uint8_t *)settings.key_map)[(mode) * KEY_MAP_SIZE + (index)])

/* ---- Touch ------------------------------------------------------------------------------- */

/* A float at any alignment (the calibration tables start at odd addresses). */
typedef float __attribute__((packed, aligned(1))) float_unaligned;

/* Calibration of every sensor (PAD_COUNT * SENSORS_PER_PAD entries each), see touch_calibration.c. */
#define touch_bias  ((volatile float_unaligned *)0x200055D7u)
#define touch_gain  ((volatile float_unaligned *)0x200057D7u)

/* The personalities (WinUSB, keyboard) hand out their input report fields through functions of
 * this type: read the field when `write` is 0, else store `value`. winusb_pad[n] reaches pad n of the
 * WinUSB report; personality_select fills the table. */
typedef uint8_t (*pad_accessor_t)(int write, uint8_t value);
#define winusb_pad       ((volatile pad_accessor_t *)0x20016884u)

/* The four touch controllers, numbered in the order the firmware polls them: port 0 is on
 * UART5, port 1 on UART2, port 2 on UART3 and port 3 on UART4. */
#define TOUCH_PORTS         4

/* Bytes received from a touch controller, filled by the UART interrupt and consumed by
 * touch_parse_frame: a ring of 2 KiB, `read` and `write` are indexes into it. */
#define TOUCH_RING_SIZE     0x800
#define TOUCH_RING_MASK     (TOUCH_RING_SIZE - 1)

typedef struct {
    uint16_t read;
    uint16_t write;
    uint8_t data[TOUCH_RING_SIZE];
} touch_ring_t;

#define touch_ring  ((volatile touch_ring_t *)0x2000292Au)

_Static_assert(sizeof(touch_ring_t) == 0x804, "touch ring size");

/* Last decoded frame of every controller, as touch_report_task wants it: 0x80 bytes each, the
 * first byte holds the payload's first byte and, in its high nibble, the "new frame" flag of the
 * port (bit 4 + port). */
#define touch_frame     ((volatile uint8_t (*)[0x80])0x2000493Au)

/* Payloads of the 0xa0, 0xa1 and 0xaa frames, 0x40 bytes per port. */
#define touch_raw_a0    ((volatile uint8_t (*)[0x40])0x200052D7u)
#define touch_raw_a1    ((volatile uint8_t (*)[0x40])0x200053D7u)
#define touch_raw_aa    ((volatile uint8_t (*)[0x40])0x200054D7u)

/* Time (tick_ms) of the last 0xad frame of every port. */
#define touch_last_frame_tick   ((volatile uint32_t *)0x2001ACD8u)

/* Sequence number a 0xbd frame must carry (never changed after start-up). */
#define touch_expected_seq      ((volatile uint8_t *)0x200006F8u)

/* The six bytes of version information a controller sent in its 0xb6 frame, 18 bytes per
 * port (byte 0 is not set by the frames). */
#define touch_info      ((volatile uint8_t (*)[18])0x200059E0u)

/* The controller's name or version string, from its 0xb2 frame. */
#define touch_name      ((volatile uint8_t *)0x20005CB8u)

/* What the touch start-up (touch_startup_task) waits for: bit n is set when port n answered. */
typedef struct {
    uint8_t mode_config_ack;                /* 0xde frame, status 2/0 */
    uint8_t param_bd_ack;                   /* 0xbd frame echoing touch_param_bd[0] */
    uint8_t param_be_ack;                   /* 0xbe frame echoing touch_param_be */
    uint8_t tuning_ack;                     /* 0xb5 frame with the 18 tuning words */
    uint8_t mode_scan_ack;                  /* 0xde frame, status 2/1 */
    uint8_t info_received;                  /* 0xb6 frame */
    uint8_t name_received;                  /* 0xb2 frame */
    uint8_t status_received;                /* 0xb0 frame */
} touch_ack_t;

#define touch_ack   (*(volatile touch_ack_t *)0x200059D8u)

/* Steps of the touch start-up (touch_state). */
#define TOUCH_STATE_DETECT      0           /* reset the controllers and look for them */
#define TOUCH_STATE_CONFIGURE   2           /* send the settings to each controller */
#define TOUCH_STATE_START       3           /* start the scan, then USB */
#define TOUCH_STATE_FADE_OUT    4           /* the slider LEDs fade out ... */
#define TOUCH_STATE_FADE_IN     5           /* ... and in again */
#define TOUCH_STATE_RUNNING     6

/* 0: normal operation; 1: a touch controller is being updated, the frames are bootloader replies. */
#define touch_update_mode       (*(volatile uint8_t *)0x20005A35u)
/* Bit n: the bootloader of port n has sent a complete reply. */
#define touch_update_replies    (*(volatile uint8_t *)0x20005A36u)

/* The packet that is streamed to the host: "DBT", the buttons, the 32 pad values and the time
 * between two frames of each touch controller. The bytes in between are not used. */
typedef struct {
    char tag[3];
    uint8_t buttons;
    uint8_t pad[PAD_COUNT];
    uint8_t reserved[14];
    uint8_t interval[TOUCH_PORTS];
} dbt_packet_t;

#define dbt_packet  (*(volatile dbt_packet_t *)0x20005D18u)

/* Pass-through mode (see bridge_mode_enter). */
#define bridge_mode         (*(volatile uint8_t *)0x200006C4u)
#define bridge_counters     ((volatile uint32_t *)0x2001ACB8u)

/* ---- LEDs -------------------------------------------------------------------------------- */

enum { LED_STRIP_SLIDER, LED_STRIP_TOWER_A, LED_STRIP_TOWER_B, LED_STRIP_AUX, LED_STRIPS };

#define SLIDER_LEDS     48      /* 16 keys of three LEDs */
#define TOWER_LEDS      24      /* six air beams of four LEDs, on each tower */
#define AUX_LEDS        28

typedef struct {
    float hue;                  /* degrees, 0..360 */
    float saturation;           /* 0..1 */
    float value;                /* brightness, 0..1 */
} hsv_t;

/* The bytes the LEDs want: green first. */
typedef struct {
    uint8_t green;
    uint8_t red;
    uint8_t blue;
} rgb_t;

#define led_slider_hsv      ((volatile hsv_t *)0x20018DB8u)
#define led_tower_a_hsv     ((volatile hsv_t *)0x20018FF8u)
#define led_tower_b_hsv     ((volatile hsv_t *)0x20019118u)
#define led_aux_hsv         ((volatile hsv_t *)0x20019238u)
#define led_slider_rgb      ((volatile rgb_t *)0x20018C44u)
#define led_tower_a_rgb     ((volatile rgb_t *)0x20018CD4u)
#define led_tower_b_rgb     ((volatile rgb_t *)0x20018D1Cu)
#define led_aux_rgb         ((volatile rgb_t *)0x20018D64u)

/* A 32 bit integer at any alignment. */
typedef int32_t __attribute__((packed, aligned(1))) int32_unaligned;

/* Per strip: what has to be done with it. led_dirty is 0 (nothing), 1 (the hsv colours changed, convert
 * them) or 2 (a packet from the host arrived). led_pending counts the transmissions still to do and led_busy
 * is set while a DMA transfer of the strip is running. */
#define led_dirty           ((volatile uint8_t *)0x20004B4Au)
#define led_pending         ((volatile int32_unaligned *)0x20004B3Au)
#define led_busy            ((volatile uint8_t *)0x200006B8u)

/* The packet the host sends over the bulk endpoint to set the LEDs, 114 bytes in two transfers:
 * "DL", a type byte, the 31 colours of the slider (from the right, key colour then separator colour,
 * 16 keys of two LEDs and 15 separators) and six tower colours, three for tower A (from the
 * top) and three for tower B. */
typedef struct {
    char tag[2];                /* 'D', 'L' */
    uint8_t type;
    uint8_t slider[31][3];      /* red, green, blue */
    uint8_t tower[6][3];
} led_packet_t;

#define led_packet          (*(volatile led_packet_t *)0x20004B4Eu)
_Static_assert(sizeof(led_packet_t) == 114, "led packet size");

/* Who decides what the LEDs show: LED_LOCAL (the device lights the keys and beams itself), or the host
 * through the packets; LED_HOST_PACKET means a packet has just arrived. After 3 s without a packet the
 * device takes over again. */
#define LED_HOST            0
#define LED_HOST_PACKET     1
#define LED_LOCAL           2
#define led_mode            (*(volatile uint8_t *)0x200052D6u)

/* Whether each slider key / air beam is pressed / interrupted. */
#define key_pressed         ((volatile uint8_t *)0x20005CF8u)
#define air_blocked         ((volatile uint8_t *)0x20005D08u)

/* Time stamps and counters of the LED effects. */
#define slider_fade_out_tick    (*(volatile uint32_t *)0x200006ACu)
#define slider_fade_in_tick     (*(volatile uint32_t *)0x200006B0u)
#define slider_progress_leds    (*(volatile int32_t *)0x200006B4u)
#define led_dma_tick            (*(volatile uint32_t *)0x200006BCu)

/* ---- USB personalities --------------------------------------------------------------------- */

/* The device presents itself in one of two ways, chosen by settings.device_mode when the
 * touch start-up is over (personality_select):
 *   WinUSB    a vendor specific device: a WinUSB interface for the LED packets and the stream, and
 *             HID interfaces with the pads in an input report ("DBT" report);
 *   keyboard  a HID keyboard whose keys are the pads, the buttons and the air beams.
 * A personality is made of the hooks below and of the descriptors it installs. */

/* Called with every frame a touch controller sent: decode it and update the report. */
typedef uint32_t (*touch_decode_fn_t)(const uint8_t *frame, int chip);
/* Interrupt of the service (PC0) and test (PD3) buttons, `pressed` is 1 while the button is held. */
typedef uint32_t (*button_fn_t)(int write, int pressed);
/* The report the host reads next from endpoint 4; `unused` is passed by the caller but never used. */
typedef uint8_t *(*report_in_fn_t)(uint32_t unused, uint16_t *length);
/* Data the host wrote to endpoint 5. */
typedef void (*report_out_fn_t)(uint8_t *data, uint32_t length);

#define touch_decode_hook   (*(volatile touch_decode_fn_t *)0x20000654u)
#define service_button_hook (*(volatile button_fn_t *)0x2000064Cu)
#define test_button_hook    (*(volatile button_fn_t *)0x20000650u)
#define report_in_hook      (*(volatile report_in_fn_t *)0x20000664u)
#define report_out_hook     (*(volatile report_out_fn_t *)0x20000668u)

/* A report with the pads: "DBT", the buttons (bits 0..5 the air beams, bit 6 the service button, bit 7 the
 * test button) and one value per pad. */
typedef struct {
    char tag[3];
    uint8_t buttons;
    uint8_t pad[PAD_COUNT];
} dbt_report_t;

#define BUTTON_SERVICE      0x40
#define BUTTON_TEST         0x80

/* The WinUSB input report as it is now, and the delayed one the host reads when settings.input_delay is
 * not 0 (its buttons are not delayed). */
#define winusb_report           (*(volatile dbt_report_t *)0x200007C4u)
#define winusb_report_delayed   (*(volatile dbt_report_t *)0x200007E8u)

/* The HID keyboard report: report id 2, the modifiers, a reserved byte and 40 keys: 32 pads, the
 * test and service buttons, six air beams. */
typedef struct {
    uint8_t id;
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t key[40];
} keyboard_report_t;

#define keyboard_report         (*(volatile keyboard_report_t *)0x2001AD08u)
#define keyboard_report_length  (*(volatile uint8_t *)0x20000700u)
#define KEYBOARD_REPORT_SIZE    0x2b
#define KEYBOARD_KEY_TEST       32
#define KEYBOARD_KEY_SERVICE    33
#define KEYBOARD_KEY_AIR        34

/* ---- USB descriptors ---------------------------------------------------------------------- */

/* The descriptors are part of the initialised data (RAM) because they are adapted at run time. */
#define usb_hid_report_vendor       ((volatile uint8_t *)0x20000040u)   /* vendor HID report, 0x1f bytes */
#define usb_bos_descriptor          ((volatile uint8_t *)0x20000084u)
#define usb_ms_os_descriptor        ((volatile uint8_t *)0x200000C0u)
#define usb_webusb_url_descriptor   ((volatile uint8_t *)0x20000174u)
#define usb_keyboard_report_descriptor  ((volatile uint8_t *)0x20000704u)   /* 0x41 bytes */
#define usb_keyboard_config_descriptor  ((volatile uint8_t *)0x20000748u)
#define usb_keyboard_device_descriptor  ((volatile uint8_t *)0x200007ACu)
#define usb_winusb_config_descriptor    ((volatile uint8_t *)0x20000DFCu)
#define usb_winusb_device_descriptor    ((volatile uint8_t *)0x20000E5Cu)

#define USB_VENDOR_ID           0x0e8f
#define USB_PRODUCT_WINUSB      0x1231
#define USB_PRODUCT_KEYBOARD_32K  0x1232
#define USB_PRODUCT_KEYBOARD_16K  0x1233
#define USB_PRODUCT_KEYBOARD_8K   0x1234
#define USB_PRODUCT_KEYBOARD_4K   0x1235

/* String descriptor buffers (0x80 bytes): the name of the device, shown as the product and as the
 * serial number. The language descriptor and the name of the interfaces ("Supernova") are
 * in the initialised data as well. */
#define usb_string_device           ((volatile uint8_t *)0x20001024u)
#define usb_string_serial           ((volatile uint8_t *)0x200010A4u)

/* S_USBD_INFO_T of the Nuvoton USB library: what the device answers to a descriptor request. */
typedef struct {
    volatile const uint8_t *gu8DevDesc;
    volatile const uint8_t *gu8ConfigDesc;
    volatile const uint8_t * volatile *gu8StringDesc;
    volatile const uint8_t * volatile *gu8HidReportDesc;
    volatile const uint8_t *gu8BosDesc;
    volatile uint32_t *gu32HidReportSize;
    volatile uint32_t *gu32ConfigHidDescIdx;
} usbd_info_t;

#define usbd_info           (*(volatile usbd_info_t *)0x20000604u)
#define usb_string_table    ((volatile const uint8_t * volatile *)0x20000208u)
#define usb_hid_report_table    ((volatile const uint8_t * volatile *)0x20001124u)
#define usb_hid_report_sizes    ((volatile uint32_t *)0x20001134u)
/* The names of the 28 LEDs of the WinUSB interface: string descriptors "LED_01R", "LED_01G",
 * "LED_01B", ... of 16 bytes each, built from the template that follows. */
#define usb_led_names           ((volatile uint8_t *)0x200008BCu)
#define usb_led_name_template   ((volatile uint8_t *)0x200008ACu)

/* ---- Input delay ---------------------------------------------------------------------------- */

/* The buttons and the pad values at one time. */
typedef struct {
    uint8_t buttons;
    uint8_t pad[PAD_COUNT];
} input_state_t;

/* With settings.input_delay not 0 the inputs are reported that many milliseconds late: every millisecond
 * input_delay_task stores the current state of the stream packet and of the WinUSB report in two queues,
 * and the oldest entry is reported. The queues are rings of INPUT_QUEUE_SIZE entries: input_queue_write
 * is where the next entry goes and input_queue_read the one that is reported. */
#define INPUT_QUEUE_SIZE        0x401
#define stream_delay_queue      ((volatile input_state_t *)0x20005D5Cu)
#define report_delay_queue      ((volatile input_state_t *)0x2000E17Du)

/* ---- USB device stack ---------------------------------------------------------------------- */

/* State of the control endpoint, with the names of Nuvoton's usbd.c. */
typedef void (*usbd_request_fn)(void);
typedef void (*usbd_set_interface_fn)(uint32_t alternate);

#define g_usbd_SetupPacket          ((volatile uint8_t *)0x20000FD8u)       /* the 8 bytes of the request */
#define g_usbd_RemoteWakeupEn       (*(volatile uint8_t *)0x20000FE0u)
#define g_usbd_CtrlInPointer        (*(const uint8_t * volatile *)0x20000FE4u)    /* what is left to send */
#define g_usbd_CtrlOutPointer       (*(uint8_t * volatile *)0x20000FE8u)          /* where the data stage goes */
#define g_usbd_CtrlInSize           (*(volatile uint32_t *)0x20000FECu)
#define g_usbd_CtrlOutSize          (*(volatile uint32_t *)0x20000FF0u)
#define g_usbd_CtrlOutSizeLimit     (*(volatile uint32_t *)0x20000FF4u)
#define g_usbd_UsbAddr              (*(volatile uint32_t *)0x20000FF8u)
#define g_usbd_UsbConfig            (*(volatile uint32_t *)0x20000FFCu)
#define g_usbd_CtrlMaxPktSize       (*(volatile uint32_t *)0x20001000u)
#define g_usbd_UsbAltInterface      (*(volatile uint32_t *)0x20001004u)
#define g_usbd_CtrlInZeroFlag       (*(volatile uint8_t *)0x20001008u)
#define g_usbd_sInfo                (*(volatile usbd_info_t * volatile *)0x2000100Cu)
#define g_usbd_pfnVendorRequest     (*(usbd_request_fn volatile *)0x20001010u)
#define g_usbd_pfnClassRequest      (*(usbd_request_fn volatile *)0x20001014u)
#define g_usbd_pfnSetInterface      (*(usbd_set_interface_fn volatile *)0x20001018u)
#define g_usbd_pfnSetConfigCallback (*(usbd_request_fn volatile *)0x2000101Cu)
#define g_u32EpStallLock            (*(volatile uint32_t *)0x20001020u)

/* The packet buffers of the USB controller. */
#define USB_SRAM                    ((volatile uint8_t *)0x400C0100u)

/* Buffers filled by the endpoint interrupts. See usbd_ep.c for which endpoint is which. */
#define ep5_out_buffer              ((volatile uint8_t *)0x20001144u)
#define stream_ctl_buffer           ((volatile uint8_t *)0x20001224u)       /* 'h' and a command byte */
#define STREAM_QUEUE_LENGTH         64
#define stream_queue                ((volatile uint8_t (*)[0x40])0x20001264u)

/* ---- Command channel and firmware update ---------------------------------------------------- */

/* A command or an answer: 64 bytes. */
typedef struct {
    uint8_t length;
    uint8_t code;
    uint8_t sequence;
    uint8_t data[0x3c];
    uint8_t checksum;
} command_packet_t;

#define command_request     (*(volatile command_packet_t *)0x20001204u)
#define command_reply       (*(volatile command_packet_t *)0x20016944u)

/* The key every device starts with, and the key in use (see command.c). */
#define aes_default_key     ((const uint8_t *)0x20000688u)
#define aes_key             ((volatile uint8_t *)0x20018984u)

/* Size of the firmware image being written to slot B. */
#define update_size         (*(volatile uint32_t *)0x20000698u)

/* Pieces of a message for the touch bootloaders, collected by CMD_TOUCH_UPDATE_DATA. */
#define touch_update_buffer ((volatile uint8_t *)0x20005A37u)

/* The hardware information string, from the flash (hardware_info_load). */
#define hardware_info       ((volatile uint8_t *)0x20005C78u)

/* The boot snapshot of the chip dump (dump.c): the registers as the bootloader left them, 0x570 bytes. It is above the
 * stack, in RAM that the stock firmware does not use and the start-up code does not clear. */
#define boot_snapshot       ((volatile uint32_t *)0x20020000u)

/* Colours of the individual tower LEDs (led_ext.c): 2 towers of 24 LEDs, red green blue, counted from the bottom, and the
 * time until which they are in use. */
#define led_ext_raw         ((volatile uint8_t *)0x20021400u)
#define led_ext_until       (*(volatile uint32_t *)0x20021500u)
/* When the static light of the towers is next laid down again (led_ext.c), 0 for as soon as possible. */
#define led_static_ms       (*(volatile uint32_t *)0x20021504u)

/* A function the control endpoint calls when the data stage of the request it was prepared for (USBD_PrepareCtrlOut) is
 * complete, or 0. It is called once. */
#define usbd_ctrl_out_done  (*(void (* volatile *)(void))0x20021600u)

/* Where a settings backup is collected before it replaces the settings (settings_transfer.c): SETTINGS_SIZE bytes. */
#define settings_staging    ((volatile uint8_t *)0x20021000u)

#define DEFAULT_THRESHOLD   0x55

/* Milliseconds until the USB device is connected again after CMD_RESTART_USB. */
#define usb_restart_countdown   (*(volatile int32_t *)0x20005D58u)

/* ---- Pins, air beams, buttons ---------------------------------------------------------------- */

/* A pin: its port (the address of the registers), the bit mask in the port and the pin number. */
typedef struct {
    uint32_t port;
    uint32_t mask;
    uint32_t number;
} gpio_pin_t;

/* The six air beams are scanned one at a time: pin PBn lights the emitter of beam n and the
 * receiver is read on air_input_map[n]. The table of the inputs holds the six receivers, then the service
 * (PC0) and the test (PD3) button. */
#define AIR_BEAMS                   6
#define INPUT_PINS                  8
#define INPUT_PIN_SERVICE_BUTTON    6
#define INPUT_PIN_TEST_BUTTON       7
#define air_led_pins        ((volatile gpio_pin_t *)0x20000F00u)
#define input_pins          ((volatile gpio_pin_t *)0x20000F60u)
#define air_input_map       ((volatile uint8_t *)0x20000FC1u)
#define air_scan_index      (*(volatile uint8_t *)0x20000FC0u)
#define air_scan_tick       (*(volatile uint32_t *)0x20000FC8u)

/* Buffers for flash reads (8 KiB) and the bootloader version. */
#define flash_buffer        ((volatile uint8_t *)0x20016984u)
#define bootloader_info     ((volatile uint8_t *)0x20005C38u)

/* The service button: the last state and the taps of a series. */
#define service_button_state    (*(volatile uint8_t *)0x20000680u)
#define service_tap_count       (*(volatile uint8_t *)0x20000681u)
#define service_tap_tick        (*(volatile int32_t *)0x20000684u)
/* Idle detection of the host's LED packets. */
#define idle_counter_ms         (*(volatile uint32_t *)0x20000678u)
#define idle_last_tick          (*(volatile int32_t *)0x2000067Cu)
#define usb_restart_tick        (*(volatile int32_t *)0x2000066Cu)
#define second_usb_level        (*(volatile uint8_t *)0x20000644u)
#define input_delay_tick        (*(volatile int32_t *)0x20000EF4u)

/* The pad accessors of the WinUSB report (personality_winusb.c). */
uint8_t winusb_pad_0(int write, uint8_t value);
uint8_t winusb_pad_1(int write, uint8_t value);
uint8_t winusb_pad_2(int write, uint8_t value);
uint8_t winusb_pad_3(int write, uint8_t value);
uint8_t winusb_pad_4(int write, uint8_t value);
uint8_t winusb_pad_5(int write, uint8_t value);
uint8_t winusb_pad_6(int write, uint8_t value);
uint8_t winusb_pad_7(int write, uint8_t value);
uint8_t winusb_pad_8(int write, uint8_t value);
uint8_t winusb_pad_9(int write, uint8_t value);
uint8_t winusb_pad_10(int write, uint8_t value);
uint8_t winusb_pad_11(int write, uint8_t value);
uint8_t winusb_pad_12(int write, uint8_t value);
uint8_t winusb_pad_13(int write, uint8_t value);
uint8_t winusb_pad_14(int write, uint8_t value);
uint8_t winusb_pad_15(int write, uint8_t value);
uint8_t winusb_pad_16(int write, uint8_t value);
uint8_t winusb_pad_17(int write, uint8_t value);
uint8_t winusb_pad_18(int write, uint8_t value);
uint8_t winusb_pad_19(int write, uint8_t value);
uint8_t winusb_pad_20(int write, uint8_t value);
uint8_t winusb_pad_21(int write, uint8_t value);
uint8_t winusb_pad_22(int write, uint8_t value);
uint8_t winusb_pad_23(int write, uint8_t value);
uint8_t winusb_pad_24(int write, uint8_t value);
uint8_t winusb_pad_25(int write, uint8_t value);
uint8_t winusb_pad_26(int write, uint8_t value);
uint8_t winusb_pad_27(int write, uint8_t value);
uint8_t winusb_pad_28(int write, uint8_t value);
uint8_t winusb_pad_29(int write, uint8_t value);
uint8_t winusb_pad_30(int write, uint8_t value);
uint8_t winusb_pad_31(int write, uint8_t value);

/* The request each PDMA channel is set to (written by PDMA_SetTransferMode, cleared by PDMA_Open). */
#define pdma_request_shadow ((volatile uint8_t *)0x2001AD33u)
