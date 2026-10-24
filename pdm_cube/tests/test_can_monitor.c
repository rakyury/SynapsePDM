#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "../src/pdm_can.h"

static uint32_t clock_hz = 16000000u, queued, reads, errors;
static unsigned init_calls, stop_calls, deinit_calls;
static unsigned tx_calls, delivered;
static uint32_t free_mailboxes = 3;
static int fail_stage; /* 1 init, 2 filter, 3 start, 4 receive, 5 stop, 6 deinit */
static CAN_RxHeaderTypeDef incoming;
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio) { (void)port; (void)gpio; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return clock_hz; }
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *can)
{
    ++init_calls;
    assert(can->Instance == CAN1 && can->Init.Mode == CAN_MODE_NORMAL);
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
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *can) { (void)can; return free_mailboxes; }
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *can, CAN_TxHeaderTypeDef *header,
                                      uint8_t *bytes, uint32_t *mailbox)
{
    (void)can; (void)bytes; ++tx_calls; *mailbox = 0;
    assert(header->StdId >= 0x580 && header->StdId <= 0x587);
    assert(header->DLC == 8 && header->IDE == CAN_ID_STD && header->RTR == CAN_RTR_DATA);
    return fail_stage == 7 ? HAL_ERROR : HAL_OK;
}
static void receiver(uint16_t id, const uint8_t bytes[8], uint8_t dlc, void *context)
{ (void)bytes; assert(id <= 0x7ff && dlc <= 8); assert(context == &delivered); ++delivered; }
static void frame(uint32_t id, uint32_t ide, uint32_t rtr, uint32_t dlc)
{
    incoming = (CAN_RxHeaderTypeDef){ .StdId = id, .IDE = ide, .RTR = rtr, .DLC = dlc };
    queued = 1; PDM_CAN_Poll();
}
int main(void)
{
    queued = 10; PDM_CAN_Poll(); assert(reads == 0);
    PDM_CAN_SetReceiver(receiver, &delivered);
    assert(PDM_CAN_Init());
    frame(0, CAN_ID_STD, CAN_RTR_DATA, 0);
    frame(0x7ff, CAN_ID_STD, CAN_RTR_DATA, 8);
    assert(PDM_CAN_ReceiveCount() == 2 && PDM_CAN_LastStandardId() == 0x7ff);
    assert(delivered == 2);
    frame(0x800, CAN_ID_STD, CAN_RTR_DATA, 8);
    frame(0xffffffffu, CAN_ID_STD, CAN_RTR_DATA, 8);
    frame(42, CAN_ID_EXT, CAN_RTR_DATA, 8);
    frame(42, CAN_ID_STD, CAN_RTR_REMOTE, 8);
    frame(42, CAN_ID_STD, CAN_RTR_DATA, 9);
    assert(PDM_CAN_ErrorCount() == 5 && PDM_CAN_ReceiveCount() == 2);
    assert(PDM_CAN_LastStandardId() == 0x7ff);
    assert(delivered == 2);
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
    /* Named HAL-reported bus-off/FIFO-overrun faults: persistent bitmasks count
       once per transition; successful explicit reinit clears the epoch. This
       does not simulate physical IRQ delivery or CAN automatic bus recovery. */
    queued=0; errors=HAL_CAN_ERROR_BOF; PDM_CAN_Poll();
    assert(PDM_CAN_ErrorCount()==1);
    for(unsigned i=0;i<100;++i) PDM_CAN_Poll();
    assert(PDM_CAN_ErrorCount()==1);
    errors |= HAL_CAN_ERROR_RX_FOV0; PDM_CAN_Poll(); assert(PDM_CAN_ErrorCount()==2);
    errors=0; PDM_CAN_Poll(); errors=HAL_CAN_ERROR_RX_FOV0; PDM_CAN_Poll();
    assert(PDM_CAN_ErrorCount()==3);
    errors=0; assert(PDM_CAN_Init()); assert(PDM_CAN_ErrorCount()==0);
    uint8_t payload[8] = {0};
    assert(!PDM_CAN_Send(0x700, payload) && !PDM_CAN_Send(0x590, payload));
    assert(!PDM_CAN_Send(0x580, NULL) && tx_calls == 0);
    free_mailboxes = 0;
    for(unsigned i=0;i<1000;++i) {
        assert(!PDM_CAN_Send(0x580,payload) && tx_calls==0);
        queued=100; reads=0; PDM_CAN_Poll(); assert(reads==16 && queued==84);
    }
    queued=0; assert(PDM_CAN_ErrorCount()==0);
    free_mailboxes = 3; assert(PDM_CAN_Send(0x580, payload) && tx_calls == 1);
    assert(!PDM_CAN_Send(0x588, payload) && tx_calls == 1);
    assert(PDM_CAN_Send(0x584, payload) && tx_calls == 2);
    for(uint16_t id=0x585;id<=0x587;++id) assert(PDM_CAN_Send(id,payload));
    assert(tx_calls==5);
    fail_stage = 7; uint32_t previous_errors = PDM_CAN_ErrorCount();
    assert(!PDM_CAN_Send(0x581, payload) && PDM_CAN_ErrorCount() == previous_errors + 1);
    fail_stage = 1; assert(!PDM_CAN_Init());
    assert(!PDM_CAN_Send(0x580, payload) && tx_calls == 6);
    puts("CAN diagnostic transport: normal mode, TX whitelist, bounded poll and fail-closed lifecycle passed");
    return 0;
}
