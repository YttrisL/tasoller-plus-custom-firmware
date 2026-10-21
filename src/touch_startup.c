/* Start-up of the touch controllers, run from the main loop (touch_startup_task), and the helpers
 * that wait for them. While waiting the other tasks that must keep running are called. */

#include "ss321.h"

/* Frames are [header][length][data...][checksum]; the checksum is the sum of the bytes before it. */
#define CMD_RESET           0xde        /* [de][2][mode][value] */
#define CMD_QUERY           0xb6        /* [b6][2][0][what] */
#define CMD_PARAM_BD        0xbd        /* [bd][2][high][low] */
#define CMD_PARAM_BE        0xbe        /* [be][2][high][low] */
#define CMD_TUNING          0xb5        /* [b5][0x24][18 words][checksum] */

#define QUERY_INFO          0x03
#define QUERY_NAME          0x0b
#define QUERY_STATUS        0x09

#define MODE_CONFIG         2
#define RESET_ALL           3

/* How many times the start-up may fail before it gives up on the controllers. */
#define MAX_ATTEMPTS        2

/* Wait `ms` milliseconds, keeping the touch frames, the LEDs and the colour conversion going. */
void delay_ms(uint32_t ms)
{
    uint32_t start = tick_ms;

    while (tick_ms - start < ms) {
        touch_uart_task();
        led_dma_task();
        led_hsv_to_rgb_task();
    }
}

/* Wait up to `timeout` ms for *flag to become `expected`. Returns whether it did. */
bool wait_for_flag(uint32_t timeout, volatile uint8_t *flag, uint32_t expected)
{
    uint32_t start = tick_ms;

    while (tick_ms - start < timeout && *flag != expected) {
        touch_uart_task();
        led_dma_task();
        led_hsv_to_rgb_task();
    }
    return *flag == expected;
}

/* Wait up to 4 s until every controller of `ports` (a bit mask) has sent its banner, showing the
 * progress on the slider. Returns 1 when they all did, else 0. */
uint32_t wait_for_touch_boot(uint32_t ports)
{
    uint32_t start = tick_ms;
    uint32_t seen = 0;
    uint32_t count = 0;
    uint32_t port;

    do {
        if (tick_ms - start >= 4000) {
            return 0;
        }
        for (port = 0; port < TOUCH_PORTS; port = (port + 1) & 0xff) {
            uint32_t bit;

            if ((1 << port & seen) == 0) {
                bit = touch_detect_boot_banner(port);
                if (bit != 0) {
                    seen |= bit;
                    count = (count + 1) & 0xff;
                }
            }
            if (boot_progress_done == 0) {
                slider_progress((int)(count * 25) >> 2);
                led_dma_task();
                led_hsv_to_rgb_task();
            }
        }
    } while (ports != seen);
    return 1;
}

/* A frame [header][2][a][b][checksum] for `header`. */
static void make_short_frame(uint8_t *frame, uint8_t header, uint8_t a, uint8_t b)
{
    frame[0] = header;
    frame[1] = 2;
    frame[2] = a;
    frame[3] = b;
    frame[4] = header + 2 + a + b;
}

/* The tuning frame: the 18 tuning words of the settings. */
static void make_tuning_frame(uint8_t *frame)
{
    uint32_t i;

    frame[0] = CMD_TUNING;
    frame[1] = 0x24;
    for (i = 0; i < 18; i++) {
        frame[2 + i * 2] = (uint8_t)(settings.tuning[i] >> 8);
        frame[3 + i * 2] = (uint8_t)settings.tuning[i];
    }
    frame[0x26] = 0;
    for (i = 0; i < 0x26; i++) {
        frame[0x26] += frame[i];
    }
}

/* Detect the controllers: reset them through the reset line, wait for their banners, then ask each
 * for its information, name and status. */
static void detect_controllers(void)
{
    uint8_t frame[5];

    if (touch_start_attempts >= MAX_ATTEMPTS) {
        /* give up on the controllers: go on with USB and the lights */
        touch_state = TOUCH_STATE_FADE_OUT;
        if (usb_already_started == 0) {
            usb_start();
        }
        usb_already_started = 0;
        return;
    }
    make_short_frame(frame, CMD_RESET, RESET_ALL, 0);
    touch_rx_flush();
    GPIO_PC5_PDIO = 0;                      /* hold the controllers in reset */
    delay_ms(200);
    touch_rx_flush();
    touch_send_all(frame, 5);
    GPIO_PC5_PDIO = 1;
    if (wait_for_touch_boot(0xf) != 1) {
        touch_start_attempts++;
    }
    delay_ms(100);

    touch_rx_flush();
    touch_ack.info_received = 0;
    make_short_frame(frame, CMD_QUERY, 0, QUERY_INFO);
    touch_send_all(frame, 5);
    touch_state = TOUCH_STATE_CONFIGURE;
    if (wait_for_flag(2000, &touch_ack.info_received, 0xf) != 1) {
        touch_start_attempts++;
        touch_state = TOUCH_STATE_DETECT;
        return;
    }
    delay_ms(10);

    touch_rx_flush();
    touch_ack.name_received = 0;
    make_short_frame(frame, CMD_QUERY, 0, QUERY_NAME);
    touch_send_all(frame, 5);
    touch_state = TOUCH_STATE_CONFIGURE;
    if (wait_for_flag(1000, &touch_ack.name_received, 0xf) == 1) {
        delay_ms(10);
    }

    touch_rx_flush();
    touch_ack.status_received = 0;
    make_short_frame(frame, CMD_QUERY, 0, QUERY_STATUS);
    touch_send_all(frame, 5);
    if (wait_for_flag(2000, &touch_ack.status_received, 0xf) == 1) {
        delay_ms(10);
    } else {
        touch_start_attempts++;
        touch_state = TOUCH_STATE_DETECT;
    }
}

