/**
 * @file can_tx.c
 * @brief CAN transmit implementation
 */

#include "can_tx.h"
#include <string.h>

/* ============================================================================
 * Initialization
 * ============================================================================ */

void CanTx_Init(CanTxManager_t* mgr, ChannelEngine_t* channels,
                CanTxSendFunc send_func)
{
    if (!mgr) return;

    memset(mgr, 0, sizeof(CanTxManager_t));
    mgr->channels = channels;
    mgr->send_func = send_func;
    mgr->initialized = true;
}

void CanTx_Reset(CanTxManager_t* mgr)
{
    if (!mgr) return;

    for (uint16_t i = 0; i < mgr->count; i++) {
        memset(&mgr->runtime[i], 0, sizeof(CanTxRuntime_t));
    }
}

/* ============================================================================
 * Configuration Management
 * ============================================================================ */

int CanTx_Add(CanTxManager_t* mgr, const CanTxConfig_t* config)
{
    if (!mgr || !config || mgr->count >= CAN_TX_MAX_MESSAGES) return -1;

    int idx = mgr->count++;
    memcpy(&mgr->config[idx], config, sizeof(CanTxConfig_t));
    memset(&mgr->runtime[idx], 0, sizeof(CanTxRuntime_t));

    return idx;
}

void CanTx_Remove(CanTxManager_t* mgr, uint16_t id)
{
    if (!mgr) return;

    for (uint16_t i = 0; i < mgr->count; i++) {
        if (mgr->config[i].id == id) {
            mgr->config[i].flags &= ~CAN_TX_FLAG_ENABLED;
            break;
        }
    }
}

/* ============================================================================
 * Signal Packing
 * ============================================================================ */

static void pack_signal_le(uint8_t* data, uint8_t start_bit, uint8_t bit_length,
                           uint32_t value)
{
    for (uint8_t i = 0; i < bit_length; i++) {
        uint8_t bit_pos = start_bit + i;
        uint8_t byte_idx = bit_pos / 8;
        uint8_t bit_idx = bit_pos % 8;

        if (byte_idx >= 8) break;

        if (value & (1UL << i)) {
            data[byte_idx] |= (1 << bit_idx);
        } else {
            data[byte_idx] &= ~(1 << bit_idx);
        }
    }
}

static void pack_signal_be(uint8_t* data, uint8_t start_bit, uint8_t bit_length,
                           uint32_t value)
{
    uint8_t byte_idx = start_bit / 8;
    uint8_t bit_in_byte = start_bit % 8;

    for (uint8_t i = 0; i < bit_length && byte_idx < 8; i++) {
        uint8_t src_bit = bit_length - 1 - i;

        if (value & (1UL << src_bit)) {
            data[byte_idx] |= (1 << (7 - bit_in_byte));
        } else {
            data[byte_idx] &= ~(1 << (7 - bit_in_byte));
        }

        bit_in_byte++;
        if (bit_in_byte >= 8) {
            bit_in_byte = 0;
            byte_idx++;
        }
    }
}

static void build_message(CanTxManager_t* mgr, uint16_t idx)
{
    CanTxConfig_t* cfg = &mgr->config[idx];
    CanTxRuntime_t* rt = &mgr->runtime[idx];

    memset(rt->data, 0, 8);

    for (uint8_t i = 0; i < cfg->signal_count && i < CAN_TX_MAX_SIGNALS; i++) {
        CanTxSignal_t* sig = &cfg->signals[i];

        int32_t raw = Channel_GetValue(mgr->channels, sig->source_channel);

        int64_t scaled;
        if (sig->scale_den != 0) {
            scaled = ((int64_t)raw * sig->scale_den) / sig->scale_num;
        } else {
            scaled = raw;
        }
        scaled -= sig->offset;

        uint32_t packed;
        if (sig->signal_type == CAN_SIGNAL_SIGNED && scaled < 0) {
            packed = (uint32_t)scaled;
            if (sig->bit_length < 32) {
                packed &= (1UL << sig->bit_length) - 1;
            }
        } else {
            packed = (uint32_t)scaled;
            if (sig->bit_length < 32) {
                packed &= (1UL << sig->bit_length) - 1;
            }
        }

        if (sig->byte_order == CAN_BYTE_ORDER_BE) {
            pack_signal_be(rt->data, sig->start_bit, sig->bit_length, packed);
        } else {
            pack_signal_le(rt->data, sig->start_bit, sig->bit_length, packed);
        }
    }
}

/* ============================================================================
 * Update Logic
 * ============================================================================ */

static bool check_change(CanTxManager_t* mgr, uint16_t idx)
{
    CanTxConfig_t* cfg = &mgr->config[idx];
    CanTxRuntime_t* rt = &mgr->runtime[idx];

    bool changed = false;

    for (uint8_t i = 0; i < cfg->signal_count && i < CAN_TX_MAX_SIGNALS; i++) {
        int32_t val = Channel_GetValue(mgr->channels, cfg->signals[i].source_channel);
        if (val != rt->prev_values[i]) {
            changed = true;
        }
        rt->prev_values[i] = val;
    }

    return changed;
}

