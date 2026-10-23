#ifndef PDM_CUBE_CAN_H
#define PDM_CUBE_CAN_H

#include <stdbool.h>
#include <stdint.h>

/* Diagnostic CAN1 in NORMAL mode. ACKs the bus; physical outputs stay OFF. */
typedef void (*PDM_CAN_ReceiveCallback)(uint16_t id, const uint8_t bytes[8], uint8_t dlc, void *context);
bool PDM_CAN_Init(void);
void PDM_CAN_SetReceiver(PDM_CAN_ReceiveCallback callback, void *context);
void PDM_CAN_Poll(void);
/* Nonblocking diagnostic-only TX, restricted to IDs 0x580..0x583 / DLC8. */
bool PDM_CAN_Send(uint16_t id, const uint8_t bytes[8]);
uint32_t PDM_CAN_ReceiveCount(void);
uint32_t PDM_CAN_LastStandardId(void);
uint32_t PDM_CAN_ErrorCount(void);

#endif
