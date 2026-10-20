/*
 * Host tests for the actual (trimmed) Racing Dash C channel engine and VM.
 * No Arduino headers, STM32 libraries or proprietary hardware required.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/r2_shared/channel_engine/channel.h"
#include "../src/r2_shared/vm/vm.h"

static ChannelEngine_t engine;
static VM_t vm;

static void test_memory_budget(void)
{
    /* Guard accidental reintroduction of H747-sized allocations in F446. */
    assert(sizeof(ChannelEngine_t) + sizeof(VM_t) <= 20000u);
    assert(MAX_CHANNELS == 96);
    assert(VM_MAX_PROGRAMS == 16);
    assert(VM_MAX_BYTECODE == 4096);
}

static void test_math_and_input_channels(void)
{
    static const uint8_t code[] = {
        OP_PUSH_CH, 100, 0,
        OP_PUSH_CONST, 10, 0, 0, 0,
        OP_ADD, OP_END
    };
    Channel_Init(&engine);
    assert(Channel_GetValue(&engine, CH_SYS_ONE) == 1);
    assert(Channel_Register(&engine, 100, CH_TYPE_NUMBER,
                            CH_CLASS_INPUT, "CAN_Sim") != NULL);
    Channel_BeginTick(&engine, 100u);
    Channel_SetValue(&engine, 100u, 45);
    Channel_EndTick(&engine);
    VM_Init(&vm, &engine);
    assert(VM_Execute(&vm, code, (uint16_t)sizeof(code)) == 55);
    assert(Channel_GetValue(&engine, 100) == 45);
}

static void test_capacity_is_bounded(void)
{
    uint16_t id = 1000;
    Channel_Init(&engine);
    while (engine.channel_count < MAX_CHANNELS)
    {
        assert(Channel_Register(&engine, id++, CH_TYPE_NUMBER,
                                CH_CLASS_USER, "v") != NULL);
    }
    assert(Channel_Register(&engine, 2000, CH_TYPE_NUMBER,
                            CH_CLASS_USER, "overflow") == NULL);
}

int main(void)
{
    test_memory_budget();
    test_math_and_input_channels();
    test_capacity_is_bounded();
    printf("R2 channel/VM host tests passed. Static RAM: %u bytes\n",
           (unsigned)(sizeof(ChannelEngine_t) + sizeof(VM_t)));
    return 0;
}
