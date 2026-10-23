/* Host-only scenarios for the real admission model. All electrical numbers
 * below are deliberately fictional simulation fixtures, NEVER board limits.
 * This executable has no HAL, GPIO, adapter, CAN or flash dependency. */
#include "pdm_board_profile.h"
#include "pdm_start_admission.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Invariant failed at line %d: %s\n", __LINE__, #condition); \
    exit(1); } } while (0)

typedef struct {
    unsigned completed, max_reserved_slots;
    uint32_t max_reserved_ma;
    unsigned blocked_observations, retained_fault_reservations;
} Evidence;

static void check_profile(const PDM_BoardProfile *profile)
{
    CHECK(profile != NULL && PDM_BoardProfile_Validate(profile, false));
    CHECK(PDM_BoardProfile_Hardware()->channel_count == 14);
    CHECK(profile->group_count == 0); /* No approved physical group design. */
    for (unsigned i = 0; i < profile->channel_count; ++i) {
        CHECK(profile->channels[i].number == i + 1u);
        CHECK(profile->channels[i].budget_group == PDM_BOARD_UNASSIGNED);
        if (profile->id == 24) {
            CHECK(profile->simulation_only);
            CHECK(profile->channels[i].gate.port == PDM_BOARD_UNASSIGNED);
            if (i < 10) CHECK(profile->channels[i].target_peak_ma == 30000);
            else CHECK(profile->channels[i].target_continuous_min_ma == 17000 &&
                       profile->channels[i].target_continuous_max_ma == 20000);
        }
    }
}

/* The host's physical-output boundary is explicitly fixed OFF. Virtual
 * scheduler states never feed this boundary. Hardware OFF is separately
 * tested by test_main_outputs.c against the actual production main loop. */
static unsigned physical_outputs_on(void) { return 0; }

static void report(const char *scenario, unsigned slots, const PDM_BoardProfile *profile,
                   const PDM_StartConfig *config, const Evidence *evidence)
{
    CHECK(physical_outputs_on() == 0);
    printf("{\"scenario\":\"%s\",\"profile_channels\":%u,\"slot_limit\":%u,"
           "\"completed_requests\":%u,\"max_reserved_slots\":%u,"
           "\"max_reserved_ma\":%lu,\"blocked_observations\":%u,"
           "\"retained_fault_reservations\":%u,\"physical_outputs_on\":0,"
           "\"output_backend\":\"simulated_zero_output_backend\","
           "\"fixture_peak_ma\":30000,\"fixture_total_budget_ma\":%lu,"
           "\"fixture_group_budgets_ma\":[%lu,%lu],\"fixture_base_load_ma\":%lu,"
           "\"hardware_authorized\":false,\"fixture_only\":true,\"checks_passed\":true}\n",
           scenario, profile->channel_count, slots, evidence->completed,
           evidence->max_reserved_slots, (unsigned long)evidence->max_reserved_ma,
           evidence->blocked_observations, evidence->retained_fault_reservations,
           (unsigned long)config->total_budget_mA,
           (unsigned long)config->group_budget_mA[0],
           (unsigned long)config->group_budget_mA[1], (unsigned long)config->base_load_mA);
}

typedef struct {
    PDM_StartAdmission scheduler;
    PDM_StartInputs feedback;
    uint32_t now;
    Evidence evidence;
} Simulation;

static void fresh(Simulation *sim)
{
    sim->feedback.sampled_ms = sim->now;
    for (unsigned i = 0; i < sim->scheduler.config.channel_count; ++i)
        sim->feedback.channels[i].sampled_ms = sim->now;
}

static void observe(Simulation *sim)
{
    const PDM_StartAdmission *s = &sim->scheduler;
    unsigned slots = 0;
    uint32_t reserved = s->config.base_load_mA;
    for (unsigned i = 0; i < s->config.channel_count; ++i) {
        slots += s->channels[i].slot_reserved ? 1u : 0u;
        reserved += s->channels[i].reserve_mA;
    }
    CHECK(slots == s->starts_reserved && slots <= s->config.max_starts);
    CHECK(reserved == s->total_reserved_mA && reserved <= s->config.total_budget_mA);
    for (unsigned g = 0; g < s->config.group_count; ++g)
        CHECK(s->group_reserved_mA[g] <= s->config.group_budget_mA[g]);
    if (slots > sim->evidence.max_reserved_slots) sim->evidence.max_reserved_slots = slots;
    if (reserved > sim->evidence.max_reserved_ma) sim->evidence.max_reserved_ma = reserved;
    CHECK(physical_outputs_on() == 0);
}

