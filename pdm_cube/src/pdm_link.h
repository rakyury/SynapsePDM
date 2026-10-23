#ifndef PDM_LINK_H
#define PDM_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include "r2_shared/channel_engine/channel.h"
#include "r2_shared/vm/vm.h"

#define PDM_LINK_VERSION 1u
#define PDM_LINK_MAX_CODE 128u
#define PDM_LINK_REPLY_CAPACITY 32u
#define PDM_LINK_SAMPLE_COUNT 21u
#define PDM_LINK_ADC_SAMPLE_COUNT 17u

/* Returns quality 0 calibrated, 1 unavailable, 2 nominal, 3 invalid, 4 stale.
 * No GPIO/ADC dependency in the wire layer; only the model supplies samples. */
typedef uint8_t (*PDM_LinkADCReader)(void *context, uint16_t channel,
                                   uint32_t now_ms, int32_t *value);

typedef struct {
    uint32_t id;
    uint8_t dlc;
    bool extended;
    bool remote;
    uint8_t data[8];
} PDM_LinkFrame;

typedef struct {
    ChannelEngine_t *channels;
    VM_t *vm;
    uint8_t staging[PDM_LINK_MAX_CODE];
    uint8_t active[PDM_LINK_MAX_CODE];
    uint16_t length, next_offset, active_length;
    uint32_t tid, last_activity_ms, active_crc;
    bool staging_valid, active_valid;
    PDM_LinkFrame replies[PDM_LINK_REPLY_CAPACITY];
    uint8_t reply_head, reply_count;
    PDM_LinkFrame heartbeat, sample;
    bool heartbeat_pending, sample_pending, heartbeat_started;
    uint32_t last_heartbeat_ms;
    uint16_t heartbeat_sequence;
    uint8_t sample_index, sample_sequences[PDM_LINK_SAMPLE_COUNT];
    PDM_LinkADCReader adc_reader;
    void *adc_context;
    bool sample_is_adc;
    uint8_t adc_sample_index, adc_sample_sequences[PDM_LINK_ADC_SAMPLE_COUNT];
    struct PDM_StartTelemetry *start_telemetry;
} PDM_Link;

/* Single-threaded API, called between ticks, never from CAN interrupt context.
 * It has no GPIO, HAL, EEPROM, flash or legacy configuration access. */
void PDM_Link_Init(PDM_Link *link, ChannelEngine_t *channels, VM_t *vm);
void PDM_Link_SetADCReader(PDM_Link *link, PDM_LinkADCReader reader, void *context);
void PDM_Link_SetStartTelemetry(PDM_Link *link, struct PDM_StartTelemetry *telemetry);
void PDM_Link_Receive(PDM_Link *link, const PDM_LinkFrame *frame, uint32_t now_ms);
void PDM_Link_Tick(PDM_Link *link, uint32_t now_ms);
const PDM_LinkFrame *PDM_Link_PeekTx(const PDM_Link *link);
void PDM_Link_PopTx(PDM_Link *link);
bool PDM_Link_ValidateBytecode(const uint8_t *code, uint16_t length);
uint32_t PDM_Link_CRC32(const uint8_t *bytes, uint16_t length);

#endif
