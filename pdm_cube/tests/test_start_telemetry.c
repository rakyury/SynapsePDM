#include "pdm_start_telemetry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static PDM_StartAdmission scheduler;
static PDM_StartInputs inputs;
static PDM_StartTelemetry telemetry;
static void fixture(unsigned count,unsigned slots,unsigned groups)
{
    PDM_StartConfig c={.channel_count=(uint8_t)count,.group_count=(uint8_t)groups,.max_starts=(uint8_t)slots,
        .total_budget_mA=1000000,.base_load_mA=20000,.request_ttl_ms=60000,.feedback_ttl_ms=1000};
    memset(&inputs,0,sizeof(inputs));
    for(unsigned g=0;g<groups;++g) { c.group_budget_mA[g]=500000; inputs.group_healthy[g]=true; }
    for(unsigned i=0;i<count;++i) {
        c.channels[i]=(PDM_StartChannelConfig){.continuous_mA=10000,.start_mA=30000,
            .start_duration_ms=5000,.cooldown_ms=100,.group=(uint8_t)(i%groups),.commissioned=true};
        inputs.channels[i]=(PDM_StartFeedback){.valid=true,.healthy=true,.confirmed_off=true};
    }
    inputs.valid=inputs.healthy=inputs.link_healthy=true; inputs.total_measured_mA=20000;
    assert(PDM_Start_Init(&scheduler,&c));
    PDM_StartTelemetry_Init(&telemetry,PDM_BoardProfile_Get((uint16_t)count),&scheduler,&inputs,true);
}
static unsigned drain(uint32_t start, uint8_t *payload, uint32_t *crc)
{
    unsigned frames=0, chunk=0;
    for(uint32_t now=start;now<start+900;now+=10) {
        PDM_StartTelemetry_Tick(&telemetry,now);
        const PDM_LinkFrame *frame=PDM_StartTelemetry_Peek(&telemetry);
        if(frame==NULL) continue;
        assert(frame->dlc==8 && !frame->extended && !frame->remote);
        ++frames;
        if(frame->id==0x585) { assert(frame->data[0]==1 && frame->data[1]==0); assert(get32(frame->data+4)==telemetry.sequence); }
        else if(frame->id==0x586) {
            assert(frame->data[2]==chunk);
            unsigned remaining=telemetry.length-chunk*5, take=remaining>5?5:remaining;
            memcpy(payload+chunk*5,frame->data+3,take);
            for(unsigned i=take;i<5;++i) assert(frame->data[3+i]==0);
            ++chunk;
        } else { assert(frame->id==0x587); *crc=get32(frame->data+4); }
        const bool end=frame->id==0x587;
        PDM_StartTelemetry_Pop(&telemetry);
        assert(PDM_StartTelemetry_Peek(&telemetry)==NULL); /* Cannot drain two/tick. */
        if(end) return frames;
    }
    assert(0); return 0;
}
int main(void)
{
    uint8_t payload[408]={0}; uint32_t crc=0;
    PDM_StartTelemetry_Init(&telemetry,PDM_BoardProfile_Hardware(),NULL,NULL,false);
    assert(drain(0,payload,&crc)==30); assert(crc==0xcf598e31u); /* Python zlib golden. */
    assert(payload[0]==14 && payload[1]==14 && payload[2]==12);
    for(unsigned i=3;i<24;++i) assert(payload[i]==0);
    for(unsigned i=0;i<14;++i) {
        const uint8_t *p=payload+24+i*8;
        assert(p[0]==PDM_START_OFF && p[1]==PDM_START_BAD_CONFIG && p[2]==0 && p[3]==255 && get32(p+4)==0);
    }
    for(unsigned count=14;count<=24;count+=10) for(unsigned slots=2;slots<=4;++slots) {
        fixture(count,slots,2);
        for(unsigned i=0;i<count;++i) assert(PDM_Start_Request(&scheduler,(uint8_t)i,1,0,0)==PDM_START_OK);
        PDM_Start_Step(&scheduler,&inputs,0);
        PDM_StartTelemetry_Tick(&telemetry,0);
        /* Mutating the live scheduler mid-transfer cannot change captured bytes. */
        PDM_Start_Reset(&scheduler,1);
        assert(drain(0,payload,&crc)==2+(24+16+count*8+4)/5);
        assert(payload[0]==count && payload[1]==count && payload[2]==11 && payload[3]==slots && payload[4]==slots);
        assert(payload[5]==2 && payload[6]==count-slots);
        assert(get32(payload+16)==20000+slots*30000);
        for(unsigned i=0;i<count;++i) {
            const uint8_t *p=payload+40+i*8;
            assert((p[0]&7u)==(i<slots?PDM_START_STARTING:PDM_START_QUEUED));
            assert((p[0]&128u)==(i<slots?128u:0u));
            assert(p[2]==(i<slots?0:i-slots+1)); assert(p[3]==i%2);
            assert(get32(p+4)==(i<slots?30000u:0u));
        }
        PDM_StartTelemetry_Tick(&telemetry,1000); assert(telemetry.sequence==2);
        /* New capture reflects actual retained FAULT reserves after reset. */
        assert(telemetry.payload[6]==0 && telemetry.payload[4]==slots);
        assert((telemetry.payload[40]&7u)==PDM_START_FAULT && (telemetry.payload[40]&128u));
    }
    fixture(24,4,24); assert(drain(0,payload,&crc)==84); assert(telemetry.length==408);
    /* A new CAN capture must not make an old/invalid measurement look fresh. */
    for(unsigned mode=0;mode<4;++mode) {
        fixture(14,2,2);
        assert(PDM_Start_Request(&scheduler,0,1,0,0)==PDM_START_OK);
        PDM_Start_Step(&scheduler,&inputs,0);
        inputs.channels[0].confirmed_off=false;
        const uint32_t now=mode==0?1001u:10u;
        if(mode==1) inputs.valid=false;
        if(mode==2) inputs.healthy=false;
        if(mode==3) inputs.sampled_ms=now+1u; /* Future time is not fresh. */
        PDM_Start_Step(&scheduler,&inputs,now);
        PDM_StartTelemetry_Tick(&telemetry,now);
        assert(telemetry.payload[2]==31 && get32(telemetry.payload+20)==0);
        assert(telemetry.payload[4]==1 && get32(telemetry.payload+16)==50000);
        assert((telemetry.payload[40]&7u)==PDM_START_FAULT && (telemetry.payload[40]&128u));
    }
    fixture(14,2,2); PDM_StartTelemetry_Tick(&telemetry,0);
    const PDM_LinkFrame old=*PDM_StartTelemetry_Peek(&telemetry);
    PDM_StartTelemetry_Tick(&telemetry,1500); assert(memcmp(&old,PDM_StartTelemetry_Peek(&telemetry),sizeof(old))==0);
    PDM_StartTelemetry_Tick(&telemetry,1501); assert(telemetry.sequence==2 && telemetry.captured_ms==1501 && telemetry.frame_index==0);
    /* Sequence and time arithmetic wrap without mixing a previous transfer. */
    fixture(14,2,2); telemetry.sequence=UINT32_MAX;
    PDM_StartTelemetry_Tick(&telemetry,UINT32_MAX-10); assert(telemetry.sequence==0);
    PDM_StartTelemetry_Tick(&telemetry,1490); assert(telemetry.sequence==1);
    PDM_StartTelemetry_Init(&telemetry,NULL,NULL,NULL,false); PDM_StartTelemetry_Tick(&telemetry,0);
    assert(PDM_StartTelemetry_Peek(&telemetry)==NULL); PDM_StartTelemetry_Tick(NULL,0); PDM_StartTelemetry_Pop(NULL);
    puts("scheduler CAN: coherent actualC14/24 snapshots slots2..4, CRC golden, immutable reserves, max84frames, timeout/wrap PASS");
}
