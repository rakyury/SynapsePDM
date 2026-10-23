#include "pdm_adc.h"
#include <stddef.h>
#include <string.h>

/* Nearest integer, ties away from zero. All callers have positive denominator
 * and int64 products bounded by validated 12-bit words and supply limits. */
static int64_t rounded(int64_t n, int64_t d)
{ return n < 0 ? -((-n + d / 2) / d) : (n + d / 2) / d; }

void PDM_ADC_Invalidate(PDM_ADC *m, uint8_t quality)
{
    if (m == NULL) return;
    if (quality != PDM_ADC_UNAVAILABLE && quality != PDM_ADC_STALE) quality = PDM_ADC_INVALID;
    memset(m->values, 0, sizeof(m->values));
    memset(m->quality, quality, sizeof(m->quality));
}

void PDM_ADC_Init(PDM_ADC *m, PDM_ADCCalibration calibration)
{
    if (m == NULL) return;
    memset(m, 0, sizeof(*m));
    m->calibration = calibration;
    PDM_ADC_Invalidate(m, PDM_ADC_UNAVAILABLE);
}

void PDM_ADC_SubmitRaw(PDM_ADC *m, const uint16_t raw[PDM_ADC_RAW_COUNT], uint32_t now_ms)
{
    if (m == NULL) return;
    PDM_ADC next;
    PDM_ADC_Init(&next, m->calibration);
    PDM_ADC_Invalidate(&next, PDM_ADC_INVALID);
    next.captured_ms = now_ms;
    const PDM_ADCCalibration c = m->calibration;
    /* Broad electrical sanity guards, not substitutes for factory calibration:
       VREFINT at 3.3V is about 1500 counts; die calibration is about 1000. */
    if (raw == NULL || c.vrefint < 1000 || c.vrefint > 2000 ||
        raw[15] == 0 || raw[15] >= 4095) { *m = next; return; }
    const int64_t vdda = rounded((int64_t)3300 * c.vrefint, raw[15]);
    if (vdda < 1800 || vdda > 3600) { *m = next; return; }
    if (raw[14] < 4095) {
        /* Legacy 3.9787mV/count at VDDA3294mV, compensated for measured VDDA. */
        const int64_t battery = rounded((int64_t)raw[14] * 39787 * vdda, (int64_t)10000 * 3294);
        if (battery <= 20000) { next.values[0] = (int32_t)battery; next.quality[0] = PDM_ADC_NOMINAL; }
    }
    int32_t total = 0;
    uint8_t total_quality = PDM_ADC_NOMINAL;
    for (unsigned i = 0; i < 14; ++i) {
        const int64_t sense = (int64_t)raw[i] * vdda;
        if (raw[i] >= 4095 || sense >= (int64_t)2000 * 4095) {
            total_quality = PDM_ADC_INVALID;
            continue;
        }
        const int64_t current = rounded(sense * 1840772, (int64_t)4095 * 100000);
        next.values[3 + i] = (int32_t)current;
        next.quality[3 + i] = PDM_ADC_NOMINAL;
        total += (int32_t)current;
    }
    if (total_quality == PDM_ADC_NOMINAL) { next.values[1] = total; next.quality[1] = total_quality; }
    if (c.temp30 >= 500 && c.temp30 <= 2000 && c.temp110 <= 2000 &&
        c.temp110 >= c.temp30 + 50u && c.temp110 <= c.temp30 + 1000u &&
        raw[16] > 0 && raw[16] < 4095) {
        const int64_t delta = (int64_t)raw[16] * vdda - (int64_t)c.temp30 * 3300;
        const int64_t temperature = 30000 + rounded(delta * 80000, (int64_t)3300 * (c.temp110 - c.temp30));
        if (temperature >= -40000 && temperature <= 125000) {
            next.values[2] = (int32_t)temperature; next.quality[2] = PDM_ADC_VALID;
        }
    }
    *m = next;
}

uint8_t PDM_ADC_Read(void *context, uint16_t channel, uint32_t now_ms, int32_t *value)
{
    const PDM_ADC *m = context;
    if (value == NULL) return PDM_ADC_INVALID;
    *value = 0;
    if (m == NULL) return PDM_ADC_UNAVAILABLE;
    unsigned index;
    if (channel >= 100 && channel <= 102) index = channel - 100;
    else if (channel >= 110 && channel <= 123) index = channel - 107;
    else return PDM_ADC_INVALID;
    uint8_t quality = m->quality[index];
    if (quality == PDM_ADC_VALID || quality == PDM_ADC_NOMINAL) {
        if ((uint32_t)(now_ms - m->captured_ms) > PDM_ADC_FRESH_MS) quality = PDM_ADC_STALE;
        else *value = m->values[index];
    }
    return quality;
}
