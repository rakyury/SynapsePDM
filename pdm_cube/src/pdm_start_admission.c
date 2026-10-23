#include "pdm_start_admission.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

static bool horizon(uint32_t n) { return n > 0 && n <= INT32_MAX; }
static bool fresh(uint32_t now, uint32_t sampled, uint32_t ttl)
{ return (uint32_t)(now - sampled) <= ttl; }
static bool after(uint32_t a, uint32_t b)
{ const uint32_t d = a - b; return d != 0 && d <= INT32_MAX; }
static bool feedback_ok(const PDM_StartAdmission *s, const PDM_StartFeedback *f, uint32_t now)
{ return f->valid && f->healthy && fresh(now, f->sampled_ms, s->config.feedback_ttl_ms); }
static bool off_proof(const PDM_StartAdmission *s, const PDM_StartFeedback *f, uint32_t now)
{ return feedback_ok(s, f, now) && f->confirmed_off && f->measured_mA == 0; }
static bool global_ok(const PDM_StartAdmission *s, const PDM_StartInputs *in, uint32_t now)
{
    return in != NULL && in->valid && in->healthy && !in->inhibited && in->link_healthy &&
        fresh(now, in->sampled_ms, s->config.feedback_ttl_ms) &&
        in->total_measured_mA <= s->config.total_budget_mA &&
        in->total_measured_mA <= s->total_reserved_mA;
}

static bool config_ok(const PDM_StartConfig *c)
{
    if (c == NULL || (c->channel_count != 14 && c->channel_count != 24) ||
        c->group_count == 0 || c->group_count > PDM_START_MAX_GROUPS ||
        c->max_starts < 2 || c->max_starts > 4 || c->total_budget_mA == 0 ||
        c->base_load_mA > c->total_budget_mA || !horizon(c->request_ttl_ms) ||
        !horizon(c->feedback_ttl_ms)) return false;
    uint64_t group_base = 0, maximum_reserve = c->base_load_mA;
    for (unsigned g = 0; g < c->group_count; ++g) {
        if (c->group_budget_mA[g] == 0 || c->group_base_mA[g] > c->group_budget_mA[g]) return false;
        group_base += c->group_base_mA[g];
    }
    if (group_base > c->base_load_mA) return false;
    for (unsigned i = 0; i < c->channel_count; ++i) {
        const PDM_StartChannelConfig *ch = &c->channels[i];
        if (!ch->commissioned || ch->group >= c->group_count || ch->priority > 3 ||
            ch->continuous_mA == 0 || ch->start_mA < ch->continuous_mA ||
            !horizon(ch->start_duration_ms) || !horizon(ch->cooldown_ms)) return false;
        maximum_reserve += ch->start_mA;
    }
    /* Reject a configuration whose maximum envelopes cannot be represented,
       even if the current slot limit would hide that overflow in one scenario. */
    return maximum_reserve <= UINT32_MAX;
}

static void totals(PDM_StartAdmission *s)
{
    uint64_t total = s->config.base_load_mA;
    uint64_t groups[PDM_START_MAX_GROUPS] = {0};
    unsigned slots = 0;
    for (unsigned g = 0; g < s->config.group_count; ++g) groups[g] = s->config.group_base_mA[g];
    for (unsigned i = 0; i < s->config.channel_count; ++i) {
        total += s->channels[i].reserve_mA;
        groups[s->config.channels[i].group] += s->channels[i].reserve_mA;
        slots += s->channels[i].slot_reserved ? 1u : 0u;
    }
    s->total_reserved_mA = (uint32_t)total;
    s->starts_reserved = (uint8_t)slots;
    for (unsigned g = 0; g < s->config.group_count; ++g) s->group_reserved_mA[g] = (uint32_t)groups[g];
}

bool PDM_Start_Init(PDM_StartAdmission *s, const PDM_StartConfig *config)
{
    if (s == NULL) return false;
    /* Init is construction, not runtime reset: use Reset to preserve live reserves. */
    memset(s, 0, sizeof(*s));
    if (!config_ok(config)) return false;
    s->config = *config; s->valid = true; totals(s); return true;
}

static void remove_at(PDM_StartAdmission *s, unsigned at)
{
    for (unsigned j = at + 1; j < s->queue_count; ++j) s->queue[j - 1] = s->queue[j];
    --s->queue_count;
}
static void remove_channel(PDM_StartAdmission *s, uint8_t channel)
{
    for (unsigned i = 0; i < s->queue_count; ++i)
        if (s->queue[i] == channel) { remove_at(s, i); return; }
}