static void step(Simulation *sim)
{
    fresh(sim);
    PDM_Start_Step(&sim->scheduler, &sim->feedback, sim->now);
    observe(sim);
}

static void setup(Simulation *sim, const PDM_BoardProfile *profile,
                  unsigned slots, const char *scenario)
{
    memset(sim, 0, sizeof(*sim));
    sim->now = 100;
    PDM_StartConfig config = {0};
    config.channel_count = profile->channel_count;
    config.group_count = 2;
    config.max_starts = (uint8_t)slots;
    /* Explicit synthetic budgets: this is neither a board rating nor a
     * proposed wiring scheme. Keep design-target metadata uncommissioned. */
    config.total_budget_mA = 1000000;
    config.base_load_mA = 2000;
    config.request_ttl_ms = 10000;
    config.feedback_ttl_ms = 50;
    config.group_budget_mA[0] = config.group_budget_mA[1] = 500000;
    config.group_base_mA[0] = config.group_base_mA[1] = 1000;
    for (unsigned i = 0; i < profile->channel_count; ++i) {
        config.channels[i] = (PDM_StartChannelConfig){
            .continuous_mA = i < 10 ? 5000 : 17000,
            .start_mA = i < 10 ? 30000 : 20000,
            .start_duration_ms = 100, .cooldown_ms = 10,
            .group = (uint8_t)(i % 2), .commissioned = true};
        sim->feedback.channels[i].valid = true;
        sim->feedback.channels[i].healthy = true;
        sim->feedback.channels[i].confirmed_off = true;
    }
    if (strcmp(scenario, "total-budget") == 0)
        config.total_budget_mA = config.base_load_mA + (slots - 1u) * 30000u;
    if (strcmp(scenario, "active-budget") == 0)
        config.total_budget_mA = config.base_load_mA + slots * 30000u;
    if (strcmp(scenario, "group-budget") == 0) {
        config.group_budget_mA[0] = config.group_base_mA[0] + (slots - 1u) * 30000u;
        for (unsigned i = 0; i < config.channel_count; ++i) config.channels[i].group = 0;
    }
    sim->feedback.valid = sim->feedback.healthy = sim->feedback.link_healthy = true;
    sim->feedback.group_healthy[0] = sim->feedback.group_healthy[1] = true;
    CHECK(PDM_Start_Init(&sim->scheduler, &config));
    fresh(sim);
}

static void requests(Simulation *sim, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        CHECK(PDM_Start_Request(&sim->scheduler, (uint8_t)i, 1, sim->now, sim->now) == PDM_START_OK);
        const unsigned queued = sim->scheduler.queue_count;
        /* Duplicates cannot manufacture a second queued entry or reserve. */
        (void)PDM_Start_Request(&sim->scheduler, (uint8_t)i, 1, sim->now, sim->now);
        CHECK(sim->scheduler.queue_count == queued);
    }
    CHECK(sim->scheduler.queue_count == count);
    step(sim);
}

static void dispatch(Simulation *sim)
{
    requests(sim, sim->scheduler.config.channel_count);
    CHECK(sim->scheduler.starts_reserved == sim->scheduler.config.max_starts);
    for (unsigned tick = 0; tick < 30; ++tick) {
        sim->now += 10;
        for (unsigned i = 0; i < sim->scheduler.config.channel_count; ++i) {
            if (sim->scheduler.channels[i].state == PDM_START_STARTING) {
                sim->feedback.channels[i].confirmed_off = false;
                sim->feedback.channels[i].start_complete = true;
            }
        }
        step(sim);
        unsigned on = 0;
        for (unsigned i = 0; i < sim->scheduler.config.channel_count; ++i)
            on += sim->scheduler.channels[i].state == PDM_START_ON ? 1u : 0u;
        if (on == sim->scheduler.config.channel_count) {
            sim->evidence.completed = on;
            CHECK(sim->scheduler.queue_count == 0 && sim->scheduler.starts_reserved == 0);
            return;
        }
    }
    CHECK(false); /* Bounded progress: every channel must reach virtual ON. */
}