void CanTx_Update(CanTxManager_t* mgr, uint32_t tick_ms)
{
    if (!mgr || !mgr->channels) return;

    for (uint16_t i = 0; i < mgr->count; i++) {
        CanTxConfig_t* cfg = &mgr->config[i];
        CanTxRuntime_t* rt = &mgr->runtime[i];

        if (!(cfg->flags & CAN_TX_FLAG_ENABLED)) continue;
        if (cfg->mode == CAN_TX_DISABLED) continue;

        bool should_send = false;
        uint32_t elapsed = tick_ms - rt->last_send_tick;

        switch ((CanTxMode_t)cfg->mode) {
            case CAN_TX_PERIODIC:
                if (elapsed >= cfg->interval_ms) {
                    should_send = true;
                }
                break;

            case CAN_TX_ON_CHANGE:
                if (check_change(mgr, i)) {
                    if (elapsed >= cfg->min_interval_ms) {
                        should_send = true;
                    }
                }
                break;

            case CAN_TX_ON_CONDITION:
                if (Channel_GetValue(mgr->channels, cfg->condition_channel)) {
                    if (elapsed >= cfg->min_interval_ms) {
                        should_send = true;
                    }
                }
                break;

            case CAN_TX_GATEWAY:
                break;

            default:
                break;
        }

        if (should_send) {
            build_message(mgr, i);
            rt->pending = true;
        }
    }
}

void CanTx_SendPending(CanTxManager_t* mgr)
{
    if (!mgr || !mgr->send_func) return;

    for (uint16_t i = 0; i < mgr->count; i++) {
        CanTxConfig_t* cfg = &mgr->config[i];
        CanTxRuntime_t* rt = &mgr->runtime[i];

        if (rt->pending) {
            mgr->send_func(cfg->bus, cfg->can_id, cfg->is_extended != 0,
                           rt->data, cfg->dlc);
            rt->pending = false;
            rt->send_count++;
            rt->last_send_tick = mgr->channels->current_tick;
        }
    }
}

void CanTx_ForceSend(CanTxManager_t* mgr, uint16_t id)
{
    if (!mgr) return;

    for (uint16_t i = 0; i < mgr->count; i++) {
        if (mgr->config[i].id == id) {
            build_message(mgr, i);
            mgr->runtime[i].pending = true;
            break;
        }
    }
}

void CanTx_OnReceive(CanTxManager_t* mgr, uint8_t bus, uint32_t can_id,
                     const uint8_t* data, uint8_t len)
{
    if (!mgr || !data) return;

    for (uint16_t i = 0; i < mgr->count; i++) {
        CanTxConfig_t* cfg = &mgr->config[i];
        CanTxRuntime_t* rt = &mgr->runtime[i];

        if (!(cfg->flags & CAN_TX_FLAG_ENABLED)) continue;
        if (cfg->mode != CAN_TX_GATEWAY) continue;
        if (cfg->gateway_rx_id != can_id) continue;
        if (cfg->bus != bus) continue;

        memcpy(rt->data, data, (len > 8) ? 8 : len);
        build_message(mgr, i);
        rt->pending = true;
    }
}

/* ============================================================================
 * Configuration Helpers
 * ============================================================================ */

CanTxConfig_t CanTx_CreatePeriodic(uint16_t id, uint32_t can_id, uint8_t bus,
                                    uint16_t interval_ms, const char* name)
{
    CanTxConfig_t cfg = {0};
    cfg.id = id;
    cfg.mode = CAN_TX_PERIODIC;
    cfg.flags = CAN_TX_FLAG_ENABLED;
    cfg.can_id = can_id;
    cfg.bus = bus;
    cfg.dlc = 8;
    cfg.interval_ms = interval_ms;
    if (name) strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    return cfg;
}

CanTxConfig_t CanTx_CreateOnChange(uint16_t id, uint32_t can_id, uint8_t bus,
                                    uint16_t min_interval_ms, const char* name)
{
    CanTxConfig_t cfg = {0};
    cfg.id = id;
    cfg.mode = CAN_TX_ON_CHANGE;
    cfg.flags = CAN_TX_FLAG_ENABLED;
    cfg.can_id = can_id;
    cfg.bus = bus;
    cfg.dlc = 8;
    cfg.min_interval_ms = min_interval_ms;
    if (name) strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    return cfg;
}

void CanTx_AddSignal(CanTxConfig_t* config, uint16_t source_ch,
                      uint8_t start_bit, uint8_t bit_length,
                      int16_t scale_num, int16_t scale_den, int16_t offset)
{
    if (!config || config->signal_count >= CAN_TX_MAX_SIGNALS) return;

    CanTxSignal_t* sig = &config->signals[config->signal_count++];
    sig->source_channel = source_ch;
    sig->start_bit = start_bit;
    sig->bit_length = bit_length;
    sig->signal_type = CAN_SIGNAL_UNSIGNED;
    sig->byte_order = CAN_BYTE_ORDER_LE;
    sig->scale_num = scale_num;
    sig->scale_den = scale_den;
    sig->offset = offset;
}
