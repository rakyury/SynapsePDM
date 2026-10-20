#include "../src/pdm_output_safety.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static PDM_OutputInputs healthy(void)
{
    return (PDM_OutputInputs){.demand = true, .current_valid = true,
                             .vbatt_valid = true, .command_valid = true};
}

int main(void)
{
    PDM_OutputSafety s;
    PDM_OutputPolicy p = {10, 100, 2};
    PDM_OutputInputs in = healthy();
    PDM_OutputSafety_Init(&s, 0);
    assert(s.state == PDM_OUTPUT_OFF);
    PDM_OutputSafety_Step(&s, &p, &in, 0);
    assert(s.state == PDM_OUTPUT_STARTING);
    PDM_OutputSafety_Step(&s, &p, &in, 9);
    assert(s.state == PDM_OUTPUT_STARTING);
    PDM_OutputSafety_Step(&s, &p, &in, 10);
    assert(s.state == PDM_OUTPUT_ON);

    /* Every fault overrides startup and ON, irrespective of demand mode. */
    for (unsigned state = PDM_OUTPUT_OFF; state <= PDM_OUTPUT_ON; ++state) {
        for (unsigned fault = 0; fault < 5; ++fault) {
            PDM_OutputSafety_Init(&s, 0);
            s.state = (PDM_OutputState)state;
            in = healthy();
            if (fault == 0) in.overcurrent = true;
            if (fault == 1) in.current_valid = false;
            if (fault == 2) in.vbatt_valid = false;
            if (fault == 3) in.emergency = true;
            if (fault == 4) in.command_valid = false;
            PDM_OutputSafety_Step(&s, &p, &in, 10);
            assert(s.state == (fault == 3 ? PDM_OUTPUT_LOCKOUT : PDM_OUTPUT_FAULT_WAIT));
            assert(s.latched_faults == (1u << fault));
        }
    }

    PDM_OutputSafety_Init(&s, 0);
    in = healthy();
    for (unsigned attempt = 0; attempt <= 2; ++attempt) {
        uint32_t now = attempt * 101;
        in.overcurrent = true;
        PDM_OutputSafety_Step(&s, &p, &in, now);
        in.overcurrent = false;
        PDM_OutputSafety_Step(&s, &p, &in, now + 99);
        assert(s.state == (attempt == 2 ? PDM_OUTPUT_LOCKOUT : PDM_OUTPUT_FAULT_WAIT));
        PDM_OutputSafety_Step(&s, &p, &in, now + 100);
        assert(s.state == (attempt == 2 ? PDM_OUTPUT_LOCKOUT : PDM_OUTPUT_STARTING));
        assert(s.retries == (attempt == 2 ? 2 : attempt + 1));
    }
    /* Held reset is consumed even when demand or a fault makes it unsafe. */
    in.reset = true;
    PDM_OutputSafety_Step(&s, &p, &in, 400);
    in.demand = false;
    PDM_OutputSafety_Step(&s, &p, &in, 401);
    assert(s.state == PDM_OUTPUT_LOCKOUT);
    in.reset = false;
    PDM_OutputSafety_Step(&s, &p, &in, 402);
    in.reset = true;
    in.overcurrent = true;
    PDM_OutputSafety_Step(&s, &p, &in, 403);
    in.overcurrent = false;
    PDM_OutputSafety_Step(&s, &p, &in, 404);
    assert(s.state == PDM_OUTPUT_LOCKOUT);
    in.reset = false;
    PDM_OutputSafety_Step(&s, &p, &in, 405);
    in.reset = true;
    PDM_OutputSafety_Step(&s, &p, &in, 406);
    assert(s.state == PDM_OUTPUT_OFF && s.retries == 0 && s.latched_faults == 0);

    /* Unsigned wrap, exact cooldown boundary, persistent fault, demand OFF. */
    PDM_OutputSafety_Init(&s, UINT32_MAX - 50);
    in = healthy();
    in.current_valid = false;
    PDM_OutputSafety_Step(&s, &p, &in, UINT32_MAX - 50);
    PDM_OutputSafety_Step(&s, &p, &in, 49);
    assert(s.state == PDM_OUTPUT_FAULT_WAIT && s.retries == 0);
    in.current_valid = true;
    in.demand = false;
    PDM_OutputSafety_Step(&s, &p, &in, 49);
    assert(s.state == PDM_OUTPUT_FAULT_WAIT);
    in.demand = true;
    PDM_OutputSafety_Step(&s, &p, &in, 49);
    assert(s.state == PDM_OUTPUT_STARTING && s.retries == 1);
    PDM_OutputSafety_Step(&s, &p, &in, 59);
    assert(s.state == PDM_OUTPUT_ON);
    in.demand = false;
    PDM_OutputSafety_Step(&s, &p, &in, 60);
    assert(s.state == PDM_OUTPUT_OFF && s.retries == 1);

    p.max_retries = 0;
    p.startup_ms = 0;
    PDM_OutputSafety_Init(&s, 0);
    in = healthy();
    PDM_OutputSafety_Step(&s, &p, &in, 0);
    assert(s.state == PDM_OUTPUT_ON);
    in.overcurrent = true;
    PDM_OutputSafety_Step(&s, &p, &in, 1);
    assert(s.state == PDM_OUTPUT_LOCKOUT);
    p.max_retries = 2;
    p.retry_cooldown_ms = 0;
    PDM_OutputSafety_Init(&s, 0);
    PDM_OutputSafety_Step(&s, &p, &in, 0);
    assert(s.state == PDM_OUTPUT_LOCKOUT && (s.latched_faults & PDM_FAULT_POLICY_INVALID));
    p.retry_cooldown_ms = 100;
    p.startup_ms = UINT32_MAX;
    PDM_OutputSafety_Init(&s, 0);
    PDM_OutputSafety_Step(&s, &p, &in, 0);
    assert(s.state == PDM_OUTPUT_LOCKOUT);
    PDM_OutputSafety_Init(&s, 0);
    PDM_OutputSafety_Step(&s, NULL, &in, 0);
    assert(s.state == PDM_OUTPUT_LOCKOUT);
    PDM_OutputSafety_Init(&s, 0);
    PDM_OutputSafety_Step(&s, &p, NULL, 0);
    assert(s.state == PDM_OUTPUT_LOCKOUT);
    PDM_OutputSafety_Init(NULL, 0);
    PDM_OutputSafety_Step(NULL, NULL, NULL, 0);
    puts("output safety policy: all tests passed (no hardware actuation)");
    return 0;
}
