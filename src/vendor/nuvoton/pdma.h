#pragma once
/* What pdma.c defines. */

void PDMA_Open(int pdma,uint32_t channel_mask);
void PDMA_SetBurstType(int pdma,int channel,uint32_t burst_type,uint32_t burst_size);
void PDMA_SetTransferAddr(int pdma,int channel,uint32_t src_addr,uint32_t src_ctrl,uint32_t dst_addr,uint32_t dst_ctrl);
void PDMA_SetTransferCnt(int pdma,int channel,uint32_t data_width,int trans_count);
void PDMA_SetTransferMode(int pdma,int channel,uint32_t peripheral,int scatter_gather,int desc_addr);
