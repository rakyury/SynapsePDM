#ifndef TEST_MAIN_HAL_H
#define TEST_MAIN_HAL_H
#include "../can_mocks/stm32f4xx_hal.h"
typedef struct { uint32_t BSRR; } GPIO_TypeDef;
extern GPIO_TypeDef mock_gpio_g, mock_gpio_f;
#define GPIOG (&mock_gpio_g)
#define GPIOF (&mock_gpio_f)
#define GPIO_PIN_2 (1u << 2)
#define GPIO_PIN_3 (1u << 3)
#define GPIO_PIN_4 (1u << 4)
#define GPIO_PIN_5 (1u << 5)
#define GPIO_PIN_6 (1u << 6)
#define GPIO_PIN_9 (1u << 9)
#define GPIO_PIN_10 (1u << 10)
#define GPIO_PIN_12 (1u << 12)
#define GPIO_PIN_13 (1u << 13)
#define GPIO_PIN_14 (1u << 14)
#define GPIO_PIN_15 (1u << 15)
#define GPIO_MODE_OUTPUT_PP 1u
#define GPIO_PULLDOWN 2u
#define GPIO_SPEED_FREQ_LOW 0u
#define __HAL_RCC_GPIOG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOF_CLK_ENABLE() ((void)0)
#define __disable_irq() ((void)0)
HAL_StatusTypeDef HAL_Init(void);
uint32_t HAL_GetTick(void);
void HAL_IncTick(void);
#endif
