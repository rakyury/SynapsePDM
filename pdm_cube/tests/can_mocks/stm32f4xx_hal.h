#ifndef TEST_CAN_HAL_H
#define TEST_CAN_HAL_H
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef struct {
    uint32_t Prescaler, Mode, SyncJumpWidth, TimeSeg1, TimeSeg2;
    uint32_t TimeTriggeredMode, AutoBusOff, AutoWakeUp, AutoRetransmission;
    uint32_t ReceiveFifoLocked, TransmitFifoPriority;
} CAN_InitTypeDef;
typedef struct { void *Instance; CAN_InitTypeDef Init; } CAN_HandleTypeDef;
typedef struct {
    uint32_t FilterBank, FilterMode, FilterScale, FilterIdHigh, FilterIdLow;
    uint32_t FilterMaskIdHigh, FilterMaskIdLow, FilterFIFOAssignment;
    uint32_t FilterActivation, SlaveStartFilterBank;
} CAN_FilterTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC; } CAN_RxHeaderTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC, TransmitGlobalTime; } CAN_TxHeaderTypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
#define CAN1 ((void *)1)
#define GPIOD ((void *)2)
#define __HAL_RCC_GPIOD_CLK_ENABLE() ((void)0)
#define __HAL_RCC_CAN1_CLK_ENABLE() ((void)0)
#define GPIO_PIN_0 1u
#define GPIO_PIN_1 2u
#define GPIO_MODE_AF_PP 2u
#define GPIO_PULLUP 1u
#define GPIO_SPEED_FREQ_VERY_HIGH 3u
#define GPIO_AF9_CAN1 9u
#define CAN_MODE_SILENT 0x80000000u
#define CAN_MODE_NORMAL 0u
#define CAN_SJW_1TQ 0u
#define CAN_BS1_13TQ 0x000c0000u
#define CAN_BS2_2TQ 0x00100000u
#define DISABLE 0u
#define ENABLE 1u
#define CAN_FILTERMODE_IDMASK 0u
#define CAN_FILTERSCALE_32BIT 1u
#define CAN_RX_FIFO0 0u
#define CAN_ID_STD 0u
#define CAN_ID_EXT 4u
#define CAN_RTR_DATA 0u
#define CAN_RTR_REMOTE 2u
#define HAL_CAN_ERROR_NONE 0u
/* STM32CubeF4 HAL error-mask values; mocks do not emulate bxCAN registers. */
#define HAL_CAN_ERROR_BOF 0x00000004u
#define HAL_CAN_ERROR_RX_FOV0 0x00000200u
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio);
uint32_t HAL_RCC_GetPCLK1Freq(void);
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *can);
HAL_StatusTypeDef HAL_CAN_DeInit(CAN_HandleTypeDef *can);
HAL_StatusTypeDef HAL_CAN_Stop(CAN_HandleTypeDef *can);
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *can, CAN_FilterTypeDef *filter);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *can);
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *can, uint32_t fifo);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *can, uint32_t fifo,
                                     CAN_RxHeaderTypeDef *header, uint8_t *bytes);
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *can);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *can);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *can, CAN_TxHeaderTypeDef *header,
                                      uint8_t *bytes, uint32_t *mailbox);
/* No GPIO output API: this HAL transport cannot control a physical output. */
#endif
