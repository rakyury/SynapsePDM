/*
 * Host tests for the actual (trimmed) Racing Dash C channel engine and VM.
 * No Arduino headers, STM32 libraries or proprietary hardware required.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/r2_shared/channel_engine/channel.h"
#include "../src/r2_shared/vm/vm.h"
#include "../src/r2_shared/channel_engine/stateful_nodes.h"
#include "../src/r2_shared/filters/filter.h"
#include "../src/r2_shared/timers/timer.h"
#include "../src/r2_shared/tables/table.h"
#include "../src/r2_shared/can_tx/can_tx.h"

static ChannelEngine_t engine;
static VM_t vm;
static StatefulManager_t stateful;
static FilterManager_t filters;
static TimerManager_t timers;
static TableManager_t tables;
static CanTxManager_t can_tx;

static void test_memory_budget(void)
{
    /* Guard accidental reintroduction of H747-sized allocations in F446. */
    const size_t full_size = sizeof(ChannelEngine_t) + sizeof(VM_t) +
        sizeof(StatefulManager_t) + sizeof(FilterManager_t) +
        sizeof(TimerManager_t) + sizeof(TableManager_t) +
        sizeof(CanTxManager_t);
    assert(full_size < 45000u);
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
    Stateful_Init(&stateful, &engine);
    Filter_Init(&filters, &engine);
    Timer_Init(&timers, &engine);
    Table_Init(&tables, &engine);
    CanTx_Init(&can_tx, &engine, NULL);
    Filter_UpdateAll(&filters);
    Timer_UpdateAll(&timers, 100u);
    Table_UpdateAll(&tables);
    Stateful_UpdateAll(&stateful, 10u);
    CanTx_Update(&can_tx, 100u);
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
           (unsigned)(sizeof(ChannelEngine_t) + sizeof(VM_t) +
                      sizeof(StatefulManager_t) + sizeof(FilterManager_t) +
                      sizeof(TimerManager_t) + sizeof(TableManager_t) +
                      sizeof(CanTxManager_t)));
    return 0;
}
