/* The start of the firmware, after the start-up code: set the hardware up and run the tasks. */

#include "ss321.h"

#define DEBUG_BAUD_RATE         1152000
#define TOUCH_BAUD_RATE         921600          /* of the touch controllers */
#define NVIC_PRIGROUP_MASK      0xf8ffu
#define NVIC_PRIGROUP_2         0x5fa0200u      /* the key and two bits of group priority */

/* Put the variables that need it into their initial state. */
void runtime_state_init(void)
{
    int port;

    for (port = 0; port < TOUCH_PORTS; port++) {
        touch_ring[port].read = 0;
        touch_ring[port].write = 0;
        memset((void *)touch_ring[port].data, 0, TOUCH_RING_SIZE);
    }
    touch_state = TOUCH_STATE_DETECT;
    touch_update_mode = 0;
    led_mode = LED_LOCAL;
    led_ext_until = 0;
    usbd_ctrl_out_done = 0;
    dbt_packet.tag[0] = 'D';
    dbt_packet.tag[1] = 'B';
    dbt_packet.tag[2] = 'T';
}

/* Entry point. The tasks are all short and run one after the other; they wait for their time by
 * comparing tick_ms. */
void main(void)
{
    board_clock_and_pin_init();
    UART_Open(&UART1_DAT, DEBUG_BAUD_RATE);
    runtime_state_init();
    settings_load();
    personality_select();
    SCS_AIRCR = (SCS_AIRCR & NVIC_PRIGROUP_MASK) | NVIC_PRIGROUP_2;
    tick_timer_start();
    led_hardware_init();
    board_gpio_init();
    touch_uart_init(TOUCH_BAUD_RATE);
    bootloader_version_scan();
    hardware_info_load();
    for (;;) {
        touch_startup_task();
        led_dma_task();
        input_delay_task();
        led_hsv_to_rgb_task();
        command_task();
        io4_task();
        settings_flush(0);
        stream_control_task();
        touch_report_task();
        touch_uart_task();
        second_usb_task();
        service_button_task();
        air_scan_task();
        idle_timeout_task();
        usb_restart_task();
        led_brightness_task();
    }
}
