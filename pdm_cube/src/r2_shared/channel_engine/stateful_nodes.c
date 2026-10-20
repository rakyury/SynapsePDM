/**
 * @file stateful_nodes.c
 * @brief Stateful processing nodes implementation
 */

#include "stateful_nodes.h"
#include <string.h>

/* ============================================================================
 * Initialization
 * ============================================================================ */

void Stateful_Init(StatefulManager_t* mgr, ChannelEngine_t* channels)
{
    if (!mgr) return;

    memset(mgr, 0, sizeof(StatefulManager_t));
    mgr->channels = channels;
    mgr->initialized = true;
}

void Stateful_Reset(StatefulManager_t* mgr)
{
    if (!mgr) return;

    for (uint16_t i = 0; i < mgr->edge_count; i++) {
        mgr->edges[i].initialized = false;
    }

    for (uint16_t i = 0; i < mgr->latch_count; i++) {
        mgr->latches[i].state = false;
    }

    for (uint16_t i = 0; i < mgr->counter_count; i++) {
        mgr->counters[i].count = 0;
        mgr->counters[i].prev_count_input = false;
        mgr->counters[i].prev_reset_input = false;
    }
    for (uint16_t i = 0; i < mgr->setting_count; i++) {
        mgr->settings[i].initialized = false;
        mgr->settings[i].prev_inc = false;
        mgr->settings[i].prev_dec = false;
    }
    for (uint16_t i = 0; i < mgr->random_count; i++) {
        mgr->randoms[i].initialized = false;
        mgr->randoms[i].prev_trigger = false;
    }

    for (uint16_t i = 0; i < mgr->hold_count; i++) {
        mgr->holds[i].initialized = false;
    }

    for (uint16_t i = 0; i < mgr->hyst_count; i++) {
        mgr->hysts[i].state = false;
    }

    for (uint16_t i = 0; i < mgr->deriv_count; i++) {
        mgr->derivs[i].initialized = false;
    }

    for (uint16_t i = 0; i < mgr->integral_count; i++) {
        mgr->integrals[i].accumulator = 0;
    }

    for (uint16_t i = 0; i < mgr->limiter_count; i++) {
        mgr->limiters[i].initialized = false;
    }
}

/* ============================================================================
 * Add Nodes
 * ============================================================================ */

int Stateful_AddEdgeDetect(StatefulManager_t* mgr, const EdgeDetect_t* cfg)
{
    if (!mgr || !cfg || mgr->edge_count >= MAX_EDGE_DETECTS) return -1;
    memcpy(&mgr->edges[mgr->edge_count], cfg, sizeof(EdgeDetect_t));
    return mgr->edge_count++;
}

int Stateful_AddLatch(StatefulManager_t* mgr, const Latch_t* cfg)
{
    if (!mgr || !cfg || mgr->latch_count >= MAX_LATCHES) return -1;
    memcpy(&mgr->latches[mgr->latch_count], cfg, sizeof(Latch_t));
    return mgr->latch_count++;
}

int Stateful_AddCounter(StatefulManager_t* mgr, const Counter_t* cfg)
{
    if (!mgr || !cfg || mgr->counter_count >= MAX_COUNTERS) return -1;
    memcpy(&mgr->counters[mgr->counter_count], cfg, sizeof(Counter_t));
    return mgr->counter_count++;
}

int Stateful_AddSetting(StatefulManager_t* mgr, const Setting_t* cfg)
{
    if (!mgr || !cfg || mgr->setting_count >= MAX_SETTINGS) return -1;
    Setting_t* s = &mgr->settings[mgr->setting_count];
    memcpy(s, cfg, sizeof(Setting_t));
    /* step 0 never moved the value on the panel while the preview
     * (engine_host.c) stepped by 1: both use 1 now */
    if (s->step == 0) s->step = 1;
    return mgr->setting_count++;
}

