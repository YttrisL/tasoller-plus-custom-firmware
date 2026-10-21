#pragma once
/* What inputs.c defines. */

void GPC_IRQHandler(void);
void GPD_IRQHandler(void);
void GPA_IRQHandler(void);
void GPB_IRQHandler(void);
void GPF_IRQHandler(void);
void service_button_task(void);
void air_sample(void);
void air_scan_next_line(void);
void air_scan_task(void);