PDM_StartReason PDM_Start_Request(PDM_StartAdmission *s, uint8_t channel,
    uint32_t request_id, uint32_t requested_ms, uint32_t now_ms)
{
    if (s == NULL || !s->valid) return PDM_START_BAD_CONFIG;
    if (channel >= s->config.channel_count) return PDM_START_BAD_CHANNEL;
    PDM_StartChannel *ch = &s->channels[channel];
    if (ch->request_seen && request_id == ch->request_id)
        return ch->state == PDM_START_QUEUED || ch->state == PDM_START_STARTING || ch->state == PDM_START_ON
            ? ch->reason : PDM_START_OLD_REQUEST;
    if (ch->request_seen && !after(request_id, ch->request_id)) return PDM_START_OLD_REQUEST;
    ch->request_seen = true; ch->request_id = request_id;
    if (!fresh(now_ms, requested_ms, s->config.request_ttl_ms) ||
        (s->reset_seen && !after(requested_ms, s->reset_ms))) {
        ch->reason = PDM_START_EXPIRED;
        return PDM_START_EXPIRED;
    }
    if (ch->state == PDM_START_FAULT) return PDM_START_FAULT_LATCHED;
    /* Newer duplicate demand is also idempotent; it cannot refresh queue TTL. */
    if (ch->state != PDM_START_OFF) return ch->reason;
    if (s->queue_count >= s->config.channel_count) return PDM_START_BAD_CONFIG;
    unsigned at = s->queue_count;
    const unsigned priority = s->config.channels[channel].priority;
    while (at > 0 && s->config.channels[s->queue[at - 1]].priority > priority) {
        s->queue[at] = s->queue[at - 1]; --at;
    }
    s->queue[at] = channel; ++s->queue_count;
    ch->requested_ms = requested_ms; ch->state = PDM_START_QUEUED; ch->reason = PDM_START_OK;
    return PDM_START_OK;
}

static void fault(PDM_StartChannel *ch, PDM_StartReason reason, uint32_t now)
{
    if (ch->state != PDM_START_FAULT) { ch->event_ms = now; ch->state = PDM_START_FAULT; }
    ch->reason = reason;
}

void PDM_Start_Cancel(PDM_StartAdmission *s, uint8_t channel, uint32_t now_ms)
{
    if (s == NULL || !s->valid || channel >= s->config.channel_count) return;
    PDM_StartChannel *ch = &s->channels[channel]; remove_channel(s, channel);
    if (ch->reserve_mA != 0 || ch->state == PDM_START_FAULT) fault(ch, PDM_START_CANCELLED, now_ms);
    else { ch->state = PDM_START_OFF; ch->reason = PDM_START_CANCELLED; }
}

void PDM_Start_Reset(PDM_StartAdmission *s, uint32_t now_ms)
{
    if (s == NULL || !s->valid) return;
    s->reset_seen = true; s->reset_ms = now_ms;
    s->queue_count = 0;
    for (unsigned i = 0; i < s->config.channel_count; ++i) {
        PDM_StartChannel *ch = &s->channels[i];
        ch->event_ms = now_ms; ch->off_confirmation_required = true;
        if (ch->reserve_mA != 0 || ch->state == PDM_START_FAULT) ch->state = PDM_START_FAULT;
        else ch->state = PDM_START_OFF;
        ch->reason = PDM_START_CANCELLED;
        /* Keep replay watermark; a reset must not revive a previously consumed ID. */
    }
}

bool PDM_Start_ClearFault(PDM_StartAdmission *s, uint8_t channel,
    const PDM_StartInputs *in, uint32_t now_ms)
{
    if (s == NULL || !s->valid || in == NULL || channel >= s->config.channel_count) return false;
    PDM_StartChannel *ch = &s->channels[channel];
    const PDM_StartFeedback *f = &in->channels[channel];
    if (ch->state != PDM_START_FAULT || ch->reserve_mA != 0 ||
        !global_ok(s, in, now_ms) || !in->group_healthy[s->config.channels[channel].group] ||
        !off_proof(s, f, now_ms) || !after(f->sampled_ms, ch->event_ms)) return false;
    ch->state = PDM_START_OFF; ch->reason = PDM_START_OK; ch->off_confirmation_required = false;
    return true;
}

