#ifndef PDM_ADC_H
#define PDM_ADC_H
#include <stdint.h>

#define PDM_ADC_RAW_COUNT 17u
#define PDM_ADC_VALUE_COUNT 17u
#define PDM_ADC_FRESH_MS 500u
enum { PDM_ADC_VALID = 0, PDM_ADC_UNAVAILABLE = 1, PDM_ADC_NOMINAL = 2,
       PDM_ADC_INVALID = 3, PDM_ADC_STALE = 4 };
typedef struct { uint16_t vrefint, temp30, temp110; } PDM_ADCCalibration;
typedef struct {
    PDM_ADCCalibration calibration;
    int32_t values[PDM_ADC_VALUE_COUNT];
    uint8_t quality[PDM_ADC_VALUE_COUNT];
    uint32_t captured_ms;
} PDM_ADC;

/* Foreground-only model. Raw order: currents 1..14, battery, VREFINT, die temp.
 * Publish all 17 conversions together; no partially acquired batch is visible. */
void PDM_ADC_Init(PDM_ADC *model, PDM_ADCCalibration calibration);
void PDM_ADC_SubmitRaw(PDM_ADC *model, const uint16_t raw[PDM_ADC_RAW_COUNT], uint32_t now_ms);
void PDM_ADC_Invalidate(PDM_ADC *model, uint8_t quality);
/* Link callback compatible. Invalid values are zeroed but never numeric quality. */
uint8_t PDM_ADC_Read(void *context, uint16_t channel, uint32_t now_ms, int32_t *value);
#endif