/* Failure while configuring: go back to detection. */
static void configure_failed(void)
{
    touch_state = TOUCH_STATE_DETECT;
    touch_start_attempts++;
}

/* Send the settings to each controller in turn. */
static void configure_controllers(void)
{
    uint8_t frame[5];
    uint8_t tuning[0x27];
    uint32_t port;

    touch_state = TOUCH_STATE_START;
    touch_ack.mode_config_ack = 0;
    make_short_frame(frame, CMD_RESET, MODE_CONFIG, 0);
    touch_send_all(frame, 5);
    if (wait_for_flag(500, &touch_ack.mode_config_ack, 0xf) != 1) {
        touch_start_attempts++;
        touch_state = TOUCH_STATE_DETECT;
        return;
    }
    delay_ms(10);
    slider_progress(0x1e);

    for (port = 0; port < TOUCH_PORTS; port = (port + 1) & 0xff) {
        uint8_t bit = 1 << port;

        /* parameter bd: one value is kept (touch_param_bd[0], the only one the setup tool can set) and sent to
         * every controller */
        make_short_frame(frame, CMD_PARAM_BD, settings.touch_param_bd[0] >> 8, (uint8_t)settings.touch_param_bd[0]);
        touch_ack.param_bd_ack = 0;
        touch_rx_flush();
        touch_send_to(port, frame, 5);
        delay_ms(10);
        if (wait_for_flag(3000, &touch_ack.param_bd_ack, bit) != 1) {
            configure_failed();
            return;
        }
        slider_progress(port * 6 + 0x23);

        /* the tuning words */
        make_tuning_frame(tuning);
        touch_ack.tuning_ack = 0;
        touch_rx_flush();
        touch_send_to(port, tuning, 0x27);
        delay_ms(10);
        if (wait_for_flag(3000, &touch_ack.tuning_ack, bit) != 1) {
            configure_failed();
            return;
        }

        /* parameter be */
        make_short_frame(frame, CMD_PARAM_BE, settings.touch_param_be >> 8, (uint8_t)settings.touch_param_be);
        touch_ack.param_be_ack = 0;
        touch_rx_flush();
        touch_send_to(port, frame, 5);
        delay_ms(10);
        if (wait_for_flag(3000, &touch_ack.param_be_ack, bit) != 1) {
            configure_failed();
            return;
        }

        /* back to the configuration mode */
        touch_ack.mode_config_ack = 0;
        make_short_frame(frame, CMD_RESET, MODE_CONFIG, 0);
        touch_send_to(port, frame, 5);
        if (wait_for_flag(500, &touch_ack.mode_config_ack, bit) != 1) {
            configure_failed();
            return;
        }
        delay_ms(10);
    }
}

/* Start the scan of each controller, twice (the second round is what the official firmware does), then
 * start USB. */
static void start_controllers(void)
{
    uint8_t frame[5];
    uint32_t round, port;

    for (round = 0; round < 2; round++) {
        for (port = 0; port < TOUCH_PORTS; port = (port + 1) & 0xff) {
            make_short_frame(frame, CMD_RESET, MODE_CONFIG, 1);
            touch_state = TOUCH_STATE_FADE_OUT;
            touch_ack.mode_scan_ack = 0;
            touch_send_to(port, frame, 5);
            slider_progress(port * 10 + 0x3c);
            if (wait_for_flag(1000, &touch_ack.mode_scan_ack, 1 << port & 0xff) != 1) {
                configure_failed();
                return;
            }
            delay_ms(0x32);
            if (usb_already_started == 0 && port == 3) {
                usb_start();
                usb_already_started = 0;
                return;
            }
            if (port == 3) {
                slider_progress(100);
                delay_ms(100);
            }
        }
    }
}

/* Main loop task: one step of the start-up of the touch controllers. */
void touch_startup_task(void)
{
    if (touch_start_tick == 0) {
        touch_start_tick = tick_ms;
    }
    switch (touch_state) {
    case TOUCH_STATE_DETECT:
        detect_controllers();
        break;
    case TOUCH_STATE_CONFIGURE:
        configure_controllers();
        break;
    case TOUCH_STATE_START:
        start_controllers();
        break;
    case TOUCH_STATE_FADE_OUT:
        if (slider_fade_out_step() == 1) {
            touch_state = TOUCH_STATE_FADE_IN;
        }
        break;
    case TOUCH_STATE_FADE_IN:
        if (slider_fade_in_step() == 1) {
            touch_state = TOUCH_STATE_RUNNING;
        }
        break;
    case TOUCH_STATE_RUNNING:
        break;
    default:
        touch_state = TOUCH_STATE_RUNNING;
        break;
    }
}
