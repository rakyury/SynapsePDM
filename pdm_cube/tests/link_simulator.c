/* stdin/stdout harness for the ACTUAL protocol and Racing Dash C VM.
 * RX <now_decimal> <id_hex> <16hexbytes> [dlc extended remote]
 * TICK <now_decimal>, SET <channel_decimal> <value_decimal>, RESET
 * Every command prints zero or more '<idhex> <16hexbytes>' and then END.
 * SET permits only diagnostic channels240/241/242/244, never output gates.
 */
#include "../src/pdm_link.h"
#include "../src/pdm_adc.h"
#include "../src/pdm_start_telemetry.h"
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static PDM_Link link;
static ChannelEngine_t channels;
static VM_t vm;
static PDM_ADC adc;
static PDM_StartTelemetry start_telemetry;
static PDM_StartAdmission start_scheduler;
static PDM_StartInputs start_inputs;
static bool start_enabled;
static bool feedback_frozen, total_override;
static uint32_t injected_total;
static uint32_t simulator_now, request_ids[24], tx_limit=3;
/* Explicit simulator fixtures, never device calibration defaults. */
static PDM_ADCCalibration calibration = {1500, 1000, 1300};
static uint32_t received;
static void reset(void)
{
    Channel_Init(&channels); VM_Init(&vm, &channels); PDM_Link_Init(&link, &channels, &vm);
    PDM_ADC_Init(&adc, calibration);
    PDM_Link_SetADCReader(&link, PDM_ADC_Read, &adc);
    PDM_StartTelemetry_Init(&start_telemetry,PDM_BoardProfile_Hardware(),NULL,NULL,false);
    PDM_Link_SetStartTelemetry(&link,&start_telemetry);
    start_enabled=false; simulator_now=0; memset(request_ids,0,sizeof(request_ids)); tx_limit=3;
    feedback_frozen=false; total_override=false; injected_total=0;
    received = 0;
}
static void start_refresh(void)
{
    if(!feedback_frozen) start_inputs.sampled_ms=simulator_now;
    start_inputs.total_measured_mA=start_scheduler.config.base_load_mA;
    for(unsigned i=0;i<start_scheduler.config.channel_count;++i) {
        if(!feedback_frozen) start_inputs.channels[i].sampled_ms=simulator_now;
        start_inputs.total_measured_mA+=start_inputs.channels[i].measured_mA;
    }
    if(total_override) start_inputs.total_measured_mA=injected_total;
}
static void start_tick(void)
{
    if(!start_enabled) return;
    PDM_StartState before[24];
    for(unsigned i=0;i<start_scheduler.config.channel_count;++i) before[i]=start_scheduler.channels[i].state;
    start_refresh(); PDM_Start_Step(&start_scheduler,&start_inputs,simulator_now);
    for(unsigned i=0;i<start_scheduler.config.channel_count;++i)
        if(start_scheduler.channels[i].state==PDM_START_STARTING && before[i]!=PDM_START_STARTING) {
            start_inputs.channels[i].confirmed_off=false;
            start_inputs.channels[i].measured_mA=start_scheduler.config.channels[i].start_mA;
        }
    start_refresh();
}
static bool start_configure(const uint32_t v[4])
{
    if((v[0]!=14 && v[0]!=24) || v[1]<2 || v[1]>4 || v[2]<20000 || v[2]>1000000 ||
       v[3]<10000 || v[3]>1000000) return false;
    const PDM_BoardProfile *profile=PDM_BoardProfile_Get((uint16_t)v[0]);
    PDM_StartConfig config={.channel_count=(uint8_t)v[0],.group_count=2,.max_starts=(uint8_t)v[1],
        .total_budget_mA=v[2],.base_load_mA=20000,.request_ttl_ms=60000,.feedback_ttl_ms=1000};
    memset(&start_inputs,0,sizeof(start_inputs)); memset(request_ids,0,sizeof(request_ids));
    feedback_frozen=false; total_override=false; injected_total=0;
    for(unsigned g=0;g<2;++g) { config.group_budget_mA[g]=v[3]; config.group_base_mA[g]=10000; start_inputs.group_healthy[g]=true; }
    for(unsigned i=0;i<v[0];++i) {
        config.channels[i]=(PDM_StartChannelConfig){.continuous_mA=10000,.start_mA=30000,
            .start_duration_ms=5000,.cooldown_ms=100,.group=(uint8_t)(i%2),.commissioned=true};
        if(v[0]==24 && i>=10) { config.channels[i].continuous_mA=17000; config.channels[i].start_mA=20000; }
        start_inputs.channels[i]=(PDM_StartFeedback){.valid=true,.healthy=true,.confirmed_off=true};
    }
    if(!PDM_Start_Init(&start_scheduler,&config)) return false;
    start_inputs.valid=start_inputs.healthy=start_inputs.link_healthy=true; start_enabled=true; start_refresh();
    /* Simulator-only new fixture. Preserve transport generation across source
       changes; abandon any old partial snapshot, never merge configurations. */
    const uint32_t sequence=start_telemetry.sequence;
    PDM_StartTelemetry_Init(&start_telemetry,profile,&start_scheduler,&start_inputs,true);
    start_telemetry.sequence=sequence;
    return true;
}
static bool words(const char *line, uint32_t *values, unsigned count)
{
    while (*line && !isspace((unsigned char)*line)) ++line;
    for (unsigned i = 0; i < count; ++i) {
        while (isspace((unsigned char)*line)) ++line;
        if (!isdigit((unsigned char)*line)) return false;
        char *end;
        errno = 0;
        unsigned long value = strtoul(line, &end, 10);
        if (errno || value > UINT32_MAX || (*end && !isspace((unsigned char)*end))) return false;
        values[i] = (uint32_t)value; line = end;
    }
    while (isspace((unsigned char)*line)) ++line;
    return *line == '\0';
}
/* Test-only fault surface. Validate the complete command before mutation. */
static bool start_inject(const char *command, const char *line)
{
    uint32_t v[2];
    if(!start_enabled) return false;
    if(strcmp(command,"STARTAUTO")==0) {
        if(!words(line,NULL,0)) return false;
        total_override=false; return true;
    }
    if(strcmp(command,"STARTTOTAL")==0) {
        if(!words(line,v,1) || v[0]>25000000u) return false;
        total_override=true; injected_total=v[0]; return true;
    }
    if(strcmp(command,"STARTCURRENT")==0 || strcmp(command,"STARTSENSOR")==0) {
        if(!words(line,v,2) || v[0]<1 || v[0]>start_scheduler.config.channel_count) return false;
        if(strcmp(command,"STARTCURRENT")==0) {
            if(v[1]>1000000u) return false;
            start_inputs.channels[v[0]-1].measured_mA=v[1];
        } else {
            if(v[1]>1) return false;
            start_inputs.channels[v[0]-1].valid=v[1]!=0;
        }
        return true;
    }
    if(strcmp(command,"STARTGROUP")==0) {
        if(!words(line,v,2) || v[0]<1 || v[0]>start_scheduler.config.group_count || v[1]>1) return false;
        start_inputs.group_healthy[v[0]-1]=v[1]!=0; return true;
    }
    if(!words(line,v,1) || v[0]>1) return false;
    if(strcmp(command,"STARTLINK")==0) start_inputs.link_healthy=v[0]!=0;
    else if(strcmp(command,"STARTFREEZE")==0) feedback_frozen=v[0]!=0;
    else if(strcmp(command,"STARTHEALTH")==0) start_inputs.healthy=v[0]!=0;
    else if(strcmp(command,"STARTINHIBIT")==0) start_inputs.inhibited=v[0]!=0;
    else return false;
    return true;
}
static int nibble(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
int main(void)
{
    char line[256], command[16];
    reset();
    while (fgets(line, sizeof(line), stdin) != NULL) {
        bool valid = false;
        if (sscanf(line, "%15s", command) != 1) return 2;
        if (strcmp(command, "RESET") == 0) { reset(); valid = true; }
        else if(strcmp(command,"TXLIMIT")==0) {
            uint32_t v[1]; if(words(line,v,1) && v[0]>=1 && v[0]<=3) { tx_limit=v[0]; valid=true; }
        } else if(strcmp(command,"STARTCFG")==0) {
            uint32_t v[4]; valid=words(line,v,4) && start_configure(v);
        } else if(strcmp(command,"STARTALL")==0 && start_enabled && words(line,NULL,0)) {
            for(unsigned i=0;i<start_scheduler.config.channel_count;++i)
                (void)PDM_Start_Request(&start_scheduler,(uint8_t)i,++request_ids[i],simulator_now,simulator_now);
            valid=true;
        } else if(strcmp(command,"STARTINVALID")==0 && start_enabled && words(line,NULL,0)) {
            start_inputs.valid=false; valid=true;
        } else if(strcmp(command,"STARTVALID")==0 && start_enabled && words(line,NULL,0)) {
            start_inputs.valid=true; valid=true;
        } else if(strcmp(command,"STARTRESET")==0 && start_enabled && words(line,NULL,0)) {
            PDM_Start_Reset(&start_scheduler,simulator_now); valid=true;
        } else if(start_inject(command,line)) {
            valid=true;
        } else if(strncmp(command,"START",5)==0 && start_enabled) {
            uint32_t v[1];
            if(words(line,v,1) && v[0]>=1 && v[0]<=start_scheduler.config.channel_count) {
                const unsigned i=v[0]-1; PDM_StartFeedback *f=&start_inputs.channels[i];
                valid=true;
                if(strcmp(command,"STARTREQ")==0) (void)PDM_Start_Request(&start_scheduler,(uint8_t)i,++request_ids[i],simulator_now,simulator_now);
                else if(strcmp(command,"STARTCOMPLETE")==0 && start_scheduler.channels[i].state==PDM_START_STARTING) {
                    f->start_complete=true; f->confirmed_off=false; f->measured_mA=start_scheduler.config.channels[i].continuous_mA;
                } else if(strcmp(command,"STARTFAULT")==0) f->healthy=false;
                else if(strcmp(command,"STARTOFF")==0) { f->healthy=true; f->confirmed_off=true; f->start_complete=false; f->measured_mA=0; }
                else if(strcmp(command,"STARTCANCEL")==0) PDM_Start_Cancel(&start_scheduler,(uint8_t)i,simulator_now);
                else if(strcmp(command,"STARTCLEAR")==0) (void)PDM_Start_ClearFault(&start_scheduler,(uint8_t)i,&start_inputs,simulator_now);
                else valid=false;
            }
        }
        else if (strcmp(command, "CAL") == 0) {
            uint32_t v[3];
            if (words(line, v, 3) && v[0] <= UINT16_MAX && v[1] <= UINT16_MAX && v[2] <= UINT16_MAX) {
                calibration = (PDM_ADCCalibration){(uint16_t)v[0], (uint16_t)v[1], (uint16_t)v[2]};
                PDM_ADC_Init(&adc, calibration); valid = true;
            }
        } else if (strcmp(command, "ADC") == 0) {
            uint32_t v[1 + PDM_ADC_RAW_COUNT];
            if (words(line, v, 1 + PDM_ADC_RAW_COUNT)) {
                uint16_t raw[PDM_ADC_RAW_COUNT]; valid = true;
                for (unsigned i = 0; i < PDM_ADC_RAW_COUNT; ++i) {
                    if (v[i + 1] > UINT16_MAX) { valid = false; break; }
                    raw[i] = (uint16_t)v[i + 1];
                }
                if (valid) PDM_ADC_SubmitRaw(&adc, raw, v[0]);
            }
        } else if (strcmp(command, "ADCFAIL") == 0) {
            uint32_t v[1];
            if (words(line, v, 1) && (v[0] == 1 || v[0] == 3 || v[0] == 4)) {
                PDM_ADC_Invalidate(&adc, (uint8_t)v[0]); valid = true;
            }
        }
        else if (strcmp(command, "TICK") == 0) {
            uint32_t now;
            if (sscanf(line, "%*s %" SCNu32, &now) == 1) { simulator_now=now; start_tick(); PDM_Link_Tick(&link, now); valid = true; }
        } else if (strcmp(command, "SET") == 0) {
            unsigned channel; int32_t value;
            if (sscanf(line, "%*s %u %" SCNd32, &channel, &value) == 2 &&
                (channel == 240 || channel == 241 || channel == 242 || channel == 244)) {
                Channel_SetValue(&channels, (uint16_t)channel, value); valid = true;
            }
        } else if (strcmp(command, "RX") == 0) {
            uint32_t now; unsigned id, dlc = 8, extended = 0, remote = 0; char hex[18];
            int fields = sscanf(line, "%*s %" SCNu32 " %x %17s %u %u %u", &now, &id, hex, &dlc, &extended, &remote);
            if ((fields == 3 || fields == 6) && strlen(hex) == 16 && dlc <= 255) {
                PDM_LinkFrame frame = {.id = id, .dlc = (uint8_t)dlc, .extended = extended != 0, .remote = remote != 0};
                valid = true;
                for (unsigned i = 0; i < 8; ++i) {
                    int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
                    if (hi < 0 || lo < 0) { valid = false; break; }
                    frame.data[i] = (uint8_t)(hi * 16 + lo);
                }
                if (valid) {
                    if (id <= 0x7ff && dlc <= 8 && !frame.extended && !frame.remote) {
                        Channel_SetValue(&channels, 240, (int32_t)(++received & 0x7fffffffu));
                        Channel_SetValue(&channels, 241, (int32_t)id);
                    }
                    PDM_Link_Receive(&link, &frame, now);
                }
            }
        }
        if (!valid) puts("ERROR");
        const PDM_LinkFrame *frame;
        unsigned sent=0;
        while (sent<tx_limit && (frame = PDM_Link_PeekTx(&link)) != NULL) {
            printf("%03" PRIx32 " ", frame->id);
            for (unsigned i = 0; i < 8; ++i) printf("%02x", frame->data[i]);
            putchar('\n'); PDM_Link_PopTx(&link); ++sent;
        }
        puts("END"); fflush(stdout);
    }
    return 0;
}
