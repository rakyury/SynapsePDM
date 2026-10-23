#include "../src/pdm_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static PDM_Link link;
static ChannelEngine_t channels;
static VM_t vm;
static const uint8_t code[] = {0x02, 230, 0, 0x01, 41, 0, 0, 0, 0x10, 0xff};
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static PDM_LinkFrame frame(unsigned op, uint32_t tid)
{
    PDM_LinkFrame f = {.id = 0x590 + op, .dlc = 8}; put32(f.data, tid); return f;
}
static void init(void) { Channel_Init(&channels); PDM_Link_Init(&link, &channels, &vm); }
static void drain(void) { while (PDM_Link_PeekTx(&link)) PDM_Link_PopTx(&link); }
static void ack(unsigned op, unsigned status, unsigned offset)
{
    const PDM_LinkFrame *f = PDM_Link_PeekTx(&link);
    assert(f && f->id == 0x582 && f->dlc == 8 && !f->extended && !f->remote);
    assert(f->data[4] == op && f->data[5] == status);
    assert((unsigned)(f->data[6] | ((unsigned)f->data[7] << 8)) == offset);
    PDM_Link_PopTx(&link);
}
static void begin(uint32_t tid, uint16_t length, uint32_t now)
{
    PDM_LinkFrame f = frame(0, tid); put16(&f.data[4], length); f.data[6] = 1;
    PDM_Link_Receive(&link, &f, now);
}
static void data(uint32_t tid, uint16_t offset, uint8_t a, uint8_t b, uint32_t now)
{
    PDM_LinkFrame f = frame(1, tid); put16(&f.data[4], offset); f.data[6] = a; f.data[7] = b;
    PDM_Link_Receive(&link, &f, now);
}
static void stage(uint32_t tid, const uint8_t *bytes, uint16_t length, uint32_t now)
{
    begin(tid, length, now); ack(0, 0, 0);
    for (uint16_t i = 0; i < length; i += 2) {
        data(tid, i, bytes[i], i + 1 < length ? bytes[i + 1] : 0, now);
        ack(1, 0, i + 1 < length ? i + 2 : i + 1);
    }
}
static void commit(uint32_t tid, uint32_t crc, uint32_t now)
{
    PDM_LinkFrame f = frame(2, tid); put32(&f.data[4], crc); PDM_Link_Receive(&link, &f, now);
}
static void test_upload(void)
{
    init();
    PDM_LinkFrame query = frame(4, 33); PDM_Link_Receive(&link, &query, 0); ack(4, 7, 0);
    stage(1, code, sizeof(code), 0);
    const uint32_t crc = PDM_Link_CRC32(code, sizeof(code));
    commit(1, crc ^ 1, 0); ack(2, 4, sizeof(code)); assert(!link.active_valid);
    commit(1, crc, 0); ack(2, 0, sizeof(code));
    assert(PDM_Link_PeekTx(&link)->id == 0x583 && get32(&PDM_Link_PeekTx(&link)->data[4]) == crc);
    drain(); assert(link.active_valid && !link.staging_valid);
    PDM_Link_Tick(&link, 10); assert(Channel_GetValue(&channels, 300) == 42); drain();
    for (unsigned id = 200; id <= 213; ++id) assert(Channel_GetValue(&channels, (uint16_t)id) == 0);
    assert(Channel_GetValue(&channels, 230) == 1);
    /* Lost COMMIT reply is reconciled by a new correlated CRC query. */
    PDM_Link_Receive(&link, &query, 20); ack(4, 0, 0);
    assert(get32(&PDM_Link_PeekTx(&link)->data[4]) == crc); drain();
    static const uint8_t store[] = {0x01, 1, 0, 0, 0, 0x90, 200, 0, 0xff};
    stage(2, store, sizeof(store), 30); commit(2, PDM_Link_CRC32(store, sizeof(store)), 30);
    ack(2, 5, sizeof(store)); assert(link.active_crc == crc);
    PDM_Link_Tick(&link, 40); drain(); assert(Channel_GetValue(&channels, 300) == 42);
    PDM_LinkFrame abort = frame(3, 2); PDM_Link_Receive(&link, &abort, 40); ack(3, 0, 0);
    assert(link.active_valid && !link.staging_valid);
}
static void test_transactions(void)
{
    init(); begin(10, sizeof(code), 0); ack(0, 0, 0);
    data(10, 2, 0, 0, 1); ack(1, 3, 0);
    data(10, 0, code[0], code[1], 1); ack(1, 0, 2);
    begin(11, sizeof(code), 2); ack(0, 2, 2);
    begin(10, sizeof(code), 2); ack(0, 0, 2); assert(link.next_offset == 2);
    begin(10, sizeof(code) + 1, 2); ack(0, 1, 2);
    data(10, 0, code[0], code[1], 3); ack(1, 0, 2);
    data(10, 0, 0x90, code[1], 3); ack(1, 3, 2); assert(link.staging[0] == code[0]);
    data(11, 2, 0, 0, 4); ack(1, 2, 2);
    commit(10, 0, 5); ack(2, 3, 2);
    PDM_LinkFrame abort = frame(3, 11); PDM_Link_Receive(&link, &abort, 6); ack(3, 2, 2);
    abort = frame(3, 10); abort.data[4] = 1; PDM_Link_Receive(&link, &abort, 6); ack(3, 1, 2);
    assert(link.staging_valid);
    abort.data[4] = 0; PDM_Link_Receive(&link, &abort, 6); ack(3, 0, 0);
    /* Odd final byte requires zero padding. */
    begin(20, 7, 10); ack(0, 0, 0);
    data(20, 0, 1, 0, 10); ack(1, 0, 2);
    data(20, 2, 0, 0, 10); ack(1, 0, 4);
    data(20, 4, 0, 0x42, 10); ack(1, 0, 6);
    data(20, 6, 0xff, 1, 10); ack(1, 1, 6);
    data(20, 6, 0xff, 0, 10); ack(1, 0, 7);
    data(20, 6, 0xff, 0, 11); ack(1, 0, 7);
    const uint8_t odd[] = {1, 0, 0, 0, 0, 0x42, 0xff};
    commit(20, PDM_Link_CRC32(odd, sizeof(odd)), 11); ack(2, 0, 7); drain();
    PDM_Link_Tick(&link, 12); assert(Channel_GetValue(&channels, 300) == 1); drain();
    begin(30, sizeof(code), UINT32_MAX - 500); ack(0, 0, 0);
    PDM_Link_Tick(&link, 498); drain(); assert(link.staging_valid);
    PDM_Link_Tick(&link, 499); drain(); assert(!link.staging_valid && link.active_valid);
    commit(30, 0, 500); ack(2, 6, 0);
    init(); assert(!link.active_valid && !link.staging_valid);
}
static void test_shape_and_backpressure(void)
{
    init(); begin(1, sizeof(code), 0); ack(0, 0, 0);
    PDM_LinkFrame f = frame(3, 1);
    for (unsigned i = 0; i < 5; ++i) {
        PDM_LinkFrame bad = f;
        if (i == 0) bad.dlc = 7;
        if (i == 1) bad.dlc = 9;
        if (i == 2) bad.extended = true;
        if (i == 3) bad.remote = true;
        if (i == 4) bad.id = 0x595;
        PDM_Link_Receive(&link, &bad, 1); assert(link.staging_valid && link.reply_count == 0);
    }
    PDM_LinkFrame query = frame(4, 7);
    while (link.reply_count < PDM_LINK_REPLY_CAPACITY - 1) PDM_Link_Receive(&link, &query, 1);
    /* QUERY conservatively reserves two slots even with no active program. */
    assert(link.reply_count == 31);
    f = frame(3, 1); PDM_Link_Receive(&link, &f, 1); assert(!link.staging_valid && link.reply_count == 32);
    begin(2, sizeof(code), 1); assert(!link.staging_valid); drain();
    stage(3, code, sizeof(code), 2);
    while (link.reply_count < 31) PDM_Link_Receive(&link, &query, 2);
    commit(3, PDM_Link_CRC32(code, sizeof(code)), 2);
    assert(!link.active_valid && link.staging_valid); drain();
    commit(3, PDM_Link_CRC32(code, sizeof(code)), 2); ack(2, 0, sizeof(code)); drain();
    /* Pending samples refresh while mailboxes are blocked. Sequence/index
       cannot advance until PopTx acknowledges actual acceptance by HAL. */
    link.sample_index = 15; /* channel240 */
    Channel_SetValue(&channels, 240, 12); PDM_Link_Tick(&link, 10);
    assert(PDM_Link_PeekTx(&link)->id == 0x580); PDM_Link_PopTx(&link);
    assert(get32(&PDM_Link_PeekTx(&link)->data[4]) == 12);
    Channel_SetValue(&channels, 240, 99); PDM_Link_Tick(&link, 20);
    assert(get32(&PDM_Link_PeekTx(&link)->data[4]) == 99);
    assert(link.sample_index == 15 && link.sample_sequences[15] == 0);
    PDM_Link_PopTx(&link); assert(link.sample_index == 16 && link.sample_sequences[15] == 1);
}
static void test_telemetry(void)
{
    init(); unsigned samples = 0, heartbeats = 0, adc_samples = 0;
    bool seen[PDM_LINK_SAMPLE_COUNT] = {false};
    bool adc_seen[PDM_LINK_ADC_SAMPLE_COUNT] = {false};
    for (uint32_t now = 0; now <= 420; now += 10) {
        PDM_Link_Tick(&link, now);
        const PDM_LinkFrame *f;
        while ((f = PDM_Link_PeekTx(&link)) != NULL) {
            if (f->id == 0x580) { assert(f->data[0] == 1 && f->data[1] == 5); ++heartbeats; }
            else if (f->id == 0x584) {
                unsigned id = f->data[0] | ((unsigned)f->data[1] << 8);
                assert(f->data[2] == 1 && get32(&f->data[4]) == 0);
                assert(f->data[3] <= 1);
                assert((id >= 100 && id <= 102) || (id >= 110 && id <= 123));
                adc_seen[id <= 102 ? id - 100 : id - 110 + 3] = true;
                ++adc_samples;
            } else {
                assert(f->id == 0x581); unsigned id = f->data[0] | ((unsigned)f->data[1] << 8);
                assert(f->data[3] <= 2); ++samples;
                if (id >= 200 && id <= 213) { assert(get32(&f->data[4]) == 0); seen[id - 200] = true; }
                else if (id == 230) { assert(get32(&f->data[4]) == 1); seen[14] = true; }
                else if (id >= 240 && id <= 244) seen[15 + id - 240] = true;
                else { assert(id == 300 && f->data[2] == 1); seen[20] = true; }
            }
            PDM_Link_PopTx(&link);
        }
    }
    assert(samples == 22 && adc_samples == 21 && heartbeats == 5);
    for (unsigned i = 0; i < PDM_LINK_SAMPLE_COUNT; ++i) assert(seen[i]);
    for (unsigned i = 0; i < PDM_LINK_ADC_SAMPLE_COUNT; ++i) assert(adc_seen[i]);
}
static uint8_t adc_quality = 2;
static int32_t adc_value = 12345;
static uint32_t adc_now;
static uint8_t read_adc(void *context, uint16_t id, uint32_t now, int32_t *value)
{
    assert(context == &adc_value);
    assert(id == 100 || id == 101);
    adc_now = now; *value = adc_value; return adc_quality;
}
static void test_adc_freshness(void)
{
    init();
    PDM_Link_SetADCReader(&link, read_adc, &adc_value);
    PDM_Link_Tick(&link, 0); drain(); /* diagnostic slot first */
    assert(link.sample_is_adc);
    PDM_Link_Tick(&link, 10);
    const PDM_LinkFrame *f = PDM_Link_PeekTx(&link);
    assert(f->id == 0x584 && f->data[2] == 2 && get32(&f->data[4]) == 12345);
    /* Congestion: refresh pending numeric and quality before it is sent. */
    adc_value = 13000; PDM_Link_Tick(&link, 20);
    assert(get32(&PDM_Link_PeekTx(&link)->data[4]) == 13000 && adc_now == 20);
    adc_quality = 4; PDM_Link_Tick(&link, 30);
    f = PDM_Link_PeekTx(&link);
    assert(f->data[2] == 4 && get32(&f->data[4]) == 0);
    assert(link.adc_sample_index == 0 && link.adc_sample_sequences[0] == 0);
    PDM_Link_PopTx(&link);
    assert(!link.sample_is_adc && link.adc_sample_index == 1 && link.adc_sample_sequences[0] == 1);
    PDM_Link_Tick(&link, 40); drain(); /* diagnostic */
    adc_quality = 255; PDM_Link_Tick(&link, 50); f = PDM_Link_PeekTx(&link);
    assert(f->id == 0x584 && f->data[2] == 3 && get32(&f->data[4]) == 0);
    /* ADC IDs never become readable VM channels via registration side effects. */
    for (unsigned id = 100; id <= 123; ++id) {
        uint8_t source[] = {0x02, (uint8_t)id, 0, 0xff};
        assert(!PDM_Link_ValidateBytecode(source, sizeof(source)));
    }
}
static void test_validator(void)
{
    assert(PDM_Link_CRC32((const uint8_t *)"123456789", 9) == 0xcbf43926u);
    assert(PDM_Link_ValidateBytecode(code, sizeof(code)));
    const uint8_t invalid[][10] = {
        {0xff}, {0x01, 0xff}, {0x02, 44, 1, 0xff}, /* no channel300 feedback */
        {0x02, 100, 0, 0xff}, {0x01, 1, 0, 0, 0, 0x90, 200, 0, 0xff},
        {0x01, 1, 0, 0, 0, 0xff, 0}, {0x10, 0xff}, {0x00, 0xff}
    };
    const uint16_t lengths[] = {1, 2, 4, 4, 9, 7, 2, 2};
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
        assert(!PDM_Link_ValidateBytecode(invalid[i], lengths[i]));
    assert(!PDM_Link_ValidateBytecode(NULL, 1));
    uint8_t bytes[129] = {0}; assert(!PDM_Link_ValidateBytecode(bytes, sizeof(bytes)));
    /* Deterministic malformed fuzz, and bounded valid expressions executed by
       the actual VM while asserting the gate/interlock channel invariants. */
    uint32_t seed = 446;
    for (unsigned run = 0; run < 10000; ++run) {
        seed = seed * 1664525u + 1013904223u;
        uint16_t length = (uint16_t)(seed % 129u);
        for (unsigned i = 0; i < length; ++i) {
            seed = seed * 1664525u + 1013904223u; bytes[i] = (uint8_t)(seed >> 24);
        }
        if (PDM_Link_ValidateBytecode(bytes, length)) (void)VM_Execute(&vm, bytes, length);
        for (uint16_t id = 200; id <= 213; ++id) assert(Channel_GetValue(&channels, id) == 0);
        assert(Channel_GetValue(&channels, 230) == 1);
    }
    uint8_t overflow[] = {1, 0xff, 0xff, 0xff, 0x7f, 1, 1, 0, 0, 0, 0x10, 0xff};
    assert(PDM_Link_ValidateBytecode(overflow, sizeof(overflow)));
    assert(VM_Execute(&vm, overflow, sizeof(overflow)) == INT32_MIN);
}
int main(void)
{
    test_upload(); test_transactions(); test_shape_and_backpressure(); test_telemetry(); test_validator(); test_adc_freshness();
    puts("HAL diagnostic link: transactions, CRC, strict VM, timeout, telemetry, congestion and 10000 fuzz cases passed");
    return 0;
}
