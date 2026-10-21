#pragma once
/* What usbd.c defines. */

void USBD_Open(const volatile usbd_info_t *info, usbd_request_fn pfnClassReq, usbd_set_interface_fn pfnSetInterface);
void USBD_Start(void);
void USBD_SetVendorRequest(usbd_request_fn pfnVendorReq);
void USBD_SwReset(void);
void USBD_MemCopy(uint8_t *dest, const uint8_t *src, int n);
void USBD_GetSetupPacket(uint8_t *buf);
void USBD_SetStall(uint32_t epnum);
void USBD_PrepareCtrlIn(const uint8_t *buf, uint32_t size);
void USBD_PrepareCtrlOut(uint8_t *buf, uint32_t size);
void USBD_CtrlIn(void);
void USBD_CtrlOut(void);
void USBD_GetDescriptor(void);
void USBD_StandardRequest(void);
void USBD_ProcessSetupPacket(void);
void USBD_IRQHandler(void);
