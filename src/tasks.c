/* Small tasks of the main loop. */

#include "ss321.h"

#define IDLE_TIMEOUT_MS         3000        /* without a LED packet from the host */
#define USB_ATTACH_ENABLE       1

/* Without LED packets for 3 s (and in the WinUSB mode) the device takes over the lights again: the slider
 * is turned off and its start-up animation plays, then the keys light up when they are touched. */
void idle_timeout_task(void)
{
    if (settings.device_mode != MODE_WINUSB) {
        return;
    }
    if (led_mode == LED_HOST_PACKET) {
        led_mode = LED_HOST;
        idle_counter_ms = 0;
    } else if (led_mode == LED_HOST && touch_state == TOUCH_STATE_RUNNING && idle_last_tick != tick_ms) {
        idle_last_tick = tick_ms;
        idle_counter_ms = idle_counter_ms + 1;
        if (idle_counter_ms > IDLE_TIMEOUT_MS) {
            led_mode = LED_LOCAL;
            touch_state = TOUCH_STATE_FADE_IN;
            slider_off();
        }
    }
}

/* When pin PB6 goes high (the second USB port is powered), and the device is running its own lights, start
 * the slider animation again. */
void second_usb_task(void)
{
    uint32_t level = GPIO_PB6_PDIO;

    if (level != second_usb_level && (second_usb_level = GPIO_PB6_PDIO, second_usb_level == 1) &&
        led_mode == LED_LOCAL && touch_state == TOUCH_STATE_RUNNING) {
        touch_state = TOUCH_STATE_FADE_IN;
        slider_off();
    }
}

/* After CMD_RESTART_USB the device stays off the bus for usb_restart_countdown milliseconds, then starts
 * USB again. */
void usb_restart_task(void)
{
    if (usb_restart_countdown != 0 && usb_restart_tick != tick_ms) {
        usb_restart_tick = tick_ms;
        usb_restart_countdown = usb_restart_countdown - 1;
        if (usb_restart_countdown == 0) {
            usb_start();
        }
    }
}

/* With an input delay (WinUSB mode only) every millisecond the state of the stream packet and of the report
 * go into the delay queues, and the oldest entries become the ones that are reported. */
void input_delay_task(void)
{
    uint32_t slot;

    if (settings.device_mode != MODE_WINUSB || settings.input_delay == 0 || input_delay_tick == tick_ms) {
        return;
    }
    input_delay_tick = tick_ms;
    memcpy((void *)&stream_delay_queue[input_queue_write], (const void *)&dbt_packet.buttons, sizeof(input_state_t));
    slot = input_queue_write;
    input_queue_write = input_queue_write + 1;
    memcpy((void *)&report_delay_queue[slot], (const void *)&winusb_report.buttons, sizeof(input_state_t));
    input_queue_read = input_queue_read + 1;
    if (input_queue_write > INPUT_QUEUE_SIZE - 1) {
        input_queue_write = 0;
    }
    if (input_queue_read > INPUT_QUEUE_SIZE - 1) {
        input_queue_read = 0;
    }
}