int Stateful_AddRandom(StatefulManager_t* mgr, const RandomNode_t* cfg)
{
    if (!mgr || !cfg || mgr->random_count >= MAX_RANDOMS) return -1;
    memcpy(&mgr->randoms[mgr->random_count], cfg, sizeof(RandomNode_t));
    return mgr->random_count++;
}

int Stateful_AddMinMaxHold(StatefulManager_t* mgr, const MinMaxHold_t* cfg)
{
    if (!mgr || !cfg || mgr->hold_count >= MAX_MINMAX_HOLDS) return -1;
    memcpy(&mgr->holds[mgr->hold_count], cfg, sizeof(MinMaxHold_t));
    return mgr->hold_count++;
}

int Stateful_AddHysteresis(StatefulManager_t* mgr, const Hysteresis_t* cfg)
{
    if (!mgr || !cfg || mgr->hyst_count >= MAX_HYSTERESIS) return -1;
    memcpy(&mgr->hysts[mgr->hyst_count], cfg, sizeof(Hysteresis_t));
    return mgr->hyst_count++;
}

int Stateful_AddDerivative(StatefulManager_t* mgr, const Derivative_t* cfg)
{
    if (!mgr || !cfg || mgr->deriv_count >= MAX_DERIVATIVES) return -1;
    memcpy(&mgr->derivs[mgr->deriv_count], cfg, sizeof(Derivative_t));
    return mgr->deriv_count++;
}

int Stateful_AddIntegral(StatefulManager_t* mgr, const Integral_t* cfg)
{
    if (!mgr || !cfg || mgr->integral_count >= MAX_INTEGRALS) return -1;
    memcpy(&mgr->integrals[mgr->integral_count], cfg, sizeof(Integral_t));
    return mgr->integral_count++;
}

int Stateful_AddRateLimiter(StatefulManager_t* mgr, const RateLimiter_t* cfg)
{
    if (!mgr || !cfg || mgr->limiter_count >= MAX_RATE_LIMITERS) return -1;
    memcpy(&mgr->limiters[mgr->limiter_count], cfg, sizeof(RateLimiter_t));
    return mgr->limiter_count++;
}

/* ============================================================================
 * Update: Edge Detect
 * ============================================================================ */

static void update_edge_detects(StatefulManager_t* mgr)
{
    for (uint16_t i = 0; i < mgr->edge_count; i++) {
        EdgeDetect_t* e = &mgr->edges[i];
        if (!(e->flags & EDGE_FLAG_ENABLED)) continue;

        int32_t val = Channel_GetValue(mgr->channels, e->input_channel);
        int32_t output = 0;

        if (e->initialized) {
            bool curr, prev;

            if (e->flags & EDGE_FLAG_BOOL_MODE) {
                curr = val != 0;
                prev = e->prev_value != 0;
            } else {
                curr = val > 0;
                prev = e->prev_value > 0;
            }

            switch ((EdgeType_t)e->edge_type) {
                case EDGE_RISING:
                    output = (!prev && curr) ? 1 : 0;
                    break;
                case EDGE_FALLING:
                    output = (prev && !curr) ? 1 : 0;
                    break;
                case EDGE_BOTH:
                    output = (prev != curr) ? 1 : 0;
                    break;
            }
        }

        e->prev_value = val;
        e->initialized = true;

        Channel_SetValue(mgr->channels, e->output_channel, output);
    }
}

/* ============================================================================
 * Update: Latches
 * ============================================================================ */

