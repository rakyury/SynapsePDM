#include "pdm_output_safety.h"
#include <stddef.h>

static void enter(PDM_OutputSafety *s, PDM_OutputState state, uint32_t now)
{
    s->state = state;
    s->entered_ms = now;
}

void PDM_OutputSafety_Init(PDM_OutputSafety *s, uint32_t now_ms)
{
    if (s == NULL) return;
    *s = (PDM_OutputSafety){0};
    enter(s, PDM_OUTPUT_OFF, now_ms);
}

void PDM_OutputSafety_Step(PDM_OutputSafety *s, const PDM_OutputPolicy *p,
                           const PDM_OutputInputs *in, uint32_t now)
{
    uint32_t faults = 0;
    bool reset_edge;
    if (s == NULL) return;
    /* Elapsed arithmetic is wrap-safe with calls less than 2^31 ms apart. */
    if (p == NULL || in == NULL || p->startup_ms > INT32_MAX ||
        p->retry_cooldown_ms > INT32_MAX ||
        (p->max_retries != 0 && p->retry_cooldown_ms == 0) ||
        s->state < PDM_OUTPUT_OFF || s->state > PDM_OUTPUT_LOCKOUT) {
        s->latched_faults |= PDM_FAULT_POLICY_INVALID;
        enter(s, PDM_OUTPUT_LOCKOUT, now);
        return;
    }
    reset_edge = in->reset && !s->reset_previous;
    s->reset_previous = in->reset;
    if (in->overcurrent) faults |= PDM_FAULT_OVERCURRENT;
    if (!in->current_valid) faults |= PDM_FAULT_CURRENT_INVALID;
    if (!in->vbatt_valid) faults |= PDM_FAULT_VBATT_INVALID;
    if (in->emergency) faults |= PDM_FAULT_EMERGENCY;
    if (!in->command_valid) faults |= PDM_FAULT_COMMAND_LOST;
    s->latched_faults |= faults;

    /* Fault override is evaluated before demand, reset or startup progress. */
    if (faults != 0) {
        if (s->state == PDM_OUTPUT_LOCKOUT) return;
        if ((faults & PDM_FAULT_EMERGENCY) != 0 || s->retries >= p->max_retries) {
            enter(s, PDM_OUTPUT_LOCKOUT, now);
        } else if (s->state != PDM_OUTPUT_FAULT_WAIT) {
            enter(s, PDM_OUTPUT_FAULT_WAIT, now);
        }
        return;
    }
    /* Invalid policy is non-resettable: reinitialization requires review. */
    if ((s->latched_faults & PDM_FAULT_POLICY_INVALID) != 0) return;
    if (reset_edge && !in->demand &&
        (s->state == PDM_OUTPUT_LOCKOUT || s->state == PDM_OUTPUT_FAULT_WAIT)) {
        s->latched_faults = 0;
        s->retries = 0;
        enter(s, PDM_OUTPUT_OFF, now);
        return;
    }
    switch (s->state) {
    case PDM_OUTPUT_LOCKOUT:
        return;
    case PDM_OUTPUT_FAULT_WAIT:
        if (in->demand && (uint32_t)(now - s->entered_ms) >= p->retry_cooldown_ms) {
            if (s->retries >= p->max_retries) {
                enter(s, PDM_OUTPUT_LOCKOUT, now);
                return;
            }
            ++s->retries;
            enter(s, p->startup_ms == 0 ? PDM_OUTPUT_ON : PDM_OUTPUT_STARTING, now);
        }
        return;
    case PDM_OUTPUT_OFF:
        if (in->demand)
            enter(s, p->startup_ms == 0 ? PDM_OUTPUT_ON : PDM_OUTPUT_STARTING, now);
        return;
    case PDM_OUTPUT_STARTING:
    case PDM_OUTPUT_ON:
        if (!in->demand) enter(s, PDM_OUTPUT_OFF, now);
        else if (s->state == PDM_OUTPUT_STARTING &&
                 (uint32_t)(now - s->entered_ms) >= p->startup_ms)
            enter(s, PDM_OUTPUT_ON, now);
        return;
    }
}
