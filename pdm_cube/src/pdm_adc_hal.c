#include "pdm_adc_hal.h"
#include "stm32f4xx_hal.h"
#include <string.h>

#ifndef PDM_ADC_FACTORY_READ16
#define PDM_ADC_FACTORY_READ16(address) (*(const volatile uint16_t *)(uintptr_t)(address))
#endif
#define ADC_BATCH_MS 250u
#define ADC_CONVERSION_MS 30u
static ADC_HandleTypeDef adc;
static PDM_ADC *destination;
static uint16_t raw[PDM_ADC_RAW_COUNT];
static uint32_t enabled_ms, batch_ms, conversion_ms;
static unsigned scan_index;
static bool ready, converting;
static const uint32_t channels[PDM_ADC_RAW_COUNT] = {
    0, 1, 2, 3, 4, 5, 6, 9, 8, 7, 13, 12, 11, 10, 14,
    ADC_CHANNEL_VREFINT, ADC_CHANNEL_TEMPSENSOR
};

/* Fail closed until explicit reinitialization; never recycle a partial batch.
 * Disable directly: HAL_Stop is unnecessary and no conversion restart follows. */
static void fail(void)
{
    ready = false;
    converting = false;
    __HAL_ADC_DISABLE(&adc);
    PDM_ADC_Invalidate(destination, PDM_ADC_INVALID);
}

bool PDM_ADC_HAL_Init(PDM_ADC *model, uint32_t now_ms)
{
    ready = false; converting = false; destination = model; scan_index = 0;
    if (model == NULL) return false;
    const PDM_ADCCalibration calibration = {
        PDM_ADC_FACTORY_READ16(0x1FFF7A2Au),
        PDM_ADC_FACTORY_READ16(0x1FFF7A2Cu),
        PDM_ADC_FACTORY_READ16(0x1FFF7A2Eu)
    };
    PDM_ADC_Init(model, calibration);
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_ANALOG; gpio.Pull = GPIO_NOPULL;
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
               GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1; HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4;
    HAL_GPIO_Init(GPIOC, &gpio);

    memset(&adc, 0, sizeof(adc)); adc.Instance = ADC1;
    /* Use actual APB2 clock, conservatively <=18MHz even at VDDA<2.4V. */
    const uint32_t pclk = HAL_RCC_GetPCLK2Freq();
    static const uint32_t dividers[] = {2, 4, 6, 8};
    static const uint32_t prescalers[] = {ADC_CLOCK_SYNC_PCLK_DIV2,
        ADC_CLOCK_SYNC_PCLK_DIV4, ADC_CLOCK_SYNC_PCLK_DIV6, ADC_CLOCK_SYNC_PCLK_DIV8};
    unsigned choice;
    for (choice = 0; choice < 4; ++choice)
        if (pclk >= 600000u * dividers[choice] && pclk <= 18000000u * dividers[choice]) break;
    if (choice == 4) { fail(); return false; }
    adc.Init.ClockPrescaler = prescalers[choice];
    adc.Init.Resolution = ADC_RESOLUTION_12B;
    adc.Init.ScanConvMode = DISABLE;
    adc.Init.ContinuousConvMode = DISABLE;
    adc.Init.DiscontinuousConvMode = DISABLE;
    adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    adc.Init.NbrOfConversion = 1;
    adc.Init.DMAContinuousRequests = DISABLE;
    adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&adc) != HAL_OK) { fail(); return false; }
    /* F446 channel18 multiplexes die temperature and internal VBAT. Keep
       temperature/VREF path on, VBAT off, and wait >=1ms without spinning. */
    ADC->CCR = (ADC->CCR & ~ADC_CCR_VBATE) | ADC_CCR_TSVREFE;
    __HAL_ADC_ENABLE(&adc);
    enabled_ms = now_ms; ready = true;
    return true;
}

static bool start(uint32_t now_ms)
{
    ADC_ChannelConfTypeDef config = {0};
    config.Rank = 1; config.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    /* HAL's TEMPSENSOR branch unconditionally busy-waits, despite the already
       stabilized sensor. Configure VREF through HAL then select rank1 IN18
       explicitly. This also avoids HAL treating bare channel18 as VBAT. */
    config.Channel = scan_index == 16 ? ADC_CHANNEL_VREFINT : channels[scan_index];
    if (HAL_ADC_ConfigChannel(&adc, &config) != HAL_OK) return false;
    if (scan_index == 16) {
        adc.Instance->SMPR1 = (adc.Instance->SMPR1 & ~ADC_SMPR1_SMP18) |
                             (ADC_SAMPLETIME_480CYCLES << 24);
        adc.Instance->SQR3 = (adc.Instance->SQR3 & ~ADC_SQR3_SQ1) | ADC_CHANNEL_18;
    }
    /* ADC stays enabled across conversions, so HAL_Start never executes its
       enable/stabilization spin loop. Unexpected disable fails closed. */
    if ((adc.Instance->CR2 & ADC_CR2_ADON) == 0) return false;
    if (HAL_ADC_Start(&adc) != HAL_OK) return false;
    converting = true; conversion_ms = now_ms;
    return true;
}

void PDM_ADC_HAL_Tick(uint32_t now_ms)
{
    if (!ready || (uint32_t)(now_ms - enabled_ms) < 1u) return;
    /* PollForConversion checks EOC, not OVR. Reject overwritten conversions
       and HAL errors explicitly, before they can be read or cleared by Start. */
    if (__HAL_ADC_GET_FLAG(&adc, ADC_FLAG_OVR) ||
        HAL_ADC_GetError(&adc) != HAL_ADC_ERROR_NONE ||
        (adc.Instance->CR2 & ADC_CR2_ADON) == 0 ||
        (ADC->CCR & (ADC_CCR_VBATE | ADC_CCR_TSVREFE)) != ADC_CCR_TSVREFE) {
        fail(); return;
    }
    if (converting) {
        if ((uint32_t)(now_ms - batch_ms) > ADC_BATCH_MS ||
            (uint32_t)(now_ms - conversion_ms) > ADC_CONVERSION_MS) { fail(); return; }
        /* Exactly one zero-timeout poll at most, never a retry loop. */
        const HAL_StatusTypeDef status = HAL_ADC_PollForConversion(&adc, 0);
        if (status == HAL_TIMEOUT) return;
        if (status != HAL_OK) { fail(); return; }
        if (__HAL_ADC_GET_FLAG(&adc, ADC_FLAG_OVR) || HAL_ADC_GetError(&adc) != HAL_ADC_ERROR_NONE) {
            fail(); return;
        }
        const uint32_t word = HAL_ADC_GetValue(&adc);
        if (word > 4095u) { fail(); return; }
        raw[scan_index++] = (uint16_t)word;
        converting = false;
        if (scan_index == PDM_ADC_RAW_COUNT) {
            /* Age starts at FIRST sample, conservatively including scan time. */
            PDM_ADC_SubmitRaw(destination, raw, batch_ms);
            scan_index = 0;
        }
    }
    if (scan_index == 0) batch_ms = now_ms;
    if (!start(now_ms)) fail();
}