static void update_latches(StatefulManager_t* mgr)
{
    for (uint16_t i = 0; i < mgr->latch_count; i++) {
        Latch_t* l = &mgr->latches[i];
        if (!(l->flags & LATCH_FLAG_ENABLED)) continue;

        bool set_val = Channel_GetValue(mgr->channels, l->set_channel) != 0;
        bool reset_val = Channel_GetValue(mgr->channels, l->reset_channel) != 0;

        bool set_trigger = set_val;
        bool reset_trigger = reset_val;

        if (l->flags & LATCH_FLAG_EDGE_SET) {
            set_trigger = set_val && !l->prev_set;
        }
        if (l->flags & LATCH_FLAG_EDGE_RESET) {
            reset_trigger = reset_val && !l->prev_reset;
        }

        /* per latch, not function statics: those were shared by every
         * StatefulManager_t and survived Stateful_Init, so a reloaded
         * config (or the preview's next Live start) saw the previous
         * engine's inputs and missed the first edge */
        l->prev_set = set_val;
        l->prev_reset = reset_val;

        if (set_trigger && reset_trigger) {
            l->state = (l->priority == 1);
        } else if (set_trigger) {
            l->state = true;
        } else if (reset_trigger) {
            l->state = false;
        }

        Channel_SetValue(mgr->channels, l->output_channel, l->state ? 1 : 0);
    }
}

/* ============================================================================
 * Update: Counters
 * ============================================================================ */

static void update_counters(StatefulManager_t* mgr)
{
    for (uint16_t i = 0; i < mgr->counter_count; i++) {
        Counter_t* c = &mgr->counters[i];
        if (!(c->flags & COUNTER_FLAG_ENABLED)) continue;

        bool count_val = Channel_GetValue(mgr->channels, c->count_channel) != 0;
        bool reset_val = c->reset_channel ?
                         Channel_GetValue(mgr->channels, c->reset_channel) != 0 : false;

        if (reset_val && !c->prev_reset_input) {
            c->count = 0;
        }

        if (count_val && !c->prev_count_input) {
            int32_t delta = 1;

            if (c->mode == COUNTER_DOWN) {
                delta = -1;
            } else if (c->mode == COUNTER_UPDOWN && c->direction_channel) {
                delta = Channel_GetValue(mgr->channels, c->direction_channel) ? 1 : -1;
            }

            c->count += delta;

            if (c->flags & COUNTER_FLAG_WRAP) {
                if (c->count > c->max_value) c->count = c->min_value;
                if (c->count < c->min_value) c->count = c->max_value;
            }
            if (c->flags & COUNTER_FLAG_CLAMP) {
                if (c->count > c->max_value) c->count = c->max_value;
                if (c->count < c->min_value) c->count = c->min_value;
            }

            if ((c->flags & COUNTER_FLAG_AUTORESET) && c->count == c->preset) {
                c->count = 0;
            }
        }

        c->prev_count_input = count_val;
        c->prev_reset_input = reset_val;

        Channel_SetValue(mgr->channels, c->output_channel, c->count);
        if (c->done_channel) {
            Channel_SetValue(mgr->channels, c->done_channel,
                            (c->count == c->preset) ? 1 : 0);
        }
    }
}

/* ============================================================================
 * Update: Min/Max Hold
 * ============================================================================ */

static void update_settings(StatefulManager_t* mgr)
{
    for (uint16_t i = 0; i < mgr->setting_count; i++) {
        Setting_t* s = &mgr->settings[i];
        if (!(s->flags & SETTING_FLAG_ENABLED)) continue;

        if (!s->initialized) {
            s->value = s->initial;
            /* an initial value outside min..max (edited range) starts
             * clamped instead of showing until the first press */
            if (s->min_value <= s->max_value) {
                if (s->value > s->max_value) s->value = s->max_value;
                if (s->value < s->min_value) s->value = s->min_value;
            }
            s->initialized = true;
        }

        /* 0 and 0xFFFF both mean "not connected" (0 = system zero) */
        bool gate = s->enable_channel == 0 || s->enable_channel == 0xFFFF ||
                    Channel_GetValue(mgr->channels, s->enable_channel) != 0;
        bool inc = s->inc_channel != 0 && s->inc_channel != 0xFFFF &&
                   Channel_GetValue(mgr->channels, s->inc_channel) != 0;
        bool dec = s->dec_channel != 0 && s->dec_channel != 0xFFFF &&
                   Channel_GetValue(mgr->channels, s->dec_channel) != 0;

        int64_t delta = 0;
        if (gate && inc && !s->prev_inc) delta += s->step;
        if (gate && dec && !s->prev_dec) delta -= s->step;
        s->prev_inc = inc;
        s->prev_dec = dec;

        if (delta) {
            /* 64-bit: value + step overflowed int32 (UB) near the limits;
             * after the wrap / clamp v is within (or at) min..max */
            int64_t v = (int64_t)s->value + delta;
            if (s->flags & SETTING_FLAG_WRAP) {
                if (v > s->max_value) v = s->min_value;
                if (v < s->min_value) v = s->max_value;
            } else {
                if (v > s->max_value) v = s->max_value;
                if (v < s->min_value) v = s->min_value;
            }
            s->value = (int32_t)v;
        }

        Channel_SetValue(mgr->channels, s->output_channel, s->value);
    }
}

