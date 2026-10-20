#ifndef PDM_CUBE_CAN_H
#define PDM_CUBE_CAN_H

#include <stdbool.h>
#include <stdint.h>

/* Read-only CAN1 monitor during HAL migration.  No CAN command output exists. */
bool PDM_CAN_Init(void);
void PDM_CAN_Poll(void);
uint32_t PDM_CAN_ReceiveCount(void);
uint32_t PDM_CAN_LastStandardId(void);
uint32_t PDM_CAN_ErrorCount(void);

#endif
