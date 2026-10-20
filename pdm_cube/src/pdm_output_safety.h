#ifndef PDM_OUTPUT_SAFETY_H
#define PDM_OUTPUT_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

/* Pure policy only: this API neither owns GPIO nor authorizes actuation. */
typedef enum {
    PDM_OUTPUT_OFF, PDM_OUTPUT_STARTING, PDM_OUTPUT_ON,
    PDM_OUTPUT_FAULT_WAIT, PDM_OUTPUT_LOCKOUT
} PDM_OutputState;

enum {
    PDM_FAULT_OVERCURRENT = 1u << 0,
    PDM_FAULT_CURRENT_INVALID = 1u << 1,
    PDM_FAULT_VBATT_INVALID = 1u << 2,
    PDM_FAULT_EMERGENCY = 1u << 3,
    PDM_FAULT_COMMAND_LOST = 1u << 4,
    PDM_FAULT_POLICY_INVALID = 1u << 5
};

typedef struct {
    uint32_t startup_ms;
    uint32_t retry_cooldown_ms;
    uint8_t max_retries; /* zero disables automatic retries */
} PDM_OutputPolicy;

typedef struct {
    bool demand;
    bool reset; /* rising edge, healthy inputs and demand OFF required */
    bool overcurrent;
    bool current_valid;
    bool vbatt_valid;
    bool emergency;
    bool command_valid;
} PDM_OutputInputs;

typedef struct {
    PDM_OutputState state;
    uint32_t entered_ms;
    uint32_t latched_faults;
    uint8_t retries;
    bool reset_previous;
} PDM_OutputSafety;

void PDM_OutputSafety_Init(PDM_OutputSafety *s, uint32_t now_ms);
void PDM_OutputSafety_Step(PDM_OutputSafety *s, const PDM_OutputPolicy *policy,
                           const PDM_OutputInputs *inputs, uint32_t now_ms);

#endif
