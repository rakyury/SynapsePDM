/*
 * Synapse PDM STM32Cube HAL bring-up — stage 1.
 *
 * This is a NON-ACTUATING baseline for STM32F446ZET6 hardware. All physical
 * output-gate pins remain low. No received CAN/VM value can energize a load.
 * Read pdm_cube/README.md before changing this file.
 */
#include "stm32f4xx_hal.h"
#include "r2_shared/channel_engine/channel.h"
#include "r2_shared/vm/vm.h"

#include <stddef.h>
#include <stdint.h>

/* Verified against original firmware OutputHandler.cpp pin groups. */
#define PDM_GPIOG_OUTPUT_PINS (GPIO_PIN_10 | GPIO_PIN_9 | GPIO_PIN_6 | \
                               GPIO_PIN_5 | GPIO_PIN_4 | GPIO_PIN_3 | GPIO_PIN_2)
#define PDM_GPIOF_OUTPUT_PINS (GPIO_PIN_15 | GPIO_PIN_14 | GPIO_PIN_13 | \
                               GPIO_PIN_12 | GPIO_PIN_2 | GPIO_PIN_1 | GPIO_PIN_0)
#define PDM_CHANNEL_COUNT 14u
#define PDM_TICK_PERIOD_MS 10u

/* Channel engine and VM use static, bounded storage (no heap allocation). */
static ChannelEngine_t s_channel_engine;
static VM_t s_vm;
volatile uint32_t pdm_cube_heartbeat_ms = 0;
volatile uint32_t pdm_cube_fault_mask = 0;
static volatile uint8_t s_outputs_permitted = 0;

/* Force OFF before configuring an output GPIO mode; also use in fatal paths. */
static void pdm_force_all_outputs_off(void)
{
    GPIOG->BSRR = ((uint32_t)PDM_GPIOG_OUTPUT_PINS) << 16;
    GPIOF->BSRR = ((uint32_t)PDM_GPIOF_OUTPUT_PINS) << 16;
}

/* Hardware default state must also be guaranteed by board pull-downs. */
static void pdm_init_safe_output_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    pdm_force_all_outputs_off();

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = PDM_GPIOG_OUTPUT_PINS;
    HAL_GPIO_Init(GPIOG, &gpio);
    gpio.Pin = PDM_GPIOF_OUTPUT_PINS;
    HAL_GPIO_Init(GPIOF, &gpio);
    pdm_force_all_outputs_off();
}

static void pdm_register_observable_channels(void)
{
    static const char *const output_names[PDM_CHANNEL_COUNT] = {
        "output_01", "output_02", "output_03", "output_04",
        "output_05", "output_06", "output_07", "output_08",
        "output_09", "output_10", "output_11", "output_12",
        "output_13", "output_14"
    };
    uint16_t i;
    Channel_Init(&s_channel_engine);
    for (i = 0; i < PDM_CHANNEL_COUNT; ++i)
    {
        /* 200..213 represent gate state (0 until output engine is validated). */
        (void)Channel_Register(&s_channel_engine, (uint16_t)(200u + i),
                               CH_TYPE_BOOL, CH_CLASS_SYSTEM, output_names[i]);
    }
    (void)Channel_Register(&s_channel_engine, 230u, CH_TYPE_BOOL,
                           CH_CLASS_SYSTEM, "outputs_interlocked");

    VM_Init(&s_vm, &s_channel_engine);
    Channel_SetValue(&s_channel_engine, 230u, 1);
}

static void pdm_nonactuating_tick(uint32_t now_ms)
{
    uint16_t i;
    Channel_BeginTick(&s_channel_engine, now_ms);
    /* Until a separately reviewed and validated power-core is integrated,
     * all virtual demand and CAN states are ignored by the hardware.
     */
    for (i = 0; i < PDM_CHANNEL_COUNT; ++i)
    {
        Channel_SetValue(&s_channel_engine, (uint16_t)(200u + i), 0);
    }
    Channel_SetValue(&s_channel_engine, 230u, 1);
    VM_ExecuteAll(&s_vm);
    Channel_EndTick(&s_channel_engine);
    /* Reassert OFF in each tick even if other logic corrupts GPIO output. */
    pdm_force_all_outputs_off();
    s_outputs_permitted = 0;
    pdm_cube_heartbeat_ms = now_ms;
}

void Error_Handler(void)
{
    pdm_cube_fault_mask |= 1u;
    s_outputs_permitted = 0;
    /* If clocks were previously initialized, this asserts OFF. No ISR
     * or background task is allowed to switch the gate GPIOs back on.
     */
    pdm_force_all_outputs_off();
    __disable_irq();
    for (;;)
    {
    }
}

/* HAL_Init() configures SysTick; this IRQ advances the HAL millisecond tick. */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

int main(void)
{
    uint32_t last_tick;
    HAL_Init();
    pdm_init_safe_output_gpio();
    pdm_register_observable_channels();
    last_tick = HAL_GetTick();

    for (;;)
    {
        const uint32_t now = HAL_GetTick();
        if ((uint32_t)(now - last_tick) >= PDM_TICK_PERIOD_MS)
        {
            last_tick = now;
            pdm_nonactuating_tick(now);
        }
    }
}