static void update_randoms(StatefulManager_t* mgr)
{
    for (uint16_t i = 0; i < mgr->random_count; i++) {
        RandomNode_t* r = &mgr->randoms[i];
        if (!(r->flags & RANDOM_FLAG_ENABLED)) continue;

        if (!r->initialized) {
            r->state = r->seed ? r->seed : 0xC0FFEE42u;
            r->initialized = true;
        }

        bool has_trig = r->trigger_channel != 0 &&
                        r->trigger_channel != 0xFFFF;
        bool trig = !has_trig ||
                    Channel_GetValue(mgr->channels,
                                     r->trigger_channel) != 0;
        bool fire = has_trig ? (trig && !r->prev_trigger) : true;
        if (has_trig) r->prev_trigger = trig;

        if (fire) {
            /* Deterministic LCG (identical on host and target) */
            r->state = r->state * 1103515245u + 12345u;
            /* span in 64 bits: max - min + 1 overflowed int32 (UB) for
             * wide ranges; a span past 2^16 is still drawn from the LCG's
             * 16 high bits, as before */
            int64_t span = (int64_t)r->max_value - r->min_value + 1;
            if (span < 1) span = 1;
            int32_t value = (int32_t)((int64_t)r->min_value +
                                      (int64_t)((r->state >> 16) % (uint64_t)span));
            Channel_SetValue(mgr->channels, r->output_channel, value);
        }
    }
}

static void update_minmax_holds(StatefulManager_t* mgr)
{
    for (uint16_t i = 0; i < mgr->hold_count; i++) {
        MinMaxHold_t* h = &mgr->holds[i];
        if (!(h->flags & HOLD_FLAG_ENABLED)) continue;

        int32_t val = Channel_GetValue(mgr->channels, h->input_channel);
        bool reset = h->reset_channel ?
                     Channel_GetValue(mgr->channels, h->reset_channel) != 0 : false;

        if (!Channel_IsLive(mgr->channels, h->input_channel)) {
            /* no real sample yet (the reset 0, a design seed, an input of a
             * previous design): the glass latched OIL PRESS MIN 0.0 from the
             * first tick before the replay fed the input. Follow the input
             * and start holding at its first real value. */
            h->initialized = false;
            h->min_value = h->max_value = val;
        } else if (reset || !h->initialized) {
            if (h->flags & HOLD_FLAG_RESET_TO_ZERO) {
                h->min_value = 0;
                h->max_value = 0;
            } else {
                h->min_value = val;
                h->max_value = val;
            }
            h->initialized = true;
        } else {
            if (val < h->min_value) h->min_value = val;
            if (val > h->max_value) h->max_value = val;
        }

        /* 0 / 0xFFFF = no output. Both loaders configure HOLD_BOTH with the
         * unused side on 0, and this wrote the held min (or max) into
         * channel 0 - the system constant ZERO that every unbound node
         * input reads - on every tick. */
        if ((h->mode == HOLD_MIN || h->mode == HOLD_BOTH) &&
            h->min_output != 0 && h->min_output != 0xFFFF) {
            Channel_SetValue(mgr->channels, h->min_output, h->min_value);
        }
        if ((h->mode == HOLD_MAX || h->mode == HOLD_BOTH) &&
            h->max_output != 0 && h->max_output != 0xFFFF) {
            Channel_SetValue(mgr->channels, h->max_output, h->max_value);
        }
    }
}