static void budget(Simulation *sim, bool group)
{
    const unsigned slots = sim->scheduler.config.max_starts;
    requests(sim, slots + 1);
    CHECK(sim->scheduler.starts_reserved == slots - 1);
    CHECK(sim->scheduler.channels[slots - 1].state == PDM_START_QUEUED);
    CHECK(sim->scheduler.channels[slots - 1].reason ==
          (group ? PDM_START_GROUP_BUDGET : PDM_START_TOTAL_BUDGET));
    ++sim->evidence.blocked_observations;
    const uint32_t reserve = sim->scheduler.total_reserved_mA;
    /* A low instantaneous measurement cannot erase the conservative envelope. */
    sim->feedback.total_measured_mA = 1;
    sim->now += 10;
    step(sim);
    CHECK(sim->scheduler.total_reserved_mA == reserve);
    CHECK(sim->scheduler.starts_reserved == slots - 1);
    ++sim->evidence.blocked_observations;
}

static void held_fault(Simulation *sim, bool timeout)
{
    const unsigned slots = sim->scheduler.config.max_starts;
    requests(sim, slots + 1);
    const uint32_t reserve = sim->scheduler.total_reserved_mA;
    for (unsigned i = 0; i < slots; ++i) sim->feedback.channels[i].confirmed_off = false;
    if (timeout) sim->now += 101;
    else { sim->now += 10; sim->feedback.channels[0].healthy = false; }
    step(sim);
    CHECK(sim->scheduler.channels[0].state == PDM_START_FAULT);
    if (timeout) CHECK(sim->scheduler.channels[0].reason == PDM_START_DEADLINE);
    CHECK(sim->scheduler.total_reserved_mA == reserve);
    CHECK(sim->scheduler.starts_reserved == slots);
    CHECK(sim->scheduler.channels[slots].state != PDM_START_STARTING);
    ++sim->evidence.retained_fault_reservations;
    sim->feedback.channels[0].healthy = true;
    CHECK(!PDM_Start_ClearFault(&sim->scheduler, 0, &sim->feedback, sim->now));
    /* Dispatching OFF alone is insufficient; only a new confirmed-OFF input
     * releases the fault's reservation, with fault latch still requiring reset. */
    for (unsigned i = 0; i < slots; ++i) PDM_Start_Cancel(&sim->scheduler, (uint8_t)i, sim->now);
    PDM_Start_Cancel(&sim->scheduler, (uint8_t)slots, sim->now);
    sim->now += 10;
    step(sim);
    CHECK(sim->scheduler.total_reserved_mA == reserve);
    ++sim->evidence.retained_fault_reservations;
    for (unsigned i = 0; i < slots; ++i) sim->feedback.channels[i].confirmed_off = true;
    sim->now += 10;
    step(sim);
    CHECK(sim->scheduler.starts_reserved == 0);
    CHECK(sim->scheduler.total_reserved_mA == sim->scheduler.config.base_load_mA);
    CHECK(sim->scheduler.channels[0].state == PDM_START_FAULT);
    CHECK(PDM_Start_ClearFault(&sim->scheduler, 0, &sim->feedback, sim->now));
    CHECK(sim->scheduler.channels[0].state == PDM_START_OFF);
    (void)PDM_Start_Request(&sim->scheduler, 0, 1, sim->now, sim->now);
    CHECK(sim->scheduler.channels[0].state == PDM_START_OFF && sim->scheduler.queue_count == 0);
    CHECK(PDM_Start_Request(&sim->scheduler, 0, 0, sim->now, sim->now) == PDM_START_OLD_REQUEST);
    CHECK(PDM_Start_Request(&sim->scheduler, 0, 2, sim->now, sim->now) == PDM_START_OK);
    sim->now += 11;
    step(sim);
    CHECK(sim->scheduler.channels[0].state == PDM_START_STARTING);
}

