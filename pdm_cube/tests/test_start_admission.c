#include "pdm_start_admission.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static PDM_StartAdmission scheduler;
static PDM_StartConfig config;
static PDM_StartInputs inputs;
static void stamp(uint32_t now)
{
    inputs.sampled_ms = now;
    for(unsigned i=0;i<config.channel_count;++i) inputs.channels[i].sampled_ms=now;
}
static void fixture(unsigned count, unsigned slots)
{
    memset(&config,0,sizeof(config)); memset(&inputs,0,sizeof(inputs));
    config.channel_count=(uint8_t)count; config.group_count=2; config.max_starts=(uint8_t)slots;
    config.total_budget_mA=1000000; config.base_load_mA=20000;
    config.request_ttl_ms=5000; config.feedback_ttl_ms=100;
    for(unsigned g=0;g<2;++g) {
        config.group_budget_mA[g]=500000; config.group_base_mA[g]=10000;
        inputs.group_healthy[g]=true;
    }
    for(unsigned i=0;i<count;++i) {
        config.channels[i]=(PDM_StartChannelConfig){.continuous_mA=10000,.start_mA=30000,
            .start_duration_ms=100,.cooldown_ms=50,.group=(uint8_t)(i%2),.commissioned=true};
        inputs.channels[i]=(PDM_StartFeedback){.valid=true,.healthy=true,.confirmed_off=true};
    }
    inputs.valid=inputs.healthy=inputs.link_healthy=true; inputs.total_measured_mA=20000;
    assert(PDM_Start_Init(&scheduler,&config));
}
static void request(unsigned i, uint32_t id, uint32_t now)
{ assert(PDM_Start_Request(&scheduler,(uint8_t)i,id,now,now)==PDM_START_OK); }
static void step(uint32_t now) { stamp(now); PDM_Start_Step(&scheduler,&inputs,now); }
static void invariants(void)
{
    unsigned slots=0, queued=0; uint64_t reserve=config.base_load_mA;
    bool seen[24]={0};
    for(unsigned i=0;i<config.channel_count;++i) {
        const PDM_StartChannel *ch=&scheduler.channels[i];
        slots+=ch->slot_reserved; reserve+=ch->reserve_mA;
        queued+=ch->state==PDM_START_QUEUED;
        if(ch->state==PDM_START_STARTING) assert(ch->reserve_mA==config.channels[i].start_mA && ch->slot_reserved);
        if(ch->state==PDM_START_ON) assert(ch->reserve_mA==config.channels[i].continuous_mA && !ch->slot_reserved);
    }
    assert(slots<=config.max_starts && slots==scheduler.starts_reserved);
    assert(reserve==scheduler.total_reserved_mA && reserve<=config.total_budget_mA);
    assert(queued==scheduler.queue_count);
    for(unsigned i=0;i<scheduler.queue_count;++i) { assert(!seen[scheduler.queue[i]]); seen[scheduler.queue[i]]=true; }
}
int main(void)
{
    for(unsigned count=14;count<=24;count+=10) for(unsigned slots=2;slots<=4;++slots) {
        fixture(count,slots);
        for(unsigned i=0;i<count;++i) { request(i,1,0); request(i,1,0); }
        step(0); assert(scheduler.starts_reserved==slots); assert(scheduler.queue_count==count-slots);
        for(unsigned i=0;i<slots;++i) assert(scheduler.channels[i].state==PDM_START_STARTING);
        invariants();
        for(uint32_t t=10;t<200 && scheduler.queue_count;t+=10) {
            for(unsigned i=0;i<count;++i) if(scheduler.channels[i].state==PDM_START_STARTING) {
                inputs.channels[i].confirmed_off=false; inputs.channels[i].start_complete=true;
                inputs.channels[i].measured_mA=10000;
            }
            step(t); invariants();
        }
        assert(scheduler.queue_count==0);
    }
    /* 4x30A adds to the existing20A; 120A total cannot authorize four starts. */
    fixture(24,4); config.total_budget_mA=120000; assert(PDM_Start_Init(&scheduler,&config));
    for(unsigned i=0;i<4;++i) request(i,1,0);
    step(0); assert(scheduler.starts_reserved==3 && scheduler.total_reserved_mA==110000);
    assert(scheduler.channels[3].reason==PDM_START_TOTAL_BUDGET);
    /* ON uses conservative10A even if measured current is0. */
    inputs.channels[0].confirmed_off=false; inputs.channels[0].start_complete=true; step(1);
    assert(scheduler.channels[0].state==PDM_START_ON && scheduler.channels[3].state==PDM_START_STARTING);
    assert(scheduler.total_reserved_mA==120000); invariants();
    fixture(24,4); config.group_budget_mA[0]=40000; assert(PDM_Start_Init(&scheduler,&config));
    request(0,1,0); request(2,1,0); request(1,1,0); step(0);
    assert(scheduler.channels[2].reason==PDM_START_GROUP_BUDGET);
    assert(scheduler.channels[1].state==PDM_START_QUEUED); /* no bypass */
    fixture(14,2); config.channels[5].priority=0; config.channels[0].priority=1;
    config.channels[1].priority=1; assert(PDM_Start_Init(&scheduler,&config));
    request(0,1,0); request(1,1,0); request(5,1,0); step(0);
    assert(scheduler.channels[5].state==PDM_START_STARTING && scheduler.channels[0].state==PDM_START_STARTING);
    /* Same-tick/pre-start completion cannot free a slot. */
    fixture(14,2); request(0,1,0); request(1,1,0); request(2,1,0); step(0);
    inputs.channels[0].confirmed_off=false; inputs.channels[0].start_complete=true;
    PDM_Start_Step(&scheduler,&inputs,1); assert(scheduler.starts_reserved==2);
    step(99); assert(scheduler.channels[0].state==PDM_START_ON);
    step(100); assert(scheduler.channels[1].state==PDM_START_FAULT);
    assert(scheduler.channels[1].reserve_mA==30000 && scheduler.channels[1].slot_reserved);
    PDM_Start_Step(&scheduler,&inputs,100); assert(scheduler.channels[1].reserve_mA==30000);
    step(101); assert(scheduler.channels[1].reserve_mA==0 && scheduler.channels[1].state==PDM_START_FAULT);
    assert(PDM_Start_Request(&scheduler,1,2,101,101)==PDM_START_FAULT_LATCHED);
    assert(PDM_Start_ClearFault(&scheduler,1,&inputs,101)); request(1,3,101); step(101);
    assert(scheduler.channels[1].reason==PDM_START_COOLDOWN);
    step(151); assert(scheduler.channels[1].state==PDM_START_STARTING);
    /* Fault with unknown OFF holds reserve indefinitely, health cannot restart. */
    fixture(14,2); request(0,1,0); step(0); inputs.channels[0].confirmed_off=false;
    inputs.channels[0].healthy=false; step(1); assert(scheduler.channels[0].state==PDM_START_FAULT);
    inputs.channels[0].healthy=true; step(200); assert(scheduler.channels[0].reserve_mA==30000);
    assert(!PDM_Start_ClearFault(&scheduler,0,&inputs,200));
    inputs.channels[0].confirmed_off=true; step(201); assert(scheduler.channels[0].reserve_mA==0);
    inputs.inhibited=true; assert(!PDM_Start_ClearFault(&scheduler,0,&inputs,201));
    inputs.inhibited=false; inputs.group_healthy[0]=false;
    assert(!PDM_Start_ClearFault(&scheduler,0,&inputs,201)); inputs.group_healthy[0]=true;
    inputs.sampled_ms=0; assert(!PDM_Start_ClearFault(&scheduler,0,&inputs,201));
    stamp(201); assert(PDM_Start_ClearFault(&scheduler,0,&inputs,201));
    assert(PDM_Start_Request(&scheduler,0,1,201,201)==PDM_START_OLD_REQUEST);
    /* Contradictory OFF proof cannot conceal load or release fault reservation. */
    fixture(14,2); request(0,1,0); inputs.channels[13].measured_mA=80000; step(0);
    assert(scheduler.starts_reserved==0 && scheduler.channels[0].reason==PDM_START_UNKNOWN_OFF);
    fixture(14,2); request(0,1,0); step(0); PDM_Start_Cancel(&scheduler,0,1);
    inputs.channels[0].measured_mA=1; step(2); assert(scheduler.channels[0].reserve_mA==30000);
    inputs.channels[0].measured_mA=0; step(3); assert(scheduler.channels[0].reserve_mA==0);
    /* Independent total detects additional unmodeled load even within budget. */
    fixture(14,2); config.base_load_mA=0; config.group_base_mA[0]=config.group_base_mA[1]=0;
    config.total_budget_mA=100000; assert(PDM_Start_Init(&scheduler,&config));
    inputs.total_measured_mA=80000; request(0,1,0); step(0);
    assert(scheduler.starts_reserved==0 && scheduler.channels[0].reason==PDM_START_UNHEALTHY);
    /* Completion lowering the envelope must recheck the independent meter. */
    fixture(14,2); request(0,1,0); step(0); inputs.total_measured_mA=45000;
    inputs.channels[0].confirmed_off=false; inputs.channels[0].start_complete=true;
    request(1,1,1); step(1); assert(scheduler.channels[0].state==PDM_START_ON);
    assert(scheduler.channels[1].state==PDM_START_QUEUED && scheduler.channels[1].reason==PDM_START_UNHEALTHY);
    /* The current envelope is a ceiling; exceeding it wins over completion. */
    fixture(14,2); request(0,1,0); step(0); inputs.channels[0].confirmed_off=false;
    inputs.channels[0].start_complete=true; inputs.channels[0].measured_mA=30001; step(1);
    assert(scheduler.channels[0].state==PDM_START_FAULT && scheduler.channels[0].reserve_mA==30000);
    /* Missing, future and stale feedback are not equivalent to zero current. */
    fixture(14,2); inputs.channels[13].valid=false; request(0,1,0); step(0);
    assert(scheduler.channels[0].reason==PDM_START_UNKNOWN_OFF);
    inputs.channels[13].valid=true; stamp(0); inputs.channels[13].sampled_ms=1;
    PDM_Start_Step(&scheduler,&inputs,0); assert(scheduler.starts_reserved==0);
    stamp(0); PDM_Start_Step(&scheduler,&inputs,101); assert(scheduler.starts_reserved==0);
    /* Duplicate and newer demand do not extend the first queued timestamp. */
    fixture(14,2); inputs.inhibited=true; request(0,1,0); step(100);
    (void)PDM_Start_Request(&scheduler,0,1,4999,4999);
    (void)PDM_Start_Request(&scheduler,0,2,4999,4999);
    assert(scheduler.channels[0].requested_ms==0); step(5001); assert(scheduler.queue_count==0);
    assert(PDM_Start_Request(&scheduler,0,1,5001,5001)==PDM_START_OLD_REQUEST);
    assert(PDM_Start_Request(&scheduler,0,3,5002,5001)==PDM_START_EXPIRED);
    assert(PDM_Start_Request(&scheduler,0,3,5002,5001)==PDM_START_OLD_REQUEST);
    /* Reset preserves live reserve, drops pending authorization and rejects old commands. */
    fixture(14,2); request(0,1,0); request(1,1,0); request(2,1,0); step(0);
    PDM_Start_Reset(&scheduler,1); assert(scheduler.queue_count==0 && scheduler.starts_reserved==2);
    stamp(1); PDM_Start_Step(&scheduler,&inputs,1); assert(scheduler.starts_reserved==2);
    assert(PDM_Start_Request(&scheduler,3,1,0,1)==PDM_START_EXPIRED);
    step(2); assert(scheduler.starts_reserved==0 && scheduler.queue_count==0);
    (void)PDM_Start_Request(&scheduler,2,1,2,2); assert(scheduler.queue_count==0);
    request(3,2,2); step(2); assert(scheduler.channels[3].state==PDM_START_STARTING);
    /* Link loss discards waiting commands, active states latch fault with reserve. */
    inputs.link_healthy=false; request(4,1,3); step(3);
    assert(scheduler.queue_count==0 && scheduler.channels[3].state==PDM_START_FAULT);
    assert(scheduler.channels[3].reserve_mA==30000);
    /* Explicit cancel must be followed by newer OFF confirmation. */
    fixture(14,2); request(0,1,0); step(0); PDM_Start_Cancel(&scheduler,0,1);
    stamp(1); PDM_Start_Step(&scheduler,&inputs,1); assert(scheduler.starts_reserved==1);
    step(2); assert(scheduler.starts_reserved==0 && scheduler.channels[0].state==PDM_START_FAULT);
    /* Time and request serial arithmetic wrap safely. */
    fixture(14,2); request(0,UINT32_MAX,UINT32_MAX-10); step(UINT32_MAX-10);
    inputs.channels[0].confirmed_off=false; inputs.channels[0].start_complete=true; step(1);
    assert(scheduler.channels[0].state==PDM_START_ON);
    assert(PDM_Start_Request(&scheduler,0,0,1,1)==PDM_START_OK);
    PDM_Start_Cancel(&scheduler,0,2); inputs.channels[0].confirmed_off=true; step(3);
    assert(PDM_Start_ClearFault(&scheduler,0,&inputs,3)); request(0,1,3); step(52);
    assert(scheduler.channels[0].reason==PDM_START_COOLDOWN); step(53);
    assert(scheduler.channels[0].state==PDM_START_STARTING);
    /* Every explicit unknown or unrepresentable configuration fails closed. */
    fixture(14,2); config.channels[0].commissioned=false; assert(!PDM_Start_Init(&scheduler,&config));
    assert(PDM_Start_Request(&scheduler,0,1,0,0)==PDM_START_BAD_CONFIG);
    fixture(14,2); config.channel_count=25; assert(!PDM_Start_Init(&scheduler,&config));
    fixture(14,2); config.max_starts=1; assert(!PDM_Start_Init(&scheduler,&config));
    fixture(14,2); config.group_count=25; assert(!PDM_Start_Init(&scheduler,&config));
    fixture(14,2); config.group_base_mA[0]=20001; assert(!PDM_Start_Init(&scheduler,&config));
    fixture(14,2); config.group_budget_mA[0]=0; assert(!PDM_Start_Init(&scheduler,&config));
    fixture(14,2); config.channels[0].start_mA=UINT32_MAX; assert(!PDM_Start_Init(&scheduler,&config));
    fixture(14,2); config.channels[0].start_duration_ms=0; assert(!PDM_Start_Init(&scheduler,&config));
    fixture(14,2); config.feedback_ttl_ms=UINT32_MAX; assert(!PDM_Start_Init(&scheduler,&config));
    fixture(14,2); config.channels[0].priority=4; assert(!PDM_Start_Init(&scheduler,&config));
    fixture(14,2); config.channels[0].group=2; assert(!PDM_Start_Init(&scheduler,&config));
    fixture(14,2); assert(PDM_Start_Request(&scheduler,14,1,0,0)==PDM_START_BAD_CHANNEL);
    assert(!PDM_Start_Init(NULL,&config)); PDM_Start_Step(NULL,NULL,0); PDM_Start_Reset(NULL,0);
    /* Bounded seeded command/fault sequences check unique queue and reserves. */
    fixture(24,4); uint32_t seed=0x123abc45, ids[24]={0};
    for(uint32_t now=1;now<10000;++now) {
        seed=seed*1664525u+1013904223u; unsigned i=seed%24;
        switch((seed>>8)%5) {
            case 0: (void)PDM_Start_Request(&scheduler,(uint8_t)i,++ids[i],now,now); break;
            case 1: PDM_Start_Cancel(&scheduler,(uint8_t)i,now); break;
            case 2: inputs.channels[i].confirmed_off=false; inputs.channels[i].start_complete=true; break;
            case 3: inputs.channels[i].confirmed_off=true; inputs.channels[i].start_complete=false; break;
            default: (void)PDM_Start_ClearFault(&scheduler,(uint8_t)i,&inputs,now); break;
        }
        step(now); invariants();
    }
    puts("start admission: 14/24 slots2..4 FIFO/priority budgets timestamps faults reset wrap and seeded invariants PASS");
}
