/**
 * @file can_tx.h
 * @brief CAN transmit system
 *
 * Modes:
 * - Periodic: Send at fixed interval
 * - OnChange: Send when source channel changes
 * - OnCondition: Send when condition channel becomes true
 * - Gateway: Forward received CAN message with modifications
 */

#ifndef CAN_TX_H
#define CAN_TX_H

#include <stdint.h>
#include <stdbool.h>
#include "../channel_engine/channel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CAN TX Types
 * ============================================================================ */

typedef enum {
    CAN_TX_DISABLED     = 0,
    CAN_TX_PERIODIC     = 1,    /* Send at fixed interval */
    CAN_TX_ON_CHANGE    = 2,    /* Send when value changes */
    CAN_TX_ON_CONDITION = 3,    /* Send when condition true */
    CAN_TX_GATEWAY      = 4,    /* Forward/modify received message */
} CanTxMode_t;

typedef enum {
    CAN_SIGNAL_UNSIGNED = 0,
    CAN_SIGNAL_SIGNED   = 1,
    CAN_SIGNAL_FLOAT    = 2,
} CanSignalType_t;

typedef enum {
    CAN_BYTE_ORDER_LE = 0,      /* Little endian (Intel) */
    CAN_BYTE_ORDER_BE = 1,      /* Big endian (Motorola) */
} CanByteOrder_t;

/* ============================================================================
 * CAN TX Signal (packed into message)
 * ============================================================================ */

typedef struct {
    uint16_t source_channel;    /* Channel to read value from */
    uint8_t  start_bit;         /* Start bit in message (0-63) */
    uint8_t  bit_length;        /* Signal bit length (1-32) */
    uint8_t  signal_type;       /* CanSignalType_t */
    uint8_t  byte_order;        /* CanByteOrder_t */
    int16_t  scale_num;         /* Scale = num/den */
    int16_t  scale_den;
    int16_t  offset;            /* Applied after scaling */
} CanTxSignal_t;

#define CAN_TX_MAX_SIGNALS 8

/* ============================================================================
 * CAN TX Message Configuration
 * ============================================================================ */

typedef struct {
    uint16_t id;                /* Config ID */
    uint8_t  mode;              /* CanTxMode_t */
    uint8_t  flags;

    uint32_t can_id;            /* CAN message ID */
    uint8_t  bus;               /* CAN bus (0 or 1) */
    uint8_t  dlc;               /* Data length (1-8) */
    uint8_t  is_extended;       /* Extended ID (29-bit) */
    uint8_t  signal_count;

    /* Timing */
    uint16_t interval_ms;       /* Periodic interval */
    uint16_t min_interval_ms;   /* Minimum time between sends */

    /* Triggers */
    uint16_t condition_channel; /* OnCondition: trigger channel */
    uint16_t gateway_rx_id;     /* Gateway: source message ID */

    /* Signals */
    CanTxSignal_t signals[CAN_TX_MAX_SIGNALS];

    char name[24];
} CanTxConfig_t;

/* Flags */
#define CAN_TX_FLAG_ENABLED     (1 << 0)
#define CAN_TX_FLAG_PENDING     (1 << 1)    /* Message pending send */

/* ============================================================================
 * CAN TX Runtime State
 * ============================================================================ */

typedef struct {
    uint32_t last_send_tick;    /* Last send timestamp */
    uint32_t send_count;        /* Total sends */
    int32_t  prev_values[CAN_TX_MAX_SIGNALS]; /* For change detection */
    uint8_t  data[8];           /* Built message data */
    bool     pending;           /* Ready to send */
} CanTxRuntime_t;

/* ============================================================================
 * CAN TX Manager
 * ============================================================================ */

#ifndef CAN_TX_MAX_MESSAGES
#define CAN_TX_MAX_MESSAGES 8 /* F446 resource profile */
#endif

typedef void (*CanTxSendFunc)(uint8_t bus, uint32_t id, bool extended,
                               const uint8_t* data, uint8_t len);

typedef struct {
    CanTxConfig_t  config[CAN_TX_MAX_MESSAGES];
    CanTxRuntime_t runtime[CAN_TX_MAX_MESSAGES];
    uint16_t       count;

    ChannelEngine_t* channels;
    CanTxSendFunc    send_func;     /* Platform-specific send */

    bool initialized;
} CanTxManager_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Initialize CAN TX manager
 */
void CanTx_Init(CanTxManager_t* mgr, ChannelEngine_t* channels,
                CanTxSendFunc send_func);

/**
 * @brief Reset all TX messages
 */
void CanTx_Reset(CanTxManager_t* mgr);

/**
 * @brief Add TX message configuration
 * @return Config index or -1 on error
 */
int CanTx_Add(CanTxManager_t* mgr, const CanTxConfig_t* config);

/**
 * @brief Remove TX message by ID
 */
void CanTx_Remove(CanTxManager_t* mgr, uint16_t id);

/**
 * @brief Update all TX messages (check triggers, build data)
 */
void CanTx_Update(CanTxManager_t* mgr, uint32_t tick_ms);

/**
 * @brief Send all pending messages
 */
void CanTx_SendPending(CanTxManager_t* mgr);

/**
 * @brief Force send specific message
 */
void CanTx_ForceSend(CanTxManager_t* mgr, uint16_t id);

/**
 * @brief Notify of received CAN message (for gateway mode)
 */
void CanTx_OnReceive(CanTxManager_t* mgr, uint8_t bus, uint32_t can_id,
                     const uint8_t* data, uint8_t len);

/* ============================================================================
 * Configuration Helpers
 * ============================================================================ */

/**
 * @brief Create periodic TX config
 */
CanTxConfig_t CanTx_CreatePeriodic(uint16_t id, uint32_t can_id, uint8_t bus,
                                    uint16_t interval_ms, const char* name);

/**
 * @brief Create on-change TX config
 */
CanTxConfig_t CanTx_CreateOnChange(uint16_t id, uint32_t can_id, uint8_t bus,
                                    uint16_t min_interval_ms, const char* name);

/**
 * @brief Add signal to TX config
 */
void CanTx_AddSignal(CanTxConfig_t* config, uint16_t source_ch,
                      uint8_t start_bit, uint8_t bit_length,
                      int16_t scale_num, int16_t scale_den, int16_t offset);

#ifdef __cplusplus
}
#endif

#endif /* CAN_TX_H */
