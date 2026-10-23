#include "pdm_adc.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static PDM_ADC model;
static uint16_t raw[17];
static void fixture(void)
{
    PDM_ADC_Init(&model, (PDM_ADCCalibration){1500, 1000, 1300});
    for (unsigned i = 0; i < 14; ++i) raw[i] = 100;
    raw[14] = 3000; raw[15] = 1500; raw[16] = 1000;
}
static void check(uint16_t channel, uint32_t now, uint8_t quality, int32_t expected)
{
    int32_t value = INT32_MAX;
    assert(PDM_ADC_Read(&model, channel, now, &value) == quality);
    assert(value == expected);
}
int main(void)
{
    fixture(); check(100, 0, PDM_ADC_UNAVAILABLE, 0);
    PDM_ADC_SubmitRaw(&model, raw, 100);
    check(100, 100, PDM_ADC_NOMINAL, 11958);
    check(101, 100, PDM_ADC_NOMINAL, 20762);
    check(102, 100, PDM_ADC_VALID, 30000);
    for (uint16_t id = 110; id <= 123; ++id) check(id, 100, PDM_ADC_NOMINAL, 1483);
    check(100, 600, PDM_ADC_NOMINAL, 11958);
    check(100, 601, PDM_ADC_STALE, 0);
    raw[16] = 1300; PDM_ADC_SubmitRaw(&model, raw, 200);
    check(102, 200, PDM_ADC_VALID, 110000);
    raw[15] = 1650; raw[16] = 1430;
    PDM_ADC_SubmitRaw(&model, raw, 300);
    check(100, 300, PDM_ADC_NOMINAL, 10871);
    check(102, 300, PDM_ADC_VALID, 110000);
    fixture(); memset(raw, 0, 15 * sizeof(*raw)); raw[16] = 850;
    PDM_ADC_SubmitRaw(&model, raw, UINT32_MAX - 100);
    check(101, 10, PDM_ADC_NOMINAL, 0); check(100, 10, PDM_ADC_NOMINAL, 0);
    check(102, 10, PDM_ADC_VALID, -10000); check(101, 401, PDM_ADC_STALE, 0);
    /* Threshold is rational before rounding: 2481<2000mV,2482>=2000mV. */
    fixture(); raw[0] = 2481; PDM_ADC_SubmitRaw(&model, raw, 0);
    check(110, 0, PDM_ADC_NOMINAL, 36803);
    raw[0] = 2482; PDM_ADC_SubmitRaw(&model, raw, 0);
    check(110, 0, PDM_ADC_INVALID, 0); check(101, 0, PDM_ADC_INVALID, 0);
    check(111, 0, PDM_ADC_NOMINAL, 1483);
    const uint16_t invalid[] = {4095, 4096, UINT16_MAX};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) {
        for (unsigned slot = 0; slot < 17; ++slot) {
            fixture(); raw[slot] = invalid[i]; PDM_ADC_SubmitRaw(&model, raw, 0);
            check(slot < 14 ? (uint16_t)(110 + slot) : slot == 16 ? 102 : 100, 0, PDM_ADC_INVALID, 0);
        }
    }
    fixture(); raw[15] = 0; PDM_ADC_SubmitRaw(&model, raw, 0);
    check(100, 0, PDM_ADC_INVALID, 0); check(102, 0, PDM_ADC_INVALID, 0);
    const PDM_ADCCalibration bad[] = {{0,1000,1300},{65535,1000,1300},{1,1000,1300}};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(*bad); ++i) {
        fixture(); model.calibration = bad[i]; PDM_ADC_SubmitRaw(&model, raw, 0);
        check(100, 0, PDM_ADC_INVALID, 0);
    }
    const PDM_ADCCalibration bad_temp[] = {{1500,0,1300},{1500,1000,1000},
        {1500,1300,1000},{1500,1000,65535},{1500,1000,1001}};
    for (unsigned i = 0; i < sizeof(bad_temp)/sizeof(*bad_temp); ++i) {
        fixture(); model.calibration = bad_temp[i]; PDM_ADC_SubmitRaw(&model, raw, 0);
        check(102, 0, PDM_ADC_INVALID, 0); check(100, 0, PDM_ADC_NOMINAL, 11958);
    }
    fixture(); raw[16] = 1; PDM_ADC_SubmitRaw(&model, raw, 0); check(102, 0, PDM_ADC_INVALID, 0);
    raw[16] = 2000; PDM_ADC_SubmitRaw(&model, raw, 0); check(102, 0, PDM_ADC_INVALID, 0);
    fixture(); raw[15] = 1000; PDM_ADC_SubmitRaw(&model, raw, 0); check(100, 0, PDM_ADC_INVALID, 0);
    raw[15] = 3000; PDM_ADC_SubmitRaw(&model, raw, 0); check(100, 0, PDM_ADC_INVALID, 0);
    fixture(); PDM_ADC_SubmitRaw(&model, raw, 0);
    PDM_ADC_Invalidate(&model, PDM_ADC_STALE); check(100, 0, PDM_ADC_STALE, 0);
    PDM_ADC_Invalidate(&model, PDM_ADC_VALID); check(100, 0, PDM_ADC_INVALID, 0);
    PDM_ADC_SubmitRaw(&model, NULL, 1); check(100, 1, PDM_ADC_INVALID, 0);
    check(200, 1, PDM_ADC_INVALID, 0);
    int32_t value; assert(PDM_ADC_Read(NULL, 100, 0, &value) == PDM_ADC_UNAVAILABLE);
    assert(PDM_ADC_Read(&model, 100, 0, NULL) == PDM_ADC_INVALID);
    PDM_ADC_Init(NULL, (PDM_ADCCalibration){0}); PDM_ADC_SubmitRaw(NULL, raw, 0);
    PDM_ADC_Invalidate(NULL, 0);
    /* Seeded arbitrary raw/calibration words exercise bounded int64 arithmetic. */
    uint32_t seed = 0x7adce123u;
    for (unsigned n = 0; n < 10000; ++n) {
        fixture();
        for (unsigned i = 0; i < 17; ++i) { seed = seed * 1664525u + 1013904223u; raw[i] = (uint16_t)(seed >> 16); }
        if (n % 2 == 0) { model.calibration.vrefint = (uint16_t)seed; model.calibration.temp30 = (uint16_t)(seed >> 16); }
        PDM_ADC_SubmitRaw(&model, raw, n);
        for (uint16_t id = 100; id <= 123; ++id) {
            uint8_t q = PDM_ADC_Read(&model, id, n, &value);
            assert(q <= PDM_ADC_STALE);
            if (q != PDM_ADC_VALID && q != PDM_ADC_NOMINAL) assert(value == 0);
        }
    }
    puts("ADC conversion: units/calibration/ranges/fault/saturation/age/wrap/10000 seeded batches PASS");
}
