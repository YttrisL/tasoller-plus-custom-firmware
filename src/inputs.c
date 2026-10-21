/* The service (PC0) and test (PD3) buttons, and the air beams. */

#include "ss321.h"

#define TAP_WINDOW_MS       500         /* the longest pause inside a series of taps */

/* Interrupt of the service button: pass its state to the personality. The pin is low when the button
 * is pressed. */
void GPC_IRQHandler(void)
{
    volatile gpio_pin_t *pin = &input_pins[INPUT_PIN_SERVICE_BUTTON];

    if ((*(volatile uint32_t *)(pin->port + 0x20) & pin->mask) == 0) {      /* GPIO_INTSRC */
        GPIO_PC_INTSRC = GPIO_PC_INTSRC;
        debug_printf(MSG_UNEXPECTED_IRQ_C);
    } else {
        *(volatile uint32_t *)(pin->port + 0x20) = pin->mask;
        service_button_hook(1, (GPIO_PC0_PDIO ^ 1) & 0xffff);
    }
}

/* Interrupt of the test button. */
void GPD_IRQHandler(void)
{
    volatile gpio_pin_t *pin = &input_pins[INPUT_PIN_TEST_BUTTON];

    if ((*(volatile uint32_t *)(pin->port + 0x20) & pin->mask) == 0) {
        GPIO_PD_INTSRC = GPIO_PD_INTSRC;
        debug_printf(MSG_UNEXPECTED_IRQ_D);
    } else {
        *(volatile uint32_t *)(pin->port + 0x20) = pin->mask;
        test_button_hook(1, (GPIO_PD3_PDIO ^ 1) & 0xffff);
    }
}

/* The other pins have no interrupt: if one comes it is reported on the debug port. */
void GPA_IRQHandler(void)
{
    GPIO_PA_INTSRC = GPIO_PA_INTSRC;
    debug_printf(MSG_UNEXPECTED_IRQ_A);
}

void GPB_IRQHandler(void)
{
    GPIO_PB_INTSRC = GPIO_PB_INTSRC;
    debug_printf(MSG_UNEXPECTED_IRQ_B);
}

void GPF_IRQHandler(void)
{
    GPIO_PF_INTSRC = GPIO_PF_INTSRC;
    debug_printf(MSG_UNEXPECTED_IRQ_F);
}

/* Main loop task: the service button as a switch for the touch start-up. Releasing the button counts
 * a tap; when no tap follows for half a second, three taps start the touch start-up again from the
 * beginning and two send the settings to the controllers again. */
void service_button_task(void)
{
    uint8_t pressed = dbt_packet.buttons & BUTTON_SERVICE;

    if (pressed != service_button_state) {
        service_button_state = pressed;
        if (service_button_state == 0) {
            if (service_tap_count == 0) {
                service_tap_count = 1;
            } else if (tick_ms - service_tap_tick < TAP_WINDOW_MS) {
                service_tap_count = service_tap_count + 1;
            } else {
                service_tap_count = 0;
            }
            service_tap_tick = tick_ms;
        }
    }
    if (service_tap_count != 0 && tick_ms - service_tap_tick > TAP_WINDOW_MS) {
        if (service_tap_count == 3) {
            touch_state = TOUCH_STATE_DETECT;
            slider_fill(0.0f, 1.0f, 0.0f);
            usb_already_started = 1;
            touch_start_attempts = 0;
        } else if (service_tap_count == 2) {
            touch_state = TOUCH_STATE_CONFIGURE;
            slider_fill(120.0f, 1.0f, 0.0f);
            usb_already_started = 1;
            touch_start_attempts = 0;
        }
        service_tap_count = 0;
    }
}

/* ---- Air beams ----------------------------------------------------------------------------------- */

/* The emitters of the six beams are lit one after the other, one for each millisecond that
 * is even: air_sample reads the receiver of the beam that is lit, air_scan_next_line lights the next. */

/* Pass the state of the beam that is lit on: to the tower LEDs and to the personality. */
void air_sample(void)
{
    uint32_t signal = (&GPIO_PB0_PDIO)[air_input_map[air_scan_index]];

    air_led_set(air_scan_index, signal & 0xff);
    winusb_air_beam(air_scan_index, signal & 0xffff);
    keyboard_air_key(1, signal & 0xffff, air_scan_index);
}

/* Light the emitter of the next beam (the pins are low when lit). */
void air_scan_next_line(void)
{
    GPIO_PB0_PDIO = 1;
    GPIO_PB1_PDIO = 1;
    GPIO_PB2_PDIO = 1;
    GPIO_PB3_PDIO = 1;
    GPIO_PB4_PDIO = 1;
    GPIO_PB5_PDIO = 1;
    air_scan_index = air_scan_index + 1;
    if (air_scan_index == AIR_BEAMS) {
        air_scan_index = 0;
    }
    (&GPIO_PB0_PDIO)[air_scan_index] = 0;
}

/* Main loop task: scan one beam every second millisecond. */
void air_scan_task(void)
{
    if (air_scan_tick != tick_ms && (tick_ms & 1) == 0) {
        air_sample();
        air_scan_next_line();
        air_scan_tick = tick_ms;
    }
}