/* ============================================================================
 * Update: Hysteresis
 * ============================================================================ */

static void update_hysteresis(StatefulManager_t* mgr)
{
    for (uint16_t i = 0; i < mgr->hyst_count; i++) {
        Hysteresis_t* h = &mgr->hysts[i];
        if (!(h->flags & HYST_FLAG_ENABLED)) continue;

        int32_t val = Channel_GetValue(mgr->channels, h->input_channel);
        int64_t hi = h->threshold_high, lo = h->threshold_low;
        if (h->flags & HYST_FLAG_REF) {          /* a limit channel: the thresholds ride on it */
            int32_t ref = Channel_GetValue(mgr->channels, h->ref_channel);
            hi += ref;
            lo += ref;
        }

        if (h->state) {
            if ((int64_t)val < lo) {
                h->state = false;
            }
        } else {
            if ((int64_t)val > hi) {
                h->state = true;
            }
        }

        int32_t output = h->state ? 1 : 0;
        if (h->flags & HYST_FLAG_INVERT) {
            output = !output;
        }

        Channel_SetValue(mgr->channels, h->output_channel, output);
    }
}

/* ============================================================================
 * Update: Derivative
 * ============================================================================ */

static void update_derivatives(StatefulManager_t* mgr)
{
    if (mgr->dt_ms == 0) return;

    for (uint16_t i = 0; i < mgr->deriv_count; i++) {
        Derivative_t* d = &mgr->derivs[i];
        if (!(d->flags & DERIV_FLAG_ENABLED)) continue;

        int32_t val = Channel_GetValue(mgr->channels, d->input_channel);
        int32_t rate = 0;

        if (d->initialized) {
            /* 64-bit: (val - prev) * 1000 overflowed int32 (UB) for a step
             * above ~2.1 M units per tick (GPS 1e-7 deg, odometers);
             * the rate saturates to int32 */
            int64_t r64 = ((int64_t)val - d->prev_value) * 1000 / (int64_t)mgr->dt_ms;
            if (r64 > INT32_MAX) r64 = INT32_MAX;
            if (r64 < -INT32_MAX) r64 = -INT32_MAX;
            rate = (int32_t)r64;

            if (d->smoothing > 0) {
                int64_t alpha = d->smoothing;
                d->smoothed_rate = (d->smoothed_rate * (256 - alpha) +
                                   (int64_t)rate * 256 * alpha) / 256;
                rate = (int32_t)(d->smoothed_rate >> 8);
            }

            if (d->flags & DERIV_FLAG_ABS) {
                rate = (rate < 0) ? -rate : rate;
            }
        }

        d->prev_value = val;
        d->initialized = true;

        Channel_SetValue(mgr->channels, d->output_channel, rate);
    }
}

/* ============================================================================
 * Update: Integral
 * ============================================================================ */

static void update_integrals(StatefulManager_t* mgr)
{
    if (mgr->dt_ms == 0) return;

    for (uint16_t i = 0; i < mgr->integral_count; i++) {
        Integral_t* integ = &mgr->integrals[i];
        if (!(integ->flags & INTEG_FLAG_ENABLED)) continue;

        bool reset = integ->reset_channel ?
                     Channel_GetValue(mgr->channels, integ->reset_channel) != 0 : false;

        if (reset) {
            integ->accumulator = 0;
        } else {
            int32_t val = Channel_GetValue(mgr->channels, integ->input_channel);

            if (!(integ->flags & INTEG_FLAG_POSITIVE_ONLY) || val > 0) {
                integ->accumulator += (int64_t)val * mgr->dt_ms;
            }
        }

        /* compare in 64 bits: the int32 cast of an accumulator past
         * INT32_MAX * 1000 wrapped negative and clamped to the MINIMUM */
        int64_t out64 = integ->accumulator / 1000;

        if (integ->flags & INTEG_FLAG_CLAMP) {
            if (out64 < integ->min_value) {
                out64 = integ->min_value;
                integ->accumulator = (int64_t)integ->min_value * 1000;
            }
            if (out64 > integ->max_value) {
                out64 = integ->max_value;
                integ->accumulator = (int64_t)integ->max_value * 1000;
            }
        }
        if (out64 > INT32_MAX) out64 = INT32_MAX;
        if (out64 < INT32_MIN) out64 = INT32_MIN;
        int32_t output = (int32_t)out64;

        Channel_SetValue(mgr->channels, integ->output_channel, output);
    }
}

