#ifndef PDM_BOARD_PROFILE_H
#define PDM_BOARD_PROFILE_H
#include <stdbool.h>
#include <stdint.h>

/* This selects the real hardware binding, never the simulator's data model. */
#ifndef PDM_BOARD_PROFILE
#define PDM_BOARD_PROFILE 14
#endif
#if PDM_BOARD_PROFILE != 14
#error "Only Synapse14 hardware is supported; PDM24 needs assigned pins and HIL approval"
#endif
#define PDM_BOARD_HARDWARE_CHANNEL_COUNT 14u
#define PDM_BOARD_MAX_CHANNELS 24u
#define PDM_BOARD_MAX_GROUPS 24u
#define PDM_BOARD_UNASSIGNED 255u

typedef enum { PDM_PORT_A, PDM_PORT_B, PDM_PORT_C, PDM_PORT_D,
    PDM_PORT_E, PDM_PORT_F, PDM_PORT_G, PDM_PORT_H, PDM_PORT_I } PDM_BoardPort;
typedef struct { uint8_t port, pin; } PDM_BoardPin;
typedef enum { PDM_CAPABILITY_UNCOMMISSIONED, PDM_CAPABILITY_START_TARGET,
    PDM_CAPABILITY_CONTINUOUS_TARGET } PDM_BoardCapability;

typedef struct {
    uint8_t number;                 /* Logical output number, 1..channel_count. */
    PDM_BoardPin gate, current;
    uint8_t adc_channel;            /* ADC1 channel, or UNASSIGNED. */
    uint8_t budget_group;           /* UNASSIGNED until load/group design. */
    uint8_t safe_level;             /* Always zero; no active-low gate binding. */
    PDM_BoardCapability capability;
    /* Design targets only: NEVER protection thresholds or admission budgets. */
    uint32_t target_peak_ma;
    uint32_t target_continuous_min_ma, target_continuous_max_ma;
} PDM_BoardChannel;

typedef struct {
    uint16_t id;                    /* 14 = Synapse14, 24 = proposed R2 PDM24. */
    uint16_t schema_version;
    uint8_t channel_count, group_count;
    bool simulation_only;
    /* No commissioned limits/calibration or actuation authorization here. */
    PDM_BoardChannel channels[PDM_BOARD_MAX_CHANNELS];
} PDM_BoardProfile;

/* Immutable built-in descriptors. Unknown identifiers return NULL. */
const PDM_BoardProfile *PDM_BoardProfile_Get(uint16_t id);
const PDM_BoardProfile *PDM_BoardProfile_Hardware(void);
/* Structural validation is NOT approval to actuate. Hardware accepts only the
 * exact existing Synapse14 gate/current map. Profile24 is simulation-only. */
bool PDM_BoardProfile_Validate(const PDM_BoardProfile *profile, bool hardware);
#endif
