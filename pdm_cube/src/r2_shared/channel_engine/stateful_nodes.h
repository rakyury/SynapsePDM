/**
 * @file stateful_nodes.h
 * @brief Additional stateful processing nodes
 *
 * Phase 8 implementation:
 * - Edge Detect (Rising/Falling/Both)
 * - Latch (SR Flip-Flop)
 * - Counter (Up/Down with preset/reset)
 * - Min/Max Hold (Peak/Valley detection)
 * - Hysteresis (Schmitt trigger)
 * - Derivative (d/dt - rate of change)
 * - Integral (∫dt with reset condition)
 * - Rate Limiter (Δ/s limiter)
 */

#ifndef STATEFUL_NODES_H
#define STATEFUL_NODES_H

#include <stdint.h>
#include <stdbool.h>
#include "channel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Edge Detect
 * ============================================================================ */

typedef enum {
    EDGE_RISING  = 0,
    EDGE_FALLING = 1,
    EDGE_BOTH    = 2,
} EdgeType_t;

typedef struct {
    uint16_t id;
    uint16_t input_channel;
    uint16_t output_channel;
    uint8_t  edge_type;         /* EdgeType_t */
    uint8_t  flags;

    /* Runtime */
    int32_t  prev_value;
    bool     initialized;

    char name[24];
} EdgeDetect_t;

#define EDGE_FLAG_ENABLED   (1 << 0)
#define EDGE_FLAG_BOOL_MODE (1 << 1) /* Compare as bool (!=0) */

/* ============================================================================
 * Latch (SR Flip-Flop)
 * ============================================================================ */

typedef struct {
    uint16_t id;
    uint16_t set_channel;       /* Set input */
    uint16_t reset_channel;     /* Reset input */
    uint16_t output_channel;
    uint8_t  flags;
    uint8_t  priority;          /* 0=reset dominant, 1=set dominant */

    /* Runtime */
    bool     state;
    bool     prev_set;          /* edge-triggered set / reset: last inputs */
    bool     prev_reset;

    char name[24];
} Latch_t;

#define LATCH_FLAG_ENABLED      (1 << 0)
#define LATCH_FLAG_EDGE_SET     (1 << 1) /* Trigger on edge, not level */
#define LATCH_FLAG_EDGE_RESET   (1 << 2)

/* ============================================================================
 * Counter
 * ============================================================================ */

typedef enum {
    COUNTER_UP   = 0,
    COUNTER_DOWN = 1,
    COUNTER_UPDOWN = 2,
} CounterMode_t;

typedef struct {
    uint16_t id;
    uint16_t count_channel;     /* Pulse input */
    uint16_t reset_channel;     /* Reset input (optional) */
    uint16_t direction_channel; /* For UPDOWN mode */
    uint16_t output_channel;
    uint16_t done_channel;      /* Outputs 1 when count == preset */

    int32_t  preset;            /* Target value */
    int32_t  min_value;         /* Minimum bound */
    int32_t  max_value;         /* Maximum bound */

    uint8_t  mode;              /* CounterMode_t */
    uint8_t  flags;

    /* Runtime */
    int32_t  count;
    bool     prev_count_input;
    bool     prev_reset_input;

    char name[24];
} Counter_t;

#define COUNTER_FLAG_ENABLED    (1 << 0)
#define COUNTER_FLAG_WRAP       (1 << 1) /* Wrap at bounds */
#define COUNTER_FLAG_CLAMP      (1 << 2) /* Clamp at bounds */
#define COUNTER_FLAG_AUTORESET  (1 << 3) /* Auto-reset when preset reached */

/* ============================================================================
 * Min/Max Hold (Peak/Valley Detection)
 * ============================================================================ */

typedef enum {
    HOLD_MIN = 0,
    HOLD_MAX = 1,
    HOLD_BOTH = 2,
} HoldMode_t;

typedef struct {
    uint16_t id;
    uint16_t input_channel;
    uint16_t reset_channel;     /* Reset to current value */
    uint16_t min_output;        /* Min value output */
    uint16_t max_output;        /* Max value output */

    uint8_t  mode;              /* HoldMode_t */
    uint8_t  flags;

    /* Runtime */
    int32_t  min_value;
    int32_t  max_value;
    bool     initialized;

    char name[24];
} MinMaxHold_t;

