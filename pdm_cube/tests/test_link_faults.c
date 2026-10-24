/* Exercise the exact stdio harness fault parser and actual scheduler, not a
 * second model. Hardware GPIO is absent from this translation unit. */
#include <assert.h>
#define main link_simulator_main
#include "link_simulator.c"
#undef main
static void inject(const char *line)
{
    char command[16]; assert(sscanf(line,"%15s",command)==1);
    assert(start_inject(command,line));
}
static void tick_at(uint32_t now)
{ simulator_now=now; start_tick(); }
static void fixture(unsigned count,unsigned slots)
{
    reset(); const uint32_t config[]={count,slots,300000,200000};
    assert(start_configure(config));
    for(unsigned i=0;i<count;++i) assert(PDM_Start_Request(&start_scheduler,(uint8_t)i,1,0,0)==PDM_START_OK);
    tick_at(10); assert(start_scheduler.starts_reserved==slots);
}
static void gates_off(void)
{ for(unsigned i=0;i<14;++i) assert(Channel_GetValue(&channels,(uint16_t)(200+i))==0); }
int main(void)
{
    for(unsigned count=14;count<=24;count+=10) for(unsigned slots=2;slots<=4;++slots) {
        fixture(count,slots);
        const uint32_t reserve=20000+slots*30000;
        inject("STARTLINK 0"); tick_at(20);
        assert(start_scheduler.queue_count==0 && start_scheduler.total_reserved_mA==reserve);
        for(unsigned i=0;i<slots;++i) assert(start_scheduler.channels[i].state==PDM_START_FAULT);
        inject("STARTLINK 1"); tick_at(30);
        assert(start_scheduler.queue_count==0 && start_scheduler.starts_reserved==slots); gates_off();
        fixture(count,slots); inject("STARTFREEZE 1"); tick_at(1010);
        assert(start_scheduler.channels[0].state==PDM_START_STARTING); /* TTL inclusive. */
        tick_at(1011); assert(start_scheduler.channels[0].state==PDM_START_FAULT);
        assert(start_scheduler.total_reserved_mA==reserve);
        PDM_StartTelemetry_Tick(&start_telemetry,1011);
        assert(start_telemetry.payload[2]==31 && start_telemetry.payload[20]==0);
        inject("STARTFREEZE 0"); tick_at(1020);
        assert(start_scheduler.channels[0].state==PDM_START_FAULT && start_scheduler.starts_reserved==slots);
        fixture(count,slots); inject("STARTCURRENT 1 30001"); tick_at(20);
        assert(start_scheduler.channels[0].state==PDM_START_FAULT);
        start_inputs.channels[0].confirmed_off=true;
        inject("STARTCURRENT 1 1"); tick_at(30);
        assert(start_scheduler.channels[0].reserve_mA==30000); /* Contradictory OFF. */
        inject("STARTCURRENT 1 0"); tick_at(40);
        assert(start_scheduler.channels[0].reserve_mA==0 && start_scheduler.channels[0].state==PDM_START_FAULT);
        fixture(count,slots); inject("STARTGROUP 1 0"); tick_at(20);
        assert(start_scheduler.channels[0].state==PDM_START_FAULT);
        assert(start_scheduler.channels[1].state==PDM_START_STARTING);
        inject("STARTGROUP 1 1"); tick_at(30); assert(start_scheduler.channels[0].state==PDM_START_FAULT);
        const char *faults[]={"STARTHEALTH 0","STARTINHIBIT 1","STARTSENSOR 1 0","STARTTOTAL 25000000"};
        for(unsigned i=0;i<4;++i) {
            fixture(count,slots); inject(faults[i]); tick_at(20);
            assert(start_scheduler.channels[0].state==PDM_START_FAULT);
            assert(start_scheduler.channels[0].reserve_mA==30000); gates_off();
        }
    }
    fixture(24,4);
    const char *bad[]={"STARTLINK 2","STARTFREEZE -1","STARTCURRENT 0 1",
        "STARTCURRENT 25 1","STARTCURRENT 1 1000001","STARTCURRENT 1 1 extra",
        "STARTTOTAL 25000001","STARTTOTAL 4294967296","STARTGROUP 3 0",
        "STARTGROUP 1 2","STARTSENSOR 1 2","STARTHEALTH 0 extra","STARTAUTO extra"};
    const PDM_StartInputs before=start_inputs;
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        char command[16]; assert(sscanf(bad[i],"%15s",command)==1);
        assert(!start_inject(command,bad[i]));
        assert(memcmp(&before,&start_inputs,sizeof(before))==0);
        assert(!feedback_frozen && !total_override);
    }
    inject("STARTTOTAL 25000000"); tick_at(20); assert(start_inputs.total_measured_mA==25000000);
    inject("STARTAUTO"); tick_at(30); assert(start_inputs.total_measured_mA==140000);
    inject("STARTFREEZE 1"); inject("STARTINHIBIT 1");
    const uint32_t config[]={14,2,300000,200000}; assert(start_configure(config));
    assert(!feedback_frozen && !total_override && !start_inputs.inhibited && start_inputs.link_healthy);
    reset(); assert(!start_inject("STARTLINK","STARTLINK 0")); gates_off();
    puts("actual harness fault controls: 6 profile/slot combinations, link loss, expiry, current contradiction, groups, bounds, no replay, OFF PASS");
    return 0;
}
