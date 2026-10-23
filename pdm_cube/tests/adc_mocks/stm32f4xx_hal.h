#ifndef TEST_ADC_HAL_H
#define TEST_ADC_HAL_H
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t CR2, SMPR1, SQR3, SR; } ADC_TypeDef;
typedef struct { uint32_t CCR; } ADC_Common_TypeDef;
extern ADC_TypeDef mock_adc;
extern ADC_Common_TypeDef mock_adc_common;
#define ADC1 (&mock_adc)
#define ADC (&mock_adc_common)
typedef struct {
    uint32_t ClockPrescaler,Resolution,ScanConvMode,ContinuousConvMode,DiscontinuousConvMode;
    uint32_t ExternalTrigConvEdge,ExternalTrigConv,DataAlign,NbrOfConversion,DMAContinuousRequests,EOCSelection;
} ADC_InitTypeDef;
typedef struct { ADC_TypeDef *Instance; ADC_InitTypeDef Init; } ADC_HandleTypeDef;
typedef struct { uint32_t Channel,Rank,SamplingTime,Offset; } ADC_ChannelConfTypeDef;
typedef struct { uint32_t Pin,Mode,Pull,Speed; } GPIO_InitTypeDef;
#define GPIOA ((void *)1)
#define GPIOB ((void *)2)
#define GPIOC ((void *)3)
#define GPIO_PIN_0 (1u<<0)
#define GPIO_PIN_1 (1u<<1)
#define GPIO_PIN_2 (1u<<2)
#define GPIO_PIN_3 (1u<<3)
#define GPIO_PIN_4 (1u<<4)
#define GPIO_PIN_5 (1u<<5)
#define GPIO_PIN_6 (1u<<6)
#define GPIO_PIN_7 (1u<<7)
#define GPIO_MODE_ANALOG 3u
#define GPIO_NOPULL 0u
#define __HAL_RCC_ADC1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define ADC_CR2_ADON 1u
#define ADC_FLAG_OVR (1u<<5)
#define HAL_ADC_ERROR_NONE 0u
#define __HAL_ADC_GET_FLAG(handle, flag) ((handle)->Instance->SR & (flag))
#define ADC_CCR_VBATE (1u<<22)
#define ADC_CCR_TSVREFE (1u<<23)
#define ADC_SMPR1_SMP18 (7u<<24)
#define ADC_SQR3_SQ1 31u
#define ADC_CHANNEL_18 18u
#define ADC_CHANNEL_VREFINT 17u
#define ADC_CHANNEL_TEMPSENSOR (18u | (1u<<28))
#define ADC_SAMPLETIME_480CYCLES 7u
#define ADC_CLOCK_SYNC_PCLK_DIV2 0u
#define ADC_CLOCK_SYNC_PCLK_DIV4 (1u<<16)
#define ADC_CLOCK_SYNC_PCLK_DIV6 (2u<<16)
#define ADC_CLOCK_SYNC_PCLK_DIV8 (3u<<16)
#define ADC_RESOLUTION_12B 0u
#define ADC_EXTERNALTRIGCONVEDGE_NONE 0u
#define ADC_SOFTWARE_START 1u
#define ADC_DATAALIGN_RIGHT 0u
#define ADC_EOC_SINGLE_CONV 1u
#define DISABLE 0u
#define __HAL_ADC_ENABLE(handle) ((handle)->Instance->CR2 |= ADC_CR2_ADON)
#define __HAL_ADC_DISABLE(handle) ((handle)->Instance->CR2 &= ~ADC_CR2_ADON)
uint16_t mock_calibration(uint32_t address);
#define PDM_ADC_FACTORY_READ16(address) mock_calibration(address)
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio);
uint32_t HAL_RCC_GetPCLK2Freq(void);
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *handle, ADC_ChannelConfTypeDef *config);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *handle, uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *handle);
uint32_t HAL_ADC_GetError(ADC_HandleTypeDef *handle);
#endif