#define HOLD_FLAG_ENABLED       (1 << 0)
#define HOLD_FLAG_RESET_TO_ZERO (1 << 1) /* Reset to 0 instead of current */

/* ============================================================================
 * Hysteresis
 * ============================================================================ */

typedef struct {
    uint16_t id;
    uint16_t input_channel;
    uint16_t output_channel;

    int32_t  threshold_high;    /* Turn ON threshold */
    int32_t  threshold_low;     /* Turn OFF threshold */
    uint16_t ref_channel;       /* HYST_FLAG_REF: both thresholds are offsets from this
                                   channel's value (a bar limit driven by a channel) */

    uint8_t  flags;

    /* Runtime */
    bool     state;

    char name[24];
} Hysteresis_t;

#define HYST_FLAG_ENABLED       (1 << 0)
#define HYST_FLAG_INVERT        (1 << 1)
#define HYST_FLAG_REF           (1 << 2)   /* thresholds relative to ref_channel */

/* ============================================================================
 * Derivative (d/dt)
 * ============================================================================ */

typedef struct {
    uint16_t id;
    uint16_t input_channel;
    uint16_t output_channel;    /* Rate of change per second */

    uint8_t  flags;
    uint8_t  smoothing;         /* EMA alpha for smoothing (0=disabled) */

    /* Runtime */
    int32_t  prev_value;
    int64_t  smoothed_rate;     /* Scaled by 256 */
    bool     initialized;

    char name[24];
} Derivative_t;

#define DERIV_FLAG_ENABLED      (1 << 0)
#define DERIV_FLAG_ABS          (1 << 1) /* Output absolute value */

/* ============================================================================
 * Integral (∫dt)
 * ============================================================================ */

typedef struct {
    uint16_t id;
    uint16_t input_channel;
    uint16_t reset_channel;
    uint16_t output_channel;

    int32_t  min_value;         /* Clamp minimum */
    int32_t  max_value;         /* Clamp maximum */

    uint8_t  flags;

    /* Runtime */
    int64_t  accumulator;       /* Scaled accumulator */

    char name[24];
} Integral_t;

#define INTEG_FLAG_ENABLED      (1 << 0)
#define INTEG_FLAG_CLAMP        (1 << 1)
#define INTEG_FLAG_POSITIVE_ONLY (1 << 2) /* Only accumulate positive values */

/* ============================================================================
 * Rate Limiter
 * ============================================================================ */

typedef struct {
    uint16_t id;
    uint16_t input_channel;
    uint16_t output_channel;

    int32_t  max_rise_per_sec;  /* Maximum increase per second */
    int32_t  max_fall_per_sec;  /* Maximum decrease per second */

    uint8_t  flags;

    /* Runtime */
    int32_t  current_value;
    bool     initialized;

    char name[24];
} RateLimiter_t;

#define RATE_FLAG_ENABLED       (1 << 0)

/* ============================================================================
 * Stateful Node Manager
 * ============================================================================ */

#ifndef MAX_EDGE_DETECTS
#define MAX_EDGE_DETECTS 8 /* F446 resource profile */
#endif
#ifndef MAX_LATCHES
#define MAX_LATCHES 8 /* F446 resource profile */
#endif
/* ============================================================================
 * Setting (user-adjustable persistent value, e.g. dash menu items)
 * ============================================================================ */

typedef struct {
    uint16_t id;
    uint16_t inc_channel;       /* Rising edge increments by step */
    uint16_t dec_channel;       /* Rising edge decrements (optional) */
    uint16_t enable_channel;    /* Adjust only while non-zero (optional) */
    uint16_t output_channel;

    int32_t  min_value;
    int32_t  max_value;
    int32_t  step;
    int32_t  initial;

    uint8_t  flags;

    /* Runtime */
    int32_t  value;
    bool     prev_inc;
    bool     prev_dec;
    bool     initialized;
} Setting_t;

typedef struct {
    uint16_t id;
    uint16_t trigger_channel;   /* Re-roll on rising edge (0/0xFFFF =
                                   every tick) */
    uint16_t output_channel;
    int32_t  min_value;
    int32_t  max_value;
    uint32_t seed;

    uint8_t  flags;

    /* Runtime */
    uint32_t state;
    bool     prev_trigger;
    bool     initialized;
} RandomNode_t;

