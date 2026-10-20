/*
 * PDM HAL CAN1 bring-up: monitor-only, intentionally does not transmit,
 * interpret configuration commands, or change a hardware output.
 * Uses the original F446 pin mapping CAN1 PD0 (RX) / PD1 (TX) AF9.
 */
#include "pdm_can.h"
#include "stm32f4xx_hal.h"

#define PDM_CAN_CLOCK_HZ 16000000u /* Initial HSI/PCLK1 in main.c */
#define PDM_CAN_BITRATE 500000u
#define PDM_CAN_RX_LIMIT_PER_TICK 16u

static CAN_HandleTypeDef s_can;
static volatile uint32_t s_received;
static volatile uint32_t s_last_id;
static volatile uint32_t s_errors;
static bool s_started;

void HAL_CAN_MspInit(CAN_HandleTypeDef *hcan)
{
    GPIO_InitTypeDef gpio = {0};
    if (hcan == NULL || hcan->Instance != CAN1)
        return;
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_CAN1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOD, &gpio);
}

bool PDM_CAN_Init(void)
{
    CAN_FilterTypeDef filter = {0};
    s_received = 0;
    s_last_id = 0;
    s_errors = 0;
    s_started = false;

    /* HSI 16 MHz APB1 / prescaler 2 / (1 + 13 + 2) tq = 500 kbit/s.
       This timing MUST be recalculated before raising the CPU/APB clocks.
       CAN termination is left in its hardware default state. */
    s_can.Instance = CAN1;
    s_can.Init.Prescaler = 2;
    s_can.Init.Mode = CAN_MODE_NORMAL;
    s_can.Init.SyncJumpWidth = CAN_SJW_1TQ;
    s_can.Init.TimeSeg1 = CAN_BS1_13TQ;
    s_can.Init.TimeSeg2 = CAN_BS2_2TQ;
    s_can.Init.TimeTriggeredMode = DISABLE;
    s_can.Init.AutoBusOff = ENABLE;
    s_can.Init.AutoWakeUp = DISABLE;
    s_can.Init.AutoRetransmission = DISABLE;
    s_can.Init.ReceiveFifoLocked = DISABLE;
    s_can.Init.TransmitFifoPriority = DISABLE;

    if (HAL_RCC_GetPCLK1Freq() != PDM_CAN_CLOCK_HZ)
    {
        ++s_errors;
        return false;
    }
    if (HAL_CAN_Init(&s_can) != HAL_OK)
    {
        ++s_errors;
        return false;
    }

    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0;
    filter.FilterIdLow = 0;
    filter.FilterMaskIdHigh = 0;
    filter.FilterMaskIdLow = 0;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;
    if (HAL_CAN_ConfigFilter(&s_can, &filter) != HAL_OK ||
        HAL_CAN_Start(&s_can) != HAL_OK)
    {
        ++s_errors;
        return false;
    }
    s_started = true;
    return true;
}

void PDM_CAN_Poll(void)
{
    uint32_t frames = 0;
    if (!s_started)
        return;

    while (frames < PDM_CAN_RX_LIMIT_PER_TICK &&
           HAL_CAN_GetRxFifoFillLevel(&s_can, CAN_RX_FIFO0) != 0u)
    {
        CAN_RxHeaderTypeDef h = {0};
        uint8_t bytes[8] = {0};
        if (HAL_CAN_GetRxMessage(&s_can, CAN_RX_FIFO0, &h, bytes) != HAL_OK)
        {
            ++s_errors;
            break;
        }

        /* No writes, no reconfiguration and no load actuation in this stage.
           Ignore RTR, extended IDs and malformed DLC. */
        if (h.IDE != CAN_ID_STD || h.RTR != CAN_RTR_DATA ||
            h.DLC > 8u || h.StdId > 0x7FFu)
        {
            ++s_errors;
            ++frames;
            continue;
        }
        s_last_id = h.StdId;
        ++s_received;
        ++frames;
    }

    if (HAL_CAN_GetError(&s_can) != HAL_CAN_ERROR_NONE)
    {
        ++s_errors;
        (void)HAL_CAN_ResetError(&s_can);
    }
}

uint32_t PDM_CAN_ReceiveCount(void) { return s_received; }
uint32_t PDM_CAN_LastStandardId(void) { return s_last_id; }
uint32_t PDM_CAN_ErrorCount(void) { return s_errors; }
