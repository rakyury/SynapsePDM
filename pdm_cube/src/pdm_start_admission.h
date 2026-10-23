#ifndef PDM_START_ADMISSION_H
#define PDM_START_ADMISSION_H
#include <stdbool.h>
#include <stdint.h>
#define PDM_START_MAX_CHANNELS 24u
#define PDM_START_MAX_GROUPS 24u
typedef enum { PDM_START_OFF, PDM_START_QUEUED, PDM_START_STARTING,
               PDM_START_ON, PDM_START_FAULT } PDM_StartState;
typedef enum { PDM_START_OK, PDM_START_BAD_CONFIG, PDM_START_BAD_CHANNEL,
    PDM_START_OLD_REQUEST, PDM_START_EXPIRED, PDM_START_UNHEALTHY,
    PDM_START_UNKNOWN_OFF, PDM_START_SLOTS, PDM_START_TOTAL_BUDGET,
    PDM_START_GROUP_BUDGET, PDM_START_COOLDOWN, PDM_START_DEADLINE,
    PDM_START_FAULT_LATCHED, PDM_START_CANCELLED } PDM_StartReason;
typedef struct {
    uint32_t continuous_mA, start_mA, start_duration_ms, cooldown_ms;
    uint8_t group, priority; /* 0 highest ..3 lowest; FIFO within priority. */
    bool commissioned;
} PDM_StartChannelConfig;
typedef struct {
    uint8_t channel_count, group_count, max_starts;
    uint32_t total_budget_mA, base_load_mA, request_ttl_ms, feedback_ttl_ms;
    uint32_t group_budget_mA[PDM_START_MAX_GROUPS];
    uint32_t group_base_mA[PDM_START_MAX_GROUPS];
    PDM_StartChannelConfig channels[PDM_START_MAX_CHANNELS];
} PDM_StartConfig;
typedef struct {
    bool valid, healthy, confirmed_off, start_complete;
    uint32_t sampled_ms, measured_mA;
} PDM_StartFeedback;
typedef struct {
    bool valid, healthy, inhibited, link_healthy;
    uint32_t sampled_ms, total_measured_mA;
    bool group_healthy[PDM_START_MAX_GROUPS];
    PDM_StartFeedback channels[PDM_START_MAX_CHANNELS];
} PDM_StartInputs;
typedef struct {
    PDM_StartState state;
    PDM_StartReason reason;
    uint32_t request_id, requested_ms, started_ms, last_off_ms, event_ms;
    uint32_t reserve_mA;
    bool request_seen, ever_started, slot_reserved, off_confirmation_required;
} PDM_StartChannel;
typedef struct {
    bool valid;
    PDM_StartConfig config;
    PDM_StartChannel channels[PDM_START_MAX_CHANNELS];
    uint8_t queue[PDM_START_MAX_CHANNELS], queue_count;
    uint8_t starts_reserved;
    uint32_t total_reserved_mA;
    uint32_t group_reserved_mA[PDM_START_MAX_GROUPS];
    uint32_t reset_ms;
    bool reset_seen;
} PDM_StartAdmission;

/* Simulation policy only: no GPIO, HAL, output binding or actuator callback.
 * IDs are zero-based channel indices. All time horizons must be 1..INT32_MAX.
 * Request IDs use serial-number ordering; duplicates never refresh queue TTL.
 * Inputs must describe every channel, including confirmed OFF for idle channels.
 * OFF proof here requires measured_mA==0. This is an explicit simulator proof,
 * never inferred from a gate bit or the legacy IS-pin ADC estimate. Independent
 * total current above the reserved envelope vetoes admission as unmodeled load.
 * Config is copied and immutable until a new Init (only after known shutdown). */
bool PDM_Start_Init(PDM_StartAdmission *s, const PDM_StartConfig *config);
PDM_StartReason PDM_Start_Request(PDM_StartAdmission *s, uint8_t channel,
    uint32_t request_id, uint32_t requested_ms, uint32_t now_ms);
void PDM_Start_Step(PDM_StartAdmission *s, const PDM_StartInputs *inputs, uint32_t now_ms);
void PDM_Start_Cancel(PDM_StartAdmission *s, uint8_t channel, uint32_t now_ms);
/* Fault/cancel/reset retain active reservation until a fresh confirmed OFF.
 * Reset discards all pending authorization; fault recovery still needs ClearFault
 * then a newer explicit request. No automatically retried starts. */
void PDM_Start_Reset(PDM_StartAdmission *s, uint32_t now_ms);
bool PDM_Start_ClearFault(PDM_StartAdmission *s, uint8_t channel,
    const PDM_StartInputs *inputs, uint32_t now_ms);
#endif