static void active_budget(Simulation *sim)
{
    const unsigned slots = sim->scheduler.config.max_starts;
    requests(sim, 1);
    CHECK(sim->scheduler.channels[0].state == PDM_START_STARTING);
    sim->now += 10;
    sim->feedback.channels[0].confirmed_off = false;
    sim->feedback.channels[0].start_complete = true;
    step(sim);
    CHECK(sim->scheduler.channels[0].state == PDM_START_ON);
    CHECK(sim->scheduler.channels[0].reserve_mA == 5000);
    sim->evidence.completed = 1;
    for (unsigned i = 1; i <= slots; ++i)
        CHECK(PDM_Start_Request(&sim->scheduler, (uint8_t)i, 1, sim->now, sim->now) == PDM_START_OK);
    sim->now += 10;
    step(sim);
    /* The input budget has room for slots*30A alone, but an already running
     * 5A envelope consumes headroom. Its low measured current cannot erase it. */
    CHECK(sim->scheduler.channels[0].state == PDM_START_ON);
    CHECK(sim->scheduler.channels[0].reserve_mA == 5000);
    CHECK(sim->scheduler.starts_reserved == slots - 1);
    CHECK(sim->scheduler.total_reserved_mA == sim->scheduler.config.base_load_mA + 5000 + (slots - 1u) * 30000u);
    CHECK(sim->scheduler.channels[slots].state == PDM_START_QUEUED);
    CHECK(sim->scheduler.channels[slots].reason == PDM_START_TOTAL_BUDGET);
    ++sim->evidence.blocked_observations;
}

static void reset_scenario(Simulation *sim)
{
    requests(sim, sim->scheduler.config.channel_count);
    const unsigned active = sim->scheduler.starts_reserved;
    const uint32_t reserve = sim->scheduler.total_reserved_mA;
    for (unsigned i = 0; i < active; ++i) sim->feedback.channels[i].confirmed_off = false;
    PDM_Start_Reset(&sim->scheduler, sim->now);
    CHECK(sim->scheduler.queue_count == 0);
    CHECK(sim->scheduler.total_reserved_mA == reserve);
    CHECK(sim->scheduler.starts_reserved == active);
    ++sim->evidence.retained_fault_reservations;
    sim->now += 10;
    step(sim);
    CHECK(sim->scheduler.starts_reserved == active);
    for (unsigned i = 0; i < active; ++i) sim->feedback.channels[i].confirmed_off = true;
    sim->now += 10;
    step(sim);
    CHECK(sim->scheduler.starts_reserved == 0 && sim->scheduler.queue_count == 0);
    CHECK(sim->scheduler.total_reserved_mA == sim->scheduler.config.base_load_mA);
    for (unsigned i = 0; i < sim->scheduler.config.channel_count; ++i) {
        CHECK(sim->scheduler.channels[i].state != PDM_START_STARTING);
        CHECK(sim->scheduler.channels[i].state != PDM_START_ON);
        CHECK(PDM_Start_Request(&sim->scheduler, (uint8_t)i, 1, sim->now, sim->now) != PDM_START_OK);
    }
}

int main(int argc, char **argv)
{
    if (argc != 4 || strlen(argv[2]) != 1 || argv[2][0] < '2' || argv[2][0] > '4' ||
        (strcmp(argv[3], "14") != 0 && strcmp(argv[3], "24") != 0)) {
        fprintf(stderr, "Usage: start_simulator dispatch|total-budget|group-budget|active-budget|fault|timeout|reset 2|3|4 14|24\n");
        return 2;
    }
    const unsigned slots = (unsigned)(argv[2][0] - '0');
    const PDM_BoardProfile *profile = PDM_BoardProfile_Get((uint16_t)atoi(argv[3]));
    check_profile(profile);
    Simulation sim;
    setup(&sim, profile, slots, argv[1]);
    if (strcmp(argv[1], "dispatch") == 0) dispatch(&sim);
    else if (strcmp(argv[1], "total-budget") == 0) budget(&sim, false);
    else if (strcmp(argv[1], "group-budget") == 0) budget(&sim, true);
    else if (strcmp(argv[1], "active-budget") == 0) active_budget(&sim);
    else if (strcmp(argv[1], "fault") == 0) held_fault(&sim, false);
    else if (strcmp(argv[1], "timeout") == 0) held_fault(&sim, true);
    else if (strcmp(argv[1], "reset") == 0) reset_scenario(&sim);
    else { fprintf(stderr, "Unknown scenario\n"); return 2; }
    report(argv[1], slots, profile, &sim.scheduler.config, &sim.evidence);
    return 0;
}
