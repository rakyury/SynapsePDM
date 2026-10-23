#ifndef PDM_START_TELEMETRY_H
#define PDM_START_TELEMETRY_H
#include "pdm_link.h"
#include "pdm_board_profile.h"
#include "pdm_start_admission.h"
#define PDM_START_TELEMETRY_MAX_PAYLOAD 408u
#define PDM_START_TELEMETRY_PERIOD_MS 1000u
#define PDM_START_TELEMETRY_DEADLINE_MS 1500u
typedef struct PDM_StartTelemetry {
    const PDM_BoardProfile *profile;
    const PDM_StartAdmission *scheduler;
    const PDM_StartInputs *inputs;
    bool simulation, started, active, frame_ready;
    uint32_t sequence, captured_ms, crc;
    uint16_t length, frame_index;
    uint8_t begin[8], payload[PDM_START_TELEMETRY_MAX_PAYLOAD];
    PDM_LinkFrame frame;
} PDM_StartTelemetry;
/* Foreground capture only. Hardware always passes NULL scheduler/inputs and
 * simulation=false: no configuration, zero-current measurement or ON is invented. */
void PDM_StartTelemetry_Init(PDM_StartTelemetry *t, const PDM_BoardProfile *profile,
    const PDM_StartAdmission *scheduler, const PDM_StartInputs *inputs, bool simulation);
void PDM_StartTelemetry_Tick(PDM_StartTelemetry *t, uint32_t now_ms);
const PDM_LinkFrame *PDM_StartTelemetry_Peek(const PDM_StartTelemetry *t);
void PDM_StartTelemetry_Pop(PDM_StartTelemetry *t);
#endif
