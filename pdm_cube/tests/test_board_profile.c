#include "pdm_board_profile.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static PDM_BoardProfile copy14(void)
{ return *PDM_BoardProfile_Get(14); }
static PDM_BoardProfile copy24(void)
{ return *PDM_BoardProfile_Get(24); }

int main(void)
{
    const PDM_BoardProfile *old = PDM_BoardProfile_Get(14), *future = PDM_BoardProfile_Get(24);
    assert(old == PDM_BoardProfile_Hardware());
    assert(PDM_BOARD_HARDWARE_CHANNEL_COUNT == 14 && PDM_BOARD_MAX_CHANNELS == 24);
    assert(!PDM_BoardProfile_Get(0) && !PDM_BoardProfile_Get(13) && !PDM_BoardProfile_Get(65535));
    assert(!PDM_BoardProfile_Validate(NULL, false));
    assert(PDM_BoardProfile_Validate(old, true));
    assert(PDM_BoardProfile_Validate(future, false));
    assert(!PDM_BoardProfile_Validate(future, true));
    assert(old->group_count == 0 && future->group_count == 0);
    /* Independent transcription of OutputHandler GPIO order and ADC scan. */
    const uint8_t gate_pin[] = {10,9,6,5,4,3,2,15,14,13,12,2,1,0};
    const uint8_t current_port[] = {PDM_PORT_A,PDM_PORT_A,PDM_PORT_A,PDM_PORT_A,
        PDM_PORT_A,PDM_PORT_A,PDM_PORT_A,PDM_PORT_B,PDM_PORT_B,PDM_PORT_A,
        PDM_PORT_C,PDM_PORT_C,PDM_PORT_C,PDM_PORT_C};
    const uint8_t current_pin[] = {0,1,2,3,4,5,6,1,0,7,3,2,1,0};
    const uint8_t adc[] = {0,1,2,3,4,5,6,9,8,7,13,12,11,10};
    for (unsigned i=0; i<14; ++i) {
        const PDM_BoardChannel *c = &old->channels[i];
        assert(c->number == i+1 && c->gate.port == (i<7 ? PDM_PORT_G : PDM_PORT_F));
        assert(c->gate.pin == gate_pin[i] && c->current.port == current_port[i]);
        assert(c->current.pin == current_pin[i] && c->adc_channel == adc[i]);
        assert(c->safe_level == 0 && c->budget_group == PDM_BOARD_UNASSIGNED);
        assert(c->capability == PDM_CAPABILITY_UNCOMMISSIONED && c->target_peak_ma == 0);
        assert(c->target_continuous_min_ma == 0 && c->target_continuous_max_ma == 0);
    }
    for (unsigned i=0; i<24; ++i) {
        const PDM_BoardChannel *c = &future->channels[i];
        assert(c->number == i+1 && c->safe_level == 0);
        assert(c->gate.port == 255 && c->gate.pin == 255 && c->current.port == 255 && c->current.pin == 255);
        assert(c->adc_channel == 255 && c->budget_group == 255);
        assert(c->capability == (i<10 ? PDM_CAPABILITY_START_TARGET : PDM_CAPABILITY_CONTINUOUS_TARGET));
        assert(c->target_peak_ma == (i<10 ? 30000u : 0u));
        assert(c->target_continuous_min_ma == (i<10 ? 0u : 17000u));
        assert(c->target_continuous_max_ma == (i<10 ? 0u : 20000u));
    }
    PDM_BoardProfile p;
    for (unsigned count=0; count<256; ++count) {
        p=copy14(); p.channel_count=(uint8_t)count;
        assert(PDM_BoardProfile_Validate(&p,false) == (count==14));
        p=copy24(); p.channel_count=(uint8_t)count;
        assert(PDM_BoardProfile_Validate(&p,false) == (count==24));
    }
    p=copy14(); p.id=42; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy14(); p.schema_version=0; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy24(); p.simulation_only=false; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy14(); p.simulation_only=true; assert(!PDM_BoardProfile_Validate(&p,true));
    p=copy24(); p.channels[23].number=1; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy24(); p.channels[0].number=0; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy24(); p.channels[0].number=25; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy24(); p.channels[0].safe_level=1; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy24(); p.channels[0].target_peak_ma=30001; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy24(); p.channels[10].target_continuous_min_ma=20001; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy24(); p.channels[0].capability=(PDM_BoardCapability)255; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy24(); p.group_count=25; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy24(); p.channels[0].budget_group=0; assert(!PDM_BoardProfile_Validate(&p,false));
    p.group_count=2; assert(PDM_BoardProfile_Validate(&p,false));
    p.channels[0].budget_group=1; assert(PDM_BoardProfile_Validate(&p,false));
    p.channels[0].budget_group=2; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy24(); p.channels[0].gate=(PDM_BoardPin){PDM_PORT_G,10}; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy24(); p.channels[0].adc_channel=0; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy14(); p.channels[0].gate.port=255; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy14(); p.channels[0].gate.pin=16; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy14(); p.channels[0].gate.port=9; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy14(); p.channels[0].adc_channel=1; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy14(); p.channels[0].gate=(PDM_BoardPin){PDM_PORT_H,0};
    assert(PDM_BoardProfile_Validate(&p,false)); assert(!PDM_BoardProfile_Validate(&p,true));
    /* Reject same-channel and all cross-channel gate/sense collisions. */
    p=copy14(); p.channels[0].gate=p.channels[0].current; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy14(); p.channels[0].gate=p.channels[1].gate; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy14(); p.channels[0].gate=p.channels[1].current; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy14(); p.channels[1].gate=p.channels[0].current; assert(!PDM_BoardProfile_Validate(&p,false));
    p=copy14(); p.channels[1].current=p.channels[0].current; p.channels[1].adc_channel=0;
    assert(!PDM_BoardProfile_Validate(&p,false));
    /* Candidate mutations never mutate the immutable factory descriptors. */
    assert(PDM_BoardProfile_Validate(old,true) && PDM_BoardProfile_Validate(future,false));
    puts("Board profiles: exact14 pin/ADC map, simulation24 targets, malformed schema/pin/group rejection PASS");
}
