#ifndef PDM_ADC_HAL_H
#define PDM_ADC_HAL_H
#include <stdbool.h>
#include "pdm_adc.h"
/* ADC1 singleton, foreground-only, no IRQ/DMA or output GPIO access. */
bool PDM_ADC_HAL_Init(PDM_ADC *model, uint32_t now_ms);
void PDM_ADC_HAL_Tick(uint32_t now_ms);
#endif
