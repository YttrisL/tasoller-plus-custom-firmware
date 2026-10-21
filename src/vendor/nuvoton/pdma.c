/* Peripheral DMA (Nuvoton library). `pdma` is the address of the controller. */

#include "ss321.h"

void PDMA_Open(int pdma,uint32_t channel_mask)
{
  uint32_t channel;
  
  for (channel = 0; channel < 0x10; channel = channel + 1) {
    if ((1 << (channel & 0xff) & channel_mask) != 0) {
      *(volatile uint32_t *)(pdma + channel * 0x10) = 0;
      pdma_request_shadow[channel] = 0;
    }
  }
  *(volatile uint32_t *)(pdma + 0x400) = *(volatile uint32_t *)(pdma + 0x400) | channel_mask;
  return;
}

void PDMA_SetBurstType(int pdma,int channel,uint32_t burst_type,uint32_t burst_size)
{
  *(volatile uint32_t *)(pdma + channel * 0x10) = *(volatile uint32_t *)(pdma + channel * 0x10) & 0xffffff8b;
  *(volatile uint32_t *)(pdma + channel * 0x10) = *(volatile uint32_t *)(pdma + channel * 0x10) | burst_type | burst_size;
  return;
}

void PDMA_SetTransferAddr(int pdma,int channel,uint32_t src_addr,uint32_t src_ctrl,uint32_t dst_addr,uint32_t dst_ctrl
            )
{
  *(volatile uint32_t *)(pdma + channel * 0x10 + 4) = src_addr;
  *(volatile uint32_t *)(pdma + channel * 0x10 + 8) = dst_addr;
  *(volatile uint32_t *)(pdma + channel * 0x10) = *(volatile uint32_t *)(pdma + channel * 0x10) & 0xfffff0ff;
  *(volatile uint32_t *)(pdma + channel * 0x10) = *(volatile uint32_t *)(pdma + channel * 0x10) | src_ctrl | dst_ctrl;
  return;
}

void PDMA_SetTransferCnt(int pdma,int channel,uint32_t data_width,int trans_count)
{
  *(volatile uint32_t *)(pdma + channel * 0x10) = *(volatile uint32_t *)(pdma + channel * 0x10) & 0xcfff;
  *(volatile uint32_t *)(pdma + channel * 0x10) =
       *(volatile uint32_t *)(pdma + channel * 0x10) | data_width | (trans_count + -1) * 0x10000;
  return;
}

void PDMA_SetTransferMode(int pdma,int channel,uint32_t peripheral,int scatter_gather,int desc_addr)
{
  int scatter_base;
  
  pdma_request_shadow[channel] = (uint8_t)peripheral;
  switch(channel) {
  case 0:
    *(volatile uint32_t *)(pdma + 0x480) = *(volatile uint32_t *)(pdma + 0x480) & 0xffffff80 | peripheral;
    break;
  case 1:
    *(volatile uint32_t *)(pdma + 0x480) = *(volatile uint32_t *)(pdma + 0x480) & 0xffff80ff | peripheral << 8;
    break;
  case 2:
    *(volatile uint32_t *)(pdma + 0x480) = *(volatile uint32_t *)(pdma + 0x480) & 0xff80ffff | peripheral << 0x10;
    break;
  case 3:
    *(volatile uint32_t *)(pdma + 0x480) = *(volatile uint32_t *)(pdma + 0x480) & 0x80ffffff | peripheral << 0x18;
    break;
  case 4:
    *(volatile uint32_t *)(pdma + 0x484) = *(volatile uint32_t *)(pdma + 0x484) & 0xffffff80 | peripheral;
    break;
  case 5:
    *(volatile uint32_t *)(pdma + 0x484) = *(volatile uint32_t *)(pdma + 0x484) & 0xffff80ff | peripheral << 8;
    break;
  case 6:
    *(volatile uint32_t *)(pdma + 0x484) = *(volatile uint32_t *)(pdma + 0x484) & 0xff80ffff | peripheral << 0x10;
    break;
  case 7:
    *(volatile uint32_t *)(pdma + 0x484) = *(volatile uint32_t *)(pdma + 0x484) & 0x80ffffff | peripheral << 0x18;
    break;
  case 8:
    *(volatile uint32_t *)(pdma + 0x488) = *(volatile uint32_t *)(pdma + 0x488) & 0xffffff80 | peripheral;
    break;
  case 9:
    *(volatile uint32_t *)(pdma + 0x488) = *(volatile uint32_t *)(pdma + 0x488) & 0xffff80ff | peripheral << 8;
    break;
  case 10:
    *(volatile uint32_t *)(pdma + 0x488) = *(volatile uint32_t *)(pdma + 0x488) & 0xff80ffff | peripheral << 0x10;
    break;
  case 0xb:
    *(volatile uint32_t *)(pdma + 0x488) = *(volatile uint32_t *)(pdma + 0x488) & 0x80ffffff | peripheral << 0x18;
    break;
  case 0xc:
    *(volatile uint32_t *)(pdma + 0x48c) = *(volatile uint32_t *)(pdma + 0x48c) & 0xffffff80 | peripheral;
    break;
  case 0xd:
    *(volatile uint32_t *)(pdma + 0x48c) = *(volatile uint32_t *)(pdma + 0x48c) & 0xffff80ff | peripheral << 8;
    break;
  case 0xe:
    *(volatile uint32_t *)(pdma + 0x48c) = *(volatile uint32_t *)(pdma + 0x48c) & 0xff80ffff | peripheral << 0x10;
    break;
  case 0xf:
    *(volatile uint32_t *)(pdma + 0x48c) = *(volatile uint32_t *)(pdma + 0x48c) & 0x80ffffff | peripheral << 0x18;
  }
  if (scatter_gather == 0) {
    *(volatile uint32_t *)(pdma + channel * 0x10) = (*(volatile uint32_t *)(pdma + channel * 0x10) & 0xfffffffc) + 1;
  }
  else {
    *(volatile uint32_t *)(pdma + channel * 0x10) = (*(volatile uint32_t *)(pdma + channel * 0x10) & 0xfffffffc) + 2;
    scatter_base = PDMA_SCATBA;
    *(volatile int *)(pdma + channel * 0x10 + 0xc) = desc_addr - scatter_base;
  }
  return;
}
