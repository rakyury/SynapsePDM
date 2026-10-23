#include "pdm_board_profile.h"
#include <stddef.h>

#define PIN(p, n) {PDM_PORT_##p, n}
#define NONE {PDM_BOARD_UNASSIGNED, PDM_BOARD_UNASSIGNED}
#define OLD(n, gp, gn, cp, cn, adc) \
    {n, PIN(gp, gn), PIN(cp, cn), adc, PDM_BOARD_UNASSIGNED, 0, \
     PDM_CAPABILITY_UNCOMMISSIONED, 0, 0, 0}
/* Exact OutputHandler gate order and pdm_adc_hal current scan order. Battery
 * PC4/ADC14 and internal VREF/temperature remain in the unchanged ADC pipeline. */
static const PDM_BoardProfile synapse14 = {
    .id = 14, .schema_version = 1, .channel_count = 14,
    .group_count = 0, .simulation_only = false,
    .channels = {
        OLD(1,G,10,A,0,0), OLD(2,G,9,A,1,1), OLD(3,G,6,A,2,2),
        OLD(4,G,5,A,3,3), OLD(5,G,4,A,4,4), OLD(6,G,3,A,5,5),
        OLD(7,G,2,A,6,6), OLD(8,F,15,B,1,9), OLD(9,F,14,B,0,8),
        OLD(10,F,13,A,7,7), OLD(11,F,12,C,3,13), OLD(12,F,2,C,2,12),
        OLD(13,F,1,C,1,11), OLD(14,F,0,C,0,10)
    }
};
#define FUTURE(n, cap, peak, low, high) \
    {n, NONE, NONE, PDM_BOARD_UNASSIGNED, PDM_BOARD_UNASSIGNED, 0, cap, peak, low, high}
#define PEAK(n) FUTURE(n, PDM_CAPABILITY_START_TARGET, 30000, 0, 0)
#define STANDARD(n) FUTURE(n, PDM_CAPABILITY_CONTINUOUS_TARGET, 0, 17000, 20000)
static const PDM_BoardProfile pdm24 = {
    .id = 24, .schema_version = 1, .channel_count = 24,
    .group_count = 0, .simulation_only = true,
    .channels = {
        PEAK(1), PEAK(2), PEAK(3), PEAK(4), PEAK(5), PEAK(6), PEAK(7), PEAK(8), PEAK(9), PEAK(10),
        STANDARD(11), STANDARD(12), STANDARD(13), STANDARD(14), STANDARD(15), STANDARD(16),
        STANDARD(17), STANDARD(18), STANDARD(19), STANDARD(20), STANDARD(21), STANDARD(22), STANDARD(23), STANDARD(24)
    }
};

const PDM_BoardProfile *PDM_BoardProfile_Get(uint16_t id)
{ return id == 14 ? &synapse14 : id == 24 ? &pdm24 : NULL; }

const PDM_BoardProfile *PDM_BoardProfile_Hardware(void)
{ return &synapse14; }

static bool unassigned(PDM_BoardPin pin)
{ return pin.port == PDM_BOARD_UNASSIGNED && pin.pin == PDM_BOARD_UNASSIGNED; }
static bool valid_pin(PDM_BoardPin pin)
{ return unassigned(pin) || (pin.port <= PDM_PORT_I && pin.pin < 16u); }
static bool same_pin(PDM_BoardPin a, PDM_BoardPin b)
{ return a.port == b.port && a.pin == b.pin; }
static bool collision(PDM_BoardPin a, PDM_BoardPin b)
{ return !unassigned(a) && !unassigned(b) && same_pin(a, b); }
static uint8_t current_adc(PDM_BoardPin pin)
{
    if (pin.port == PDM_PORT_A && pin.pin <= 7) return pin.pin;
    if (pin.port == PDM_PORT_B && pin.pin <= 1) return (uint8_t)(8u + pin.pin);
    if (pin.port == PDM_PORT_C && pin.pin <= 5) return (uint8_t)(10u + pin.pin);
    return PDM_BOARD_UNASSIGNED;
}

bool PDM_BoardProfile_Validate(const PDM_BoardProfile *p, bool hardware)
{
    if (p == NULL || p->schema_version != 1 ||
        (p->id != 14 && p->id != 24) || p->channel_count != p->id ||
        p->group_count > PDM_BOARD_MAX_GROUPS ||
        (p->id == 24 && !p->simulation_only) ||
        (hardware && (p->id != 14 || p->simulation_only))) return false;
    uint32_t seen = 0;
    for (unsigned i = 0; i < p->channel_count; ++i) {
        const PDM_BoardChannel *c = &p->channels[i];
        if (c->number == 0 || c->number > p->channel_count || c->safe_level != 0 ||
            (c->budget_group != PDM_BOARD_UNASSIGNED && c->budget_group >= p->group_count) ||
            !valid_pin(c->gate) || !valid_pin(c->current)) return false;
        const uint32_t bit = 1u << (c->number - 1u);
        if (seen & bit) return false;
        seen |= bit;
        if (p->id == 24) {
            if (!unassigned(c->gate) || !unassigned(c->current) ||
                c->adc_channel != PDM_BOARD_UNASSIGNED) return false;
        } else if (unassigned(c->gate) || unassigned(c->current) ||
                   c->adc_channel == PDM_BOARD_UNASSIGNED ||
                   c->adc_channel != current_adc(c->current)) return false;
        const PDM_BoardChannel *factory = &PDM_BoardProfile_Get(p->id)->channels[c->number - 1u];
        if (c->capability != factory->capability || c->target_peak_ma != factory->target_peak_ma ||
            c->target_continuous_min_ma != factory->target_continuous_min_ma ||
            c->target_continuous_max_ma != factory->target_continuous_max_ma) return false;
        if (hardware && (c->number != i + 1u || !same_pin(c->gate, factory->gate) ||
            !same_pin(c->current, factory->current) || c->adc_channel != factory->adc_channel)) return false;
        if (collision(c->gate, c->current)) return false;
        for (unsigned j = 0; j < i; ++j) {
            const PDM_BoardChannel *other = &p->channels[j];
            if (collision(c->gate, other->gate) || collision(c->gate, other->current) ||
                collision(c->current, other->gate) || collision(c->current, other->current)) return false;
        }
    }
    return true;
}