/* ============================================================================
 * Update: Rate Limiter
 * ============================================================================ */

static void update_rate_limiters(StatefulManager_t* mgr)
{
    if (mgr->dt_ms == 0) return;

    for (uint16_t i = 0; i < mgr->limiter_count; i++) {
        RateLimiter_t* r = &mgr->limiters[i];
        if (!(r->flags & RATE_FLAG_ENABLED)) continue;

        int32_t target = Channel_GetValue(mgr->channels, r->input_channel);

        if (!r->initialized) {
            r->current_value = target;
            r->initialized = true;
        } else {
            /* 64-bit: rate * dt and target - current overflowed int32 (UB)
             * for fast limits / far-apart values */
            int64_t max_rise = ((int64_t)r->max_rise_per_sec * mgr->dt_ms) / 1000;
            int64_t max_fall = ((int64_t)r->max_fall_per_sec * mgr->dt_ms) / 1000;

            if (max_rise < 1) max_rise = 1;
            if (max_fall < 1) max_fall = 1;

            int64_t delta = (int64_t)target - r->current_value;

            if (delta > max_rise) {
                r->current_value = (int32_t)(r->current_value + max_rise);
            } else if (delta < -max_fall) {
                r->current_value = (int32_t)(r->current_value - max_fall);
            } else {
                r->current_value = target;
            }
        }

        Channel_SetValue(mgr->channels, r->output_channel, r->current_value);
    }
}

/* ============================================================================
 * Update All
 * ============================================================================ */

void Stateful_UpdateAll(StatefulManager_t* mgr, uint32_t dt_ms)
{
    if (!mgr || !mgr->channels) return;

    mgr->dt_ms = dt_ms;

    update_edge_detects(mgr);
    update_latches(mgr);
    update_counters(mgr);
    update_settings(mgr);
    update_randoms(mgr);
    update_minmax_holds(mgr);
    update_hysteresis(mgr);
    update_derivatives(mgr);
    update_integrals(mgr);
    update_rate_limiters(mgr);
}

/* ============================================================================
 * Reset Specific Nodes
 * ============================================================================ */

void Stateful_ResetCounter(StatefulManager_t* mgr, uint16_t id)
{
    if (!mgr) return;
    for (uint16_t i = 0; i < mgr->counter_count; i++) {
        if (mgr->counters[i].id == id) {
            mgr->counters[i].count = 0;
            break;
        }
    }
}

void Stateful_ResetMinMax(StatefulManager_t* mgr, uint16_t id)
{
    if (!mgr) return;
    for (uint16_t i = 0; i < mgr->hold_count; i++) {
        if (mgr->holds[i].id == id) {
            mgr->holds[i].initialized = false;
            break;
        }
    }
}

void Stateful_ResetIntegral(StatefulManager_t* mgr, uint16_t id)
{
    if (!mgr) return;
    for (uint16_t i = 0; i < mgr->integral_count; i++) {
        if (mgr->integrals[i].id == id) {
            mgr->integrals[i].accumulator = 0;
            break;
        }
    }
}

void Stateful_ResetSession(StatefulManager_t* mgr)
{
    if (!mgr) return;
    for (uint16_t i = 0; i < mgr->hold_count; i++) mgr->holds[i].initialized = false;
    for (uint16_t i = 0; i < mgr->integral_count; i++) mgr->integrals[i].accumulator = 0;
}
