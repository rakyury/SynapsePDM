/*
 * PDM HAL CAN1 diagnostic link: NORMAL mode for telemetry/RAM VM upload.
 * It acknowledges frames but never changes a physical output or legacy config.
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
static bool s_initialized;
static uint32_t s_previous_error;
static PDM_CAN_ReceiveCallback s_receiver;
static void *s_receiver_context;

void PDM_CAN_SetReceiver(PDM_CAN_ReceiveCallback callback, void *context)
{
    s_receiver = callback;
    s_receiver_context = context;
}

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
    s_previous_error = HAL_CAN_ERROR_NONE;
    /* Init may be requested again after a bus/clock error. Do not reconfigure
       a running HAL handle or keep polling a partially initialized one. */
    if (s_initialized)
    {
        bool cleanup_ok = true;
        if (s_started && HAL_CAN_Stop(&s_can) != HAL_OK)
            cleanup_ok = false;
        s_started = false;
        if (HAL_CAN_DeInit(&s_can) != HAL_OK)
            cleanup_ok = false;
        else
            s_initialized = false;
        if (!cleanup_ok)
        {
            ++s_errors;
            return false;
        }
    }
    s_started = false;

    /* HSI 16 MHz APB1 / prescaler 2 / (1 + 13 + 2) tq = 500 kbit/s.
       This timing MUST be recalculated before raising the CPU/APB clocks.
       CAN termination is left in its hardware default state. */
    s_can.Instance = CAN1;
    s_can.Init.Prescaler = 2;
    s_can.Init.Mode = CAN_MODE_NORMAL; /* Diagnostic TX/upload; explicitly ACKs bus traffic. */
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
    /* Remember even a failed attempt: HAL may have configured GPIO/clocks
       before returning an error, so the next attempt must deinitialize. */
    s_initialized = true;
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

        /* Ignore RTR, extended IDs and malformed DLC. Only the foreground
           diagnostic handler may inspect data; it has no output/config API. */
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
        if (s_receiver != NULL)
            s_receiver((uint16_t)h.StdId, bytes, (uint8_t)h.DLC, s_receiver_context);
    }

    /* ErrorCode is a persistent HAL bitmask. Count transitions instead of
       charging an error on every scheduling tick. */
    {
        uint32_t error = HAL_CAN_GetError(&s_can);
        if (error != HAL_CAN_ERROR_NONE && error != s_previous_error)
            ++s_errors;
        s_previous_error = error;
    }
}

bool PDM_CAN_Send(uint16_t id, const uint8_t bytes[8])
{
    CAN_TxHeaderTypeDef header = {0};
    uint32_t mailbox;
    if (!s_started || bytes == NULL || id < 0x580u || id > 0x583u) return false;
    if (HAL_CAN_GetTxMailboxesFreeLevel(&s_can) == 0u) return false;
    header.StdId = id; header.IDE = CAN_ID_STD; header.RTR = CAN_RTR_DATA;
    header.DLC = 8; header.TransmitGlobalTime = DISABLE;
    /* HAL does not retain the payload pointer; it copies to mailbox registers. */
    if (HAL_CAN_AddTxMessage(&s_can, &header, (uint8_t *)bytes, &mailbox) != HAL_OK) {
        ++s_errors;
        return false;
    }
    return true;
}

uint32_t PDM_CAN_ReceiveCount(void) { return s_received; }
uint32_t PDM_CAN_LastStandardId(void) { return s_last_id; }
uint32_t PDM_CAN_ErrorCount(void) { return s_errors; }
