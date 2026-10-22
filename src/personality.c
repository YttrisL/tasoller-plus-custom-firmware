/* Choosing the personality (see state.h): which hooks the rest of the firmware calls. */

#include "ss321.h"

/* Install the hooks and descriptors of the personality that settings.device_mode asks for. */
void personality_select(void)
{
    static const pad_accessor_t winusb_pad_table[PAD_COUNT] = {
        winusb_pad_0,  winusb_pad_1,  winusb_pad_2,  winusb_pad_3,  winusb_pad_4,  winusb_pad_5,
        winusb_pad_6,  winusb_pad_7,  winusb_pad_8,  winusb_pad_9,  winusb_pad_10, winusb_pad_11,
        winusb_pad_12, winusb_pad_13, winusb_pad_14, winusb_pad_15, winusb_pad_16, winusb_pad_17,
        winusb_pad_18, winusb_pad_19, winusb_pad_20, winusb_pad_21, winusb_pad_22, winusb_pad_23,
        winusb_pad_24, winusb_pad_25, winusb_pad_26, winusb_pad_27, winusb_pad_28, winusb_pad_29,
        winusb_pad_30, winusb_pad_31,
    };
    int i;

    for (i = 0; i < PAD_COUNT; i++) {
        winusb_pad[i] = winusb_pad_table[i];
    }
    usbd_info.gu8StringDesc = usb_string_table;         /* the IO4-Emulation personality has strings of its own */
    io4_active = 0;                                     /* the one that runs now stops until the new one is ready */
    io4_static_active = 0;
    if (settings_io4_saved()) {
        touch_decode_hook = winusb_decode_touch;
        service_button_hook = winusb_service_button;
        test_button_hook = winusb_test_button;
        io4_personality_init();
        io4_static_active = settings_static_light_saved();
        io4_active = 1;
    } else if (settings.device_mode == MODE_WINUSB) {
        report_in_hook = (report_in_fn_t)winusb_report_in;
        report_out_hook = (report_out_fn_t)winusb_report_out;
        touch_decode_hook = winusb_decode_touch;
        winusb_personality_init();
        service_button_hook = winusb_service_button;
        test_button_hook = winusb_test_button;
    } else {
        report_in_hook = (report_in_fn_t)keyboard_report_in;
        report_out_hook = (report_out_fn_t)keyboard_report_out;
        touch_decode_hook = keyboard_decode_touch;
        keyboard_personality_init();
        service_button_hook = (button_fn_t)keyboard_service_button;
        test_button_hook = (button_fn_t)keyboard_test_button;
    }
}
