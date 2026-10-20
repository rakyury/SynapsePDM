#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "../src/pdm_can.h"

static uint32_t clock_hz = 16000000u, queued, reads, errors;
static unsigned init_calls, stop_calls, deinit_calls;
static int fail_stage; /* 1 init, 2 filter, 3 start, 4 receive, 5 stop, 6 deinit */
static CAN_RxHeaderTypeDef incoming;
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio) { (void)port; (void)gpio; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return clock_hz; }
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *can)
{
    ++init_calls;
    assert(can->Instance == CAN1 && can->Init.Mode == CAN_MODE_SILENT);
    assert(can->Init.AutoRetransmission == DISABLE);
    assert(can->Init.Prescaler == 2);
    return fail_stage == 1 ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_DeInit(CAN_HandleTypeDef *can)
{ (void)can; ++deinit_calls; return fail_stage == 6 ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_CAN_Stop(CAN_HandleTypeDef *can)
{ (void)can; ++stop_calls; return fail_stage == 5 ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *can, CAN_FilterTypeDef *filter)
{
    (void)can;
    assert(filter->FilterFIFOAssignment == CAN_RX_FIFO0);
    return fail_stage == 2 ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *can)
{ (void)can; return fail_stage == 3 ? HAL_ERROR : HAL_OK; }
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *can, uint32_t fifo)
{ (void)can; assert(fifo == CAN_RX_FIFO0); return queued; }
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *can, uint32_t fifo,
                                     CAN_RxHeaderTypeDef *header, uint8_t *bytes)
{
    (void)can; assert(fifo == CAN_RX_FIFO0); ++reads;
    if (fail_stage == 4) return HAL_ERROR;
    assert(queued); --queued; *header = incoming; memset(bytes, 0xff, 8);
    return HAL_OK;
}
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *can) { (void)can; return errors; }
static void frame(uint32_t id, uint32_t ide, uint32_t rtr, uint32_t dlc)
{
    incoming = (CAN_RxHeaderTypeDef){ .StdId = id, .IDE = ide, .RTR = rtr, .DLC = dlc };
    queued = 1; PDM_CAN_Poll();
}
int main(void)
{
    queued = 10; PDM_CAN_Poll(); assert(reads == 0);
    assert(PDM_CAN_Init());
    frame(0, CAN_ID_STD, CAN_RTR_DATA, 0);
    frame(0x7ff, CAN_ID_STD, CAN_RTR_DATA, 8);
    assert(PDM_CAN_ReceiveCount() == 2 && PDM_CAN_LastStandardId() == 0x7ff);
    frame(0x800, CAN_ID_STD, CAN_RTR_DATA, 8);
    frame(0xffffffffu, CAN_ID_STD, CAN_RTR_DATA, 8);
    frame(42, CAN_ID_EXT, CAN_RTR_DATA, 8);
    frame(42, CAN_ID_STD, CAN_RTR_REMOTE, 8);
    frame(42, CAN_ID_STD, CAN_RTR_DATA, 9);
    assert(PDM_CAN_ErrorCount() == 5 && PDM_CAN_ReceiveCount() == 2);
    assert(PDM_CAN_LastStandardId() == 0x7ff);
    /* Floods of valid AND invalid frames must yield to safety scheduling. */
    queued = 100; reads = 0; PDM_CAN_Poll(); assert(reads == 16 && queued == 84);
    incoming.DLC = 8; queued = 100; reads = 0;
    PDM_CAN_Poll(); assert(reads == 16 && queued == 84);
    queued = 1; reads = 0; fail_stage = 4; PDM_CAN_Poll(); assert(reads == 1);
    fail_stage = 0; queued = 0;
    assert(PDM_CAN_Init()); assert(stop_calls == 1 && deinit_calls == 1);
    errors = 1; PDM_CAN_Poll(); PDM_CAN_Poll(); assert(PDM_CAN_ErrorCount() == 1);
    errors = 3; PDM_CAN_Poll(); assert(PDM_CAN_ErrorCount() == 2);
    errors = 0; PDM_CAN_Poll(); errors = 3; PDM_CAN_Poll();
    assert(PDM_CAN_ErrorCount() == 3);
    /* Regression: function-static previous_error used to survive Init. */
    assert(PDM_CAN_Init()); PDM_CAN_Poll(); assert(PDM_CAN_ErrorCount() == 1);
    assert(PDM_CAN_ReceiveCount() == 0 && PDM_CAN_LastStandardId() == 0);
    errors = 0;
    for (int stage = 1; stage <= 3; ++stage) {
        fail_stage = stage; assert(!PDM_CAN_Init());
        queued = 1; reads = 0; PDM_CAN_Poll(); assert(reads == 0);
        assert(PDM_CAN_ErrorCount() == 1);
        fail_stage = 0; assert(PDM_CAN_Init());
    }
    for (int stage = 5; stage <= 6; ++stage) {
        fail_stage = stage; unsigned before = init_calls;
        assert(!PDM_CAN_Init()); assert(init_calls == before);
        reads = 0; PDM_CAN_Poll(); assert(reads == 0);
        fail_stage = 0; assert(PDM_CAN_Init());
    }
    clock_hz = 42000000u; unsigned before = init_calls;
    assert(!PDM_CAN_Init()); assert(init_calls == before);
    reads = 0; PDM_CAN_Poll(); assert(reads == 0);
    clock_hz = 16000000u; assert(PDM_CAN_Init());
    puts("CAN monitor: validation, bounded poll, reinit and fail-closed lifecycle passed");
    return 0;
}