#define RANDOM_FLAG_ENABLED     (1 << 0)

#define SETTING_FLAG_ENABLED    (1 << 0)
#define SETTING_FLAG_WRAP       (1 << 1) /* Wrap at bounds (else clamp) */

#ifndef MAX_COUNTERS
#define MAX_COUNTERS 8 /* F446 resource profile */
#endif
#ifndef MAX_SETTINGS
#define MAX_SETTINGS 8 /* F446 resource profile */
#endif
#ifndef MAX_RANDOMS
#define MAX_RANDOMS 4 /* F446 resource profile */
#endif
#ifndef MAX_MINMAX_HOLDS
#define MAX_MINMAX_HOLDS 4 /* F446 resource profile */
#endif
#ifndef MAX_HYSTERESIS
#define MAX_HYSTERESIS 8 /* F446 resource profile */
#endif
#ifndef MAX_DERIVATIVES
#define MAX_DERIVATIVES 4 /* F446 resource profile */
#endif
#ifndef MAX_INTEGRALS
#define MAX_INTEGRALS 4 /* F446 resource profile */
#endif
#ifndef MAX_RATE_LIMITERS
#define MAX_RATE_LIMITERS 4 /* F446 resource profile */
#endif

typedef struct {
    EdgeDetect_t   edges[MAX_EDGE_DETECTS];
    uint16_t       edge_count;

    Latch_t        latches[MAX_LATCHES];
    uint16_t       latch_count;

    Counter_t      counters[MAX_COUNTERS];
    uint16_t       counter_count;

    Setting_t      settings[MAX_SETTINGS];
    uint16_t       setting_count;

    RandomNode_t   randoms[MAX_RANDOMS];
    uint16_t       random_count;

    MinMaxHold_t   holds[MAX_MINMAX_HOLDS];
    uint16_t       hold_count;

    Hysteresis_t   hysts[MAX_HYSTERESIS];
    uint16_t       hyst_count;

    Derivative_t   derivs[MAX_DERIVATIVES];
    uint16_t       deriv_count;

    Integral_t     integrals[MAX_INTEGRALS];
    uint16_t       integral_count;

    RateLimiter_t  limiters[MAX_RATE_LIMITERS];
    uint16_t       limiter_count;

    ChannelEngine_t* channels;
    uint32_t       dt_ms;

    bool initialized;
} StatefulManager_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

void Stateful_Init(StatefulManager_t* mgr, ChannelEngine_t* channels);
void Stateful_Reset(StatefulManager_t* mgr);
void Stateful_UpdateAll(StatefulManager_t* mgr, uint32_t dt_ms);

/* Add nodes */
int Stateful_AddEdgeDetect(StatefulManager_t* mgr, const EdgeDetect_t* cfg);
int Stateful_AddLatch(StatefulManager_t* mgr, const Latch_t* cfg);
int Stateful_AddCounter(StatefulManager_t* mgr, const Counter_t* cfg);
int Stateful_AddSetting(StatefulManager_t* mgr, const Setting_t* cfg);
int Stateful_AddRandom(StatefulManager_t* mgr, const RandomNode_t* cfg);
int Stateful_AddMinMaxHold(StatefulManager_t* mgr, const MinMaxHold_t* cfg);
int Stateful_AddHysteresis(StatefulManager_t* mgr, const Hysteresis_t* cfg);
int Stateful_AddDerivative(StatefulManager_t* mgr, const Derivative_t* cfg);
int Stateful_AddIntegral(StatefulManager_t* mgr, const Integral_t* cfg);
int Stateful_AddRateLimiter(StatefulManager_t* mgr, const RateLimiter_t* cfg);

/* Reset specific node */
void Stateful_ResetCounter(StatefulManager_t* mgr, uint16_t id);
void Stateful_ResetMinMax(StatefulManager_t* mgr, uint16_t id);
void Stateful_ResetIntegral(StatefulManager_t* mgr, uint16_t id);

/* Session values: every min/max hold restarts from the current input and
 * every integral from zero (ACTION_RESET_SESSION, ignition-on later) */
void Stateful_ResetSession(StatefulManager_t* mgr);

#ifdef __cplusplus
}
#endif

#endif /* STATEFUL_NODES_H */
