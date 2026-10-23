#include "pdm_start_telemetry.h"
#include <stddef.h>
#include <string.h>
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) { for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t crc_bytes(uint32_t crc, const uint8_t *p, unsigned length)
{
    for(unsigned i=0;i<length;++i) {
        crc ^= p[i];
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return crc;
}
void PDM_StartTelemetry_Init(PDM_StartTelemetry *t, const PDM_BoardProfile *profile,
    const PDM_StartAdmission *scheduler, const PDM_StartInputs *inputs, bool simulation)
{
    if(t==NULL) return;
    memset(t,0,sizeof(*t)); t->profile=profile; t->scheduler=scheduler; t->inputs=inputs; t->simulation=simulation;
}
static void capture(PDM_StartTelemetry *t, uint32_t now)
{
    const PDM_StartAdmission *s=t->scheduler;
    const bool available=t->simulation && s!=NULL && t->inputs!=NULL && s->valid &&
        s->config.channel_count==t->profile->channel_count && s->config.group_count>0 &&
        s->config.group_count<=24 && s->config.max_starts>=2 && s->config.max_starts<=4;
    const unsigned count=t->profile->channel_count, groups=available?s->config.group_count:0;
    const bool measurement_available=available && t->inputs->valid && t->inputs->healthy &&
        (uint32_t)(now-t->inputs->sampled_ms)<=s->config.feedback_ttl_ms;
    memset(t->payload,0,sizeof(t->payload));
    t->payload[0]=(uint8_t)t->profile->id; t->payload[1]=(uint8_t)count;
    t->payload[2]=(uint8_t)(8u|(t->simulation?2u:0u)|(available?1u:4u));
    if(available && (t->inputs->inhibited || !t->inputs->healthy || !t->inputs->valid || !t->inputs->link_healthy)) t->payload[2]|=4u;
    if(available && !measurement_available) t->payload[2]|=20u; /* Unknown current + inhibit; retain reservations. */
    put32(t->payload+8,now);
    if(available) {
        t->payload[3]=s->config.max_starts; t->payload[4]=s->starts_reserved;
        t->payload[5]=(uint8_t)groups; t->payload[6]=s->queue_count;
        put32(t->payload+12,s->config.total_budget_mA); put32(t->payload+16,s->total_reserved_mA);
        if(measurement_available) put32(t->payload+20,t->inputs->total_measured_mA);
        for(unsigned g=0;g<groups;++g) {
            put32(t->payload+24+g*8,s->config.group_budget_mA[g]);
            put32(t->payload+28+g*8,s->group_reserved_mA[g]);
        }
    }
    for(unsigned i=0;i<count;++i) {
        uint8_t *p=t->payload+24+groups*8+i*8;
        if(!available) { p[0]=PDM_START_OFF; p[1]=PDM_START_BAD_CONFIG; p[3]=255; continue; }
        const PDM_StartChannel *ch=&s->channels[i];
        p[0]=(uint8_t)(ch->state|(ch->slot_reserved?128u:0u)); p[1]=(uint8_t)ch->reason;
        p[3]=s->config.channels[i].group; put32(p+4,ch->reserve_mA);
        for(unsigned q=0;q<s->queue_count && q<24;++q) if(s->queue[q]==i) p[2]=(uint8_t)(q+1);
    }
    t->length=(uint16_t)(24+groups*8+count*8);
    ++t->sequence; t->captured_ms=now; t->started=true; t->active=true; t->frame_index=0;
    memset(t->begin,0,8); t->begin[0]=1; put16(t->begin+2,t->length); put32(t->begin+4,t->sequence);
    t->crc=~crc_bytes(crc_bytes(UINT32_MAX,t->begin,8),t->payload,t->length);
}
void PDM_StartTelemetry_Tick(PDM_StartTelemetry *t, uint32_t now)
{
    if(t==NULL || t->profile==NULL || !PDM_BoardProfile_Validate(t->profile,false) ||
        (t->profile->channel_count!=14 && t->profile->channel_count!=24)) return;
    if(t->active && (uint32_t)(now-t->captured_ms)>PDM_START_TELEMETRY_DEADLINE_MS) {
        t->active=false; t->frame_ready=false;
    }
    if(!t->active && (!t->started || (uint32_t)(now-t->captured_ms)>=PDM_START_TELEMETRY_PERIOD_MS)) capture(t,now);
    if(!t->active || t->frame_ready) return;
    t->frame=(PDM_LinkFrame){.dlc=8};
    const unsigned chunks=(t->length+4u)/5u;
    if(t->frame_index==0) { t->frame.id=0x585; memcpy(t->frame.data,t->begin,8); }
    else if(t->frame_index<=chunks) {
        t->frame.id=0x586; put16(t->frame.data,(uint16_t)t->sequence);
        const unsigned chunk=t->frame_index-1u, offset=chunk*5;
        t->frame.data[2]=(uint8_t)chunk;
        unsigned count=t->length-offset; if(count>5) count=5;
        memcpy(t->frame.data+3,t->payload+offset,count);
    } else { t->frame.id=0x587; put32(t->frame.data,t->sequence); put32(t->frame.data+4,t->crc); }
    t->frame_ready=true;
}
const PDM_LinkFrame *PDM_StartTelemetry_Peek(const PDM_StartTelemetry *t)
{ return t!=NULL && t->active && t->frame_ready ? &t->frame : NULL; }
void PDM_StartTelemetry_Pop(PDM_StartTelemetry *t)
{
    if(t==NULL || !t->frame_ready) return;
    t->frame_ready=false;
    if(t->frame_index>(t->length+4u)/5u) t->active=false;
    else ++t->frame_index;
    /* A subsequent frame is prepared only by Tick: <=1 telemetry frame/tick. */
}