void PDM_Start_Step(PDM_StartAdmission *s, const PDM_StartInputs *in, uint32_t now_ms)
{
    if (s == NULL || !s->valid) return;
    bool healthy = global_ok(s, in, now_ms);
    for (unsigned i = 0; i < s->config.channel_count; ++i) {
        PDM_StartChannel *ch = &s->channels[i];
        const PDM_StartChannelConfig *cfg = &s->config.channels[i];
        const PDM_StartFeedback *f = in == NULL ? NULL : &in->channels[i];
        const bool good = f != NULL && feedback_ok(s, f, now_ms);
        if (ch->state == PDM_START_STARTING || ch->state == PDM_START_ON) {
            if (!healthy || !good || !in->group_healthy[cfg->group] || f->measured_mA > ch->reserve_mA)
                fault(ch, PDM_START_UNHEALTHY, now_ms);
            else if (ch->state == PDM_START_STARTING) {
                if ((uint32_t)(now_ms - ch->started_ms) >= cfg->start_duration_ms)
                    fault(ch, PDM_START_DEADLINE, now_ms);
                else if (f->start_complete && !f->confirmed_off && after(f->sampled_ms, ch->started_ms)) {
                    if (f->measured_mA > cfg->continuous_mA) fault(ch, PDM_START_UNHEALTHY, now_ms);
                    else { ch->state = PDM_START_ON; ch->reserve_mA = cfg->continuous_mA; ch->slot_reserved = false; }
                }
            } else if (f->confirmed_off) fault(ch, PDM_START_UNKNOWN_OFF, now_ms);
        }
        /* OFF sent is not OFF observed. Require a later fresh healthy sample,
           never a pre-start or same-tick snapshot, before returning any reserve. */
        if (ch->state == PDM_START_FAULT && ch->reserve_mA != 0 && good && off_proof(s, f, now_ms) &&
            after(f->sampled_ms, ch->event_ms)) {
            ch->reserve_mA = 0; ch->slot_reserved = false; ch->last_off_ms = now_ms;
        }
        if (ch->off_confirmation_required && good && off_proof(s, f, now_ms) && after(f->sampled_ms, ch->event_ms))
            ch->off_confirmation_required = false;
    }
    totals(s);
    healthy = global_ok(s, in, now_ms); /* Recheck after any release/completion. */
    /* Expiry and CAN loss discard pending commands, including behind a blocked
       head; no health recovery may revive these commands. */
    for (unsigned i = 0; i < s->queue_count;) {
        PDM_StartChannel *ch = &s->channels[s->queue[i]];
        if (in == NULL || !in->link_healthy || !fresh(now_ms, ch->requested_ms, s->config.request_ttl_ms)) {
            ch->state = PDM_START_OFF;
            ch->reason = in == NULL || !in->link_healthy ? PDM_START_CANCELLED : PDM_START_EXPIRED;
            remove_at(s, i);
        } else ++i;
    }
    bool known_off = true;
    for (unsigned i = 0; i < s->config.channel_count; ++i) {
        const PDM_StartChannel *ch = &s->channels[i];
        if (ch->state == PDM_START_STARTING || ch->state == PDM_START_ON) continue;
        const PDM_StartFeedback *f = in == NULL ? NULL : &in->channels[i];
        if (ch->reserve_mA != 0 || ch->off_confirmation_required || f == NULL ||
            !off_proof(s, f, now_ms)) known_off = false;
    }
    /* Strict priority/FIFO: a blocked head is never silently leapfrogged. */
    while (s->queue_count != 0) {
        const uint8_t i = s->queue[0];
        PDM_StartChannel *ch = &s->channels[i];
        const PDM_StartChannelConfig *cfg = &s->config.channels[i];
        PDM_StartReason reason = PDM_START_OK;
        if (!healthy || !in->group_healthy[cfg->group]) reason = PDM_START_UNHEALTHY;
        else if (!known_off) reason = PDM_START_UNKNOWN_OFF;
        else if (ch->ever_started && !fresh(now_ms, ch->last_off_ms, INT32_MAX)) reason = PDM_START_COOLDOWN;
        else if (ch->ever_started && (uint32_t)(now_ms - ch->last_off_ms) < cfg->cooldown_ms) reason = PDM_START_COOLDOWN;
        else if (s->starts_reserved >= s->config.max_starts) reason = PDM_START_SLOTS;
        else if ((uint64_t)s->total_reserved_mA + cfg->start_mA > s->config.total_budget_mA) reason = PDM_START_TOTAL_BUDGET;
        else if ((uint64_t)s->group_reserved_mA[cfg->group] + cfg->start_mA > s->config.group_budget_mA[cfg->group]) reason = PDM_START_GROUP_BUDGET;
        ch->reason = reason;
        if (reason != PDM_START_OK) break;
        ch->state = PDM_START_STARTING; ch->reserve_mA = cfg->start_mA; ch->slot_reserved = true;
        ch->started_ms = ch->event_ms = now_ms; ch->ever_started = true;
        remove_at(s, 0); totals(s);
    }
}
