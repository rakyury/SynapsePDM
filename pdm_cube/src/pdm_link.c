#include "pdm_link.h"
#include <stddef.h>
#include <string.h>

enum { LINK_OK, LINK_MALFORMED, LINK_BUSY, LINK_OFFSET, LINK_CRC,
       LINK_BYTECODE, LINK_EXPIRED, LINK_NO_ACTIVE };
static const uint16_t sample_ids[PDM_LINK_SAMPLE_COUNT] = {
    200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213,
    230, 240, 241, 242, 243, 244, 300
};

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8u * i));
}
static bool readable(uint16_t id)
{
    return (id >= 200 && id <= 213) || id == 230 || (id >= 240 && id <= 244);
}

uint32_t PDM_Link_CRC32(const uint8_t *bytes, uint16_t length)
{
    uint32_t crc = 0xffffffffu;
    if (bytes == NULL && length != 0) return 0;
    for (uint16_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}

bool PDM_Link_ValidateBytecode(const uint8_t *code, uint16_t length)
{
    uint16_t pc = 0;
    unsigned depth = 0;
    if (code == NULL || length == 0 || length > PDM_LINK_MAX_CODE) return false;
    while (pc < length) {
        uint8_t op = code[pc++];
        if (op == 0xff) return pc == length && depth == 1;
        if (op == 0x01) {
            if ((unsigned)pc + 4u > length) return false;
            pc += 4; ++depth;
        } else if (op == 0x02) {
            if ((unsigned)pc + 2u > length || !readable(get16(&code[pc]))) return false;
            pc += 2; ++depth;
        } else if (op == 0x42) {
            if (depth < 1) return false;
        } else if (op == 0x10 || op == 0x11 || (op >= 0x30 && op <= 0x35) ||
                   op == 0x40 || op == 0x41 || op == 0x50 || op == 0x51) {
            if (depth < 2) return false;
            --depth;
        } else return false;
        if (depth > 48) return false;
    }
    return false;
}

static void clear_staging(PDM_Link *link)
{
    link->staging_valid = false;
    link->tid = 0; link->length = 0; link->next_offset = 0;
    memset(link->staging, 0, sizeof(link->staging));
}
static void expire(PDM_Link *link, uint32_t now)
{
    if (link->staging_valid && (uint32_t)(now - link->last_activity_ms) >= 1000u)
        clear_staging(link);
}
static void reply(PDM_Link *link, uint32_t tid, uint8_t op, uint8_t status, uint16_t offset)
{
    PDM_LinkFrame *frame = &link->replies[(link->reply_head + link->reply_count) % PDM_LINK_REPLY_CAPACITY];
    *frame = (PDM_LinkFrame){.id = 0x582, .dlc = 8};
    put32(frame->data, tid); frame->data[4] = op; frame->data[5] = status;
    put16(&frame->data[6], offset); ++link->reply_count;
}
static void readback(PDM_Link *link, uint32_t tid)
{
    PDM_LinkFrame *frame = &link->replies[(link->reply_head + link->reply_count) % PDM_LINK_REPLY_CAPACITY];
    *frame = (PDM_LinkFrame){.id = 0x583, .dlc = 8};
    put32(frame->data, tid); put32(&frame->data[4], link->active_crc); ++link->reply_count;
}

void PDM_Link_Init(PDM_Link *link, ChannelEngine_t *channels, VM_t *vm)
{
    if (link == NULL) return;
    memset(link, 0, sizeof(*link));
    link->channels = channels; link->vm = vm;
    if (channels == NULL || vm == NULL) return;
    VM_Init(vm, channels);
    for (unsigned i = 0; i < PDM_LINK_SAMPLE_COUNT; ++i)
        (void)Channel_Register(channels, sample_ids[i], CH_TYPE_NUMBER, CH_CLASS_SYSTEM, "pdm_diagnostic");
    for (uint16_t id = 200; id <= 213; ++id) Channel_SetValue(channels, id, 0);
    Channel_SetValue(channels, 230, 1);
    Channel_SetValue(channels, 300, 0);
}

void PDM_Link_Receive(PDM_Link *link, const PDM_LinkFrame *frame, uint32_t now)
{
    if (link == NULL || link->channels == NULL || link->vm == NULL || frame == NULL ||
        frame->extended || frame->remote || frame->dlc != 8 || frame->id < 0x590 || frame->id > 0x594)
        return;
    expire(link, now);
    const uint8_t op = (uint8_t)(frame->id - 0x590);
    const uint32_t tid = get32(frame->data);
    const unsigned needed = (op == 2 || op == 4) ? 2u : 1u;
    /* Reserve all replies BEFORE any mutation. A flooded/full link drops the
       request, so timeout/query can reconcile without a half-acknowledged commit. */
    if ((unsigned)link->reply_count + needed > PDM_LINK_REPLY_CAPACITY) return;
    if (tid == 0 || (op == 0 && (frame->data[6] != PDM_LINK_VERSION || frame->data[7] != 0)) ||
        ((op == 3 || op == 4) && get32(&frame->data[4]) != 0)) {
        reply(link, tid, op, LINK_MALFORMED, link->next_offset); return;
    }
    if (op == 4) {
        reply(link, tid, op, link->active_valid ? LINK_OK : LINK_NO_ACTIVE, 0);
        if (link->active_valid) readback(link, tid);
        return;
    }
    if (op == 0) {
        const uint16_t length = get16(&frame->data[4]);
        if (length == 0 || length > PDM_LINK_MAX_CODE) {
            reply(link, tid, op, LINK_MALFORMED, link->next_offset); return;
        }
        if (link->staging_valid) {
            const uint8_t status = tid != link->tid ? LINK_BUSY :
                length != link->length ? LINK_MALFORMED : LINK_OK;
            reply(link, tid, op, status, link->next_offset);
            if (status == LINK_OK) link->last_activity_ms = now;
            return;
        }
        clear_staging(link);
        link->staging_valid = true; link->tid = tid; link->length = length;
        link->last_activity_ms = now;
        reply(link, tid, op, LINK_OK, 0); return;
    }
    if (!link->staging_valid) { reply(link, tid, op, LINK_EXPIRED, 0); return; }
    if (tid != link->tid) { reply(link, tid, op, LINK_BUSY, link->next_offset); return; }
    if (op == 3) {
        clear_staging(link); reply(link, tid, op, LINK_OK, 0); return;
    }
    if (op == 1) {
        const uint16_t offset = get16(&frame->data[4]);
        if (offset >= link->length || offset % 2u != 0 || offset > link->next_offset) {
            reply(link, tid, op, LINK_OFFSET, link->next_offset); return;
        }
        const uint16_t count = (uint16_t)(link->length - offset == 1 ? 1 : 2);
        if (count == 1 && frame->data[7] != 0) {
            reply(link, tid, op, LINK_MALFORMED, link->next_offset); return;
        }
        if (offset < link->next_offset) {
            if (offset + count > link->next_offset || memcmp(&link->staging[offset], &frame->data[6], count) != 0) {
                reply(link, tid, op, LINK_OFFSET, link->next_offset); return;
            }
        } else {
            memcpy(&link->staging[offset], &frame->data[6], count);
            link->next_offset = (uint16_t)(offset + count);
        }
        link->last_activity_ms = now;
        reply(link, tid, op, LINK_OK, link->next_offset); return;
    }
    if (link->next_offset != link->length) {
        reply(link, tid, op, LINK_OFFSET, link->next_offset); return;
    }
    const uint32_t crc = PDM_Link_CRC32(link->staging, link->length);
    if (crc != get32(&frame->data[4])) {
        reply(link, tid, op, LINK_CRC, link->next_offset); return;
    }
    if (!PDM_Link_ValidateBytecode(link->staging, link->length)) {
        reply(link, tid, op, LINK_BYTECODE, link->next_offset); return;
    }
    /* Called only from the foreground RX poll, between VM ticks. The active
       buffer is never visible partially, and the old one survives every reject. */
    memcpy(link->active, link->staging, link->length);
    link->active_length = link->length; link->active_crc = crc; link->active_valid = true;
    reply(link, tid, op, LINK_OK, link->length); readback(link, tid);
    clear_staging(link);
}

void PDM_Link_Tick(PDM_Link *link, uint32_t now)
{
    if (link == NULL || link->channels == NULL || link->vm == NULL) return;
    expire(link, now);
    for (uint16_t id = 200; id <= 213; ++id) Channel_SetValue(link->channels, id, 0);
    Channel_SetValue(link->channels, 230, 1);
    Channel_SetValue(link->channels, 243, (int32_t)(now & 0x7fffffffu));
    if (link->active_valid)
        Channel_SetValue(link->channels, 300, VM_Execute(link->vm, link->active, link->active_length));
    if (!link->heartbeat_started || (uint32_t)(now - link->last_heartbeat_ms) >= 100u) {
        link->heartbeat = (PDM_LinkFrame){.id = 0x580, .dlc = 8};
        link->heartbeat.data[0] = PDM_LINK_VERSION;
        link->heartbeat.data[1] = (uint8_t)(5u | (link->active_valid ? 2u : 0u));
        put16(&link->heartbeat.data[2], link->heartbeat_sequence++);
        put32(&link->heartbeat.data[4], now);
        link->heartbeat_pending = true; link->heartbeat_started = true;
        link->last_heartbeat_ms = now;
    }
    {
        const uint8_t index = link->sample_index;
        const uint16_t id = sample_ids[index];
        /* A congested HAL mailbox must not later present an old cached value
           as fresh. Refresh the pending channel every tick; only successful
           transmission advances the rotating index and per-channel sequence. */
        link->sample = (PDM_LinkFrame){.id = 0x581, .dlc = 8};
        put16(link->sample.data, id);
        link->sample.data[2] = (uint8_t)(id == 300 && !link->active_valid ? 1 : 0);
        link->sample.data[3] = link->sample_sequences[index];
        put32(&link->sample.data[4], (uint32_t)Channel_GetValue(link->channels, id));
        link->sample_pending = true;
    }
}

const PDM_LinkFrame *PDM_Link_PeekTx(const PDM_Link *link)
{
    if (link == NULL) return NULL;
    if (link->heartbeat_pending) return &link->heartbeat;
    if (link->sample_pending) return &link->sample;
    if (link->reply_count) return &link->replies[link->reply_head];
    return NULL;
}
void PDM_Link_PopTx(PDM_Link *link)
{
    if (link == NULL) return;
    if (link->heartbeat_pending) link->heartbeat_pending = false;
    else if (link->sample_pending) {
        link->sample_pending = false;
        ++link->sample_sequences[link->sample_index];
        link->sample_index = (uint8_t)((link->sample_index + 1u) % PDM_LINK_SAMPLE_COUNT);
    }
    else if (link->reply_count) {
        link->reply_head = (uint8_t)((link->reply_head + 1u) % PDM_LINK_REPLY_CAPACITY);
        --link->reply_count;
    }
}
