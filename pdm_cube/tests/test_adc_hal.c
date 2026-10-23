#include "pdm_adc_hal.h"
#include "stm32f4xx_hal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
ADC_TypeDef mock_adc;
ADC_Common_TypeDef mock_adc_common;
static PDM_ADC model;
static unsigned starts, polls, configs, gpio_count, position;
static uint32_t clock_hz, prescaler, bad_word;
static uint32_t adc_error;
static bool overrun_in_poll;
static HAL_StatusTypeDef init_status, start_status, config_status, poll_status;
static const uint32_t expected[] = {0,1,2,3,4,5,6,9,8,7,13,12,11,10,14,17,18};
uint16_t mock_calibration(uint32_t address)
{
    switch(address) { case 0x1FFF7A2A: return 1500; case 0x1FFF7A2C: return 1000;
        case 0x1FFF7A2E: return 1300; default: assert(0); return 0; }
}
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio)
{
    assert(gpio->Mode == GPIO_MODE_ANALOG && gpio->Pull == GPIO_NOPULL);
    assert((port == GPIOA && gpio->Pin == 0xff) || (port == GPIOB && gpio->Pin == 3) ||
           (port == GPIOC && gpio->Pin == 0x1f));
    ++gpio_count;
}
uint32_t HAL_RCC_GetPCLK2Freq(void) { return clock_hz; }
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *handle)
{
    assert(handle->Instance == ADC1); prescaler = handle->Init.ClockPrescaler;
    assert(handle->Init.Resolution == ADC_RESOLUTION_12B);
    assert(handle->Init.ScanConvMode == DISABLE && handle->Init.ContinuousConvMode == DISABLE);
    assert(handle->Init.DMAContinuousRequests == DISABLE && handle->Init.NbrOfConversion == 1);
    assert(handle->Init.ExternalTrigConvEdge == ADC_EXTERNALTRIGCONVEDGE_NONE);
    return init_status;
}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *handle, ADC_ChannelConfTypeDef *config)
{
    ++configs; assert(handle->Instance == ADC1); assert(config->Rank == 1);
    assert(config->SamplingTime == ADC_SAMPLETIME_480CYCLES);
    /* Neither HAL path with a temperature busy loop nor its VBAT path is used. */
    assert(config->Channel != ADC_CHANNEL_TEMPSENSOR && config->Channel != 18);
    handle->Instance->SQR3 = (handle->Instance->SQR3 & ~ADC_SQR3_SQ1) | config->Channel;
    return config_status;
}
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *handle)
{
    assert(handle->Instance->CR2 & ADC_CR2_ADON); /* HAL enable-delay cannot run. */
    assert((ADC->CCR & ADC_CCR_VBATE) == 0 && (ADC->CCR & ADC_CCR_TSVREFE) != 0);
    assert((handle->Instance->SQR3 & ADC_SQR3_SQ1) == expected[position]);
    assert((handle->Instance->SQR3 & ~ADC_SQR3_SQ1) == 0xa0000000u);
    if (position == 16) {
        assert((handle->Instance->SMPR1 & ADC_SMPR1_SMP18) == 7u<<24);
        assert((handle->Instance->SMPR1 & ~ADC_SMPR1_SMP18) == 0xa0000000u);
    }
    ++starts; return start_status;
}
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *handle, uint32_t timeout)
{ assert(handle->Instance == ADC1); assert(timeout == 0); ++polls;
  if(overrun_in_poll) ADC1->SR |= ADC_FLAG_OVR;
  return poll_status; }
