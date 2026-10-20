/* Compile the actual main.c under a fake HAL; verify software GPIO writes,
 * not electrical voltage, driver latency or hardware power-on behavior. */
#include <assert.h>
#include <stdio.h>
#define main pdm_firmware_main
#include "../src/main.c"
#undef main

GPIO_TypeDef mock_gpio_g, mock_gpio_f;
static uint32_t now;
static unsigned tx_count, gpio_init_count;
static void assert_off(void)
{
    assert(GPIOG->BSRR == (uint32_t)PDM_GPIOG_OUTPUT_PINS << 16);
    assert(GPIOF->BSRR == (uint32_t)PDM_GPIOF_OUTPUT_PINS << 16);
    assert(s_outputs_permitted == 0);
}
HAL_StatusTypeDef HAL_Init(void) { return HAL_OK; }
uint32_t HAL_GetTick(void) { return now; }
void HAL_IncTick(void) { ++now; }
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio)
{
    assert_off(); assert(port == GPIOG || port == GPIOF); ++gpio_init_count;
    assert(gpio->Mode == GPIO_MODE_OUTPUT_PP && gpio->Pull == GPIO_PULLDOWN);
}
bool PDM_CAN_Init(void) { assert_off(); return true; }
void PDM_CAN_SetReceiver(PDM_CAN_ReceiveCallback callback, void *context)
{ assert(callback != NULL && context != NULL); }
void PDM_CAN_Poll(void) { assert_off(); }
bool PDM_CAN_Send(uint16_t id, const uint8_t bytes[8])
{
    assert_off(); assert(id >= 0x580 && id <= 0x583); assert(bytes); ++tx_count;
    return true;
}
uint32_t PDM_CAN_ReceiveCount(void) { return 0; }
uint32_t PDM_CAN_LastStandardId(void) { return 0; }
uint32_t PDM_CAN_ErrorCount(void) { return 0; }

int main(void)
{
    pdm_init_safe_output_gpio(); assert_off(); assert(gpio_init_count == 2);
    pdm_register_observable_channels(); PDM_Link_Init(&s_link, &s_channel_engine, &s_vm);
    /* A nonzero diagnostic VM result cannot become gate demand. */
    const uint8_t expression[] = {1, 1, 0, 0, 0, 0xff};
    assert(PDM_Link_ValidateBytecode(expression, sizeof(expression)));
    for (unsigned i = 0; i < sizeof(expression); ++i) s_link.active[i] = expression[i];
    s_link.active_length = sizeof(expression); s_link.active_valid = true;
    for (now = 10; now <= 500; now += 10) {
        /* Tick must reassert OFF before the CAN handler runs. */
        GPIOG->BSRR = 0; GPIOF->BSRR = 0; tx_count = 0;
        pdm_nonactuating_tick(now); assert_off(); assert(tx_count <= 3);
        assert(Channel_GetValue(&s_channel_engine, 300) == 1);
        assert(Channel_GetValue(&s_channel_engine, 230) == 1);
        for (unsigned i = 0; i < PDM_CHANNEL_COUNT; ++i) {
            assert(s_output_safety[i].state == PDM_OUTPUT_LOCKOUT);
            assert(Channel_GetValue(&s_channel_engine, (uint16_t)(200 + i)) == 0);
        }
    }
    puts("actual main.c fake-HAL: OFF before GPIO mode/RX/TX, nonzero VM isolated, TX <=3 per tick");
    return 0;
}
