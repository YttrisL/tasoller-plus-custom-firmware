#pragma once
/* What usb_device.c defines. */

void HID_ClassRequest(void);
void WinUSB_VendorRequest(void);
void usb_start(void);
void usb_string_descriptor_set(uint8_t *descriptor, const char *text);
void EP2_Handler(void);
void EP3_Handler(void);
void EP4_Handler(void);
void EP5_Handler(void);
void EP6_Handler(void);
void EP7_Handler(void);
void EP8_Handler(void);
void EP9_Handler(void);
void EP10_Handler(void);
void EP11_Handler(void);