uint32_t HAL_ADC_GetError(ADC_HandleTypeDef *handle) { assert(handle->Instance == ADC1); return adc_error; }
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *handle)
{
    assert(handle->Instance == ADC1);
    const uint32_t result = position < 14 ? 100 : position == 14 ? 3000 : position == 15 ? 1500 : 1000;
    position = (position + 1) % 17;
    return bad_word != 0 ? bad_word : result;
}
static void reset(void)
{
    memset(&mock_adc, 0, sizeof(mock_adc)); mock_adc.SMPR1 = 0xa0000000u; mock_adc.SQR3 = 0xa0000000u;
    mock_adc_common.CCR = ADC_CCR_VBATE;
    starts = polls = configs = gpio_count = position = 0; bad_word = 0;
    adc_error = 0; overrun_in_poll = false;
    init_status = start_status = config_status = poll_status = HAL_OK; clock_hz = 16000000;
}
static void tick(uint32_t now)
{ const unsigned before = polls; PDM_ADC_HAL_Tick(now); assert(polls - before <= 1); }
static void quality(uint8_t q)
{ int32_t value; assert(PDM_ADC_Read(&model,100,200,&value) == q); if(q != PDM_ADC_NOMINAL) assert(value == 0); }
int main(void)
{
    reset(); assert(PDM_ADC_HAL_Init(&model,0)); assert(gpio_count == 3);
    assert(prescaler == ADC_CLOCK_SYNC_PCLK_DIV2); tick(0); assert(starts == 0);
    tick(1); assert(starts == 1 && polls == 0); quality(PDM_ADC_UNAVAILABLE);
    for (uint32_t t=11; t<=161; t+=10) { tick(t); quality(PDM_ADC_UNAVAILABLE); }
    tick(171); quality(PDM_ADC_NOMINAL); assert(model.captured_ms == 1);
    assert(polls == 17 && starts == 18);
    int32_t value; assert(PDM_ADC_Read(&model,102,200,&value) == PDM_ADC_VALID && value == 30000);
    /* Failures invalidate a previous valid batch immediately and never restart. */
    poll_status = HAL_ERROR; tick(181); quality(PDM_ADC_INVALID);
    unsigned stopped = starts; tick(191); assert(starts == stopped && !(ADC1->CR2 & ADC_CR2_ADON));
    reset(); assert(PDM_ADC_HAL_Init(&model,0)); tick(1); poll_status = HAL_TIMEOUT;
    tick(11); tick(21); tick(31); quality(PDM_ADC_UNAVAILABLE);
    tick(32); quality(PDM_ADC_INVALID); assert(starts == 1);
    /* Individual conversions may finish within30ms but whole batch must<=250. */
    reset(); assert(PDM_ADC_HAL_Init(&model,0)); tick(1);
    for (uint32_t t=11;t<=261;t+=10) { poll_status = ((t/10)%2) ? HAL_TIMEOUT : HAL_OK; tick(t); }
    quality(PDM_ADC_INVALID); assert(starts < 17);
    reset(); assert(PDM_ADC_HAL_Init(&model,0)); tick(1); bad_word=65536; tick(11); quality(PDM_ADC_INVALID);
    reset(); init_status=HAL_ERROR; assert(!PDM_ADC_HAL_Init(&model,0)); tick(10); assert(starts==0); quality(PDM_ADC_INVALID);
    reset(); config_status=HAL_ERROR; assert(PDM_ADC_HAL_Init(&model,0)); tick(1); assert(starts==0); quality(PDM_ADC_INVALID);
    reset(); start_status=HAL_ERROR; assert(PDM_ADC_HAL_Init(&model,0)); tick(1); quality(PDM_ADC_INVALID);
    reset(); assert(PDM_ADC_HAL_Init(&model,0)); ADC1->CR2=0; tick(1); assert(starts==0); quality(PDM_ADC_INVALID);
    reset(); assert(PDM_ADC_HAL_Init(&model,0)); tick(1); ADC1->SR=ADC_FLAG_OVR; tick(11); quality(PDM_ADC_INVALID); assert(polls==0);
    reset(); assert(PDM_ADC_HAL_Init(&model,0)); tick(1); overrun_in_poll=true; tick(11); quality(PDM_ADC_INVALID); assert(position==0);
    reset(); assert(PDM_ADC_HAL_Init(&model,0)); tick(1); adc_error=1; tick(11); quality(PDM_ADC_INVALID);
    reset(); assert(PDM_ADC_HAL_Init(&model,0)); tick(1); ADC->CCR |= ADC_CCR_VBATE; tick(11); quality(PDM_ADC_INVALID);
    reset(); assert(PDM_ADC_HAL_Init(&model,UINT32_MAX)); tick(UINT32_MAX); assert(starts==0);
    tick(0); assert(starts==1); tick(10); assert(polls==1);
    const uint32_t frequencies[] = {36000000,72000000,90000000,144000000};
    const uint32_t divisors[] = {ADC_CLOCK_SYNC_PCLK_DIV2,ADC_CLOCK_SYNC_PCLK_DIV4,ADC_CLOCK_SYNC_PCLK_DIV6,ADC_CLOCK_SYNC_PCLK_DIV8};
    for(unsigned i=0;i<4;++i) { reset(); clock_hz=frequencies[i]; assert(PDM_ADC_HAL_Init(&model,0)); assert(prescaler==divisors[i]); }
    reset(); clock_hz=145000000; assert(!PDM_ADC_HAL_Init(&model,0)); quality(PDM_ADC_INVALID);
    reset(); clock_hz=1199999; assert(!PDM_ADC_HAL_Init(&model,0)); quality(PDM_ADC_INVALID);
    assert(!PDM_ADC_HAL_Init(NULL,0));
    puts("ADC HAL: exact17pin scan/480cycles/mux/startup/one poll/atomic batch/deadlines/errors/clocks/wrap PASS");
}
