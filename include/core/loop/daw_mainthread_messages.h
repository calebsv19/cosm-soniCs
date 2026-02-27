#pragma once

#include <stdbool.h>
#include <stdint.h>

// Defines DAW main-thread message kinds for queue-driven loop integration.
typedef enum DawMainThreadMessageType {
    DAW_MAINTHREAD_MSG_NONE = 0,
    DAW_MAINTHREAD_MSG_USER = 1,
    DAW_MAINTHREAD_MSG_ENGINE_FX_METER = 2,
    DAW_MAINTHREAD_MSG_ENGINE_FX_SCOPE = 3,
    DAW_MAINTHREAD_MSG_ENGINE_TRANSPORT = 4,
    DAW_MAINTHREAD_MSG_LIBRARY_SCAN_COMPLETE = 5
} DawMainThreadMessageType;

// Stores one queued DAW main-thread message payload.
typedef struct DawMainThreadMessage {
    DawMainThreadMessageType type;
    uint64_t seq;
    uint64_t user_u64;
    void* user_ptr;
} DawMainThreadMessage;

// Captures DAW message queue diagnostics and high-water marks.
typedef struct DawMainThreadMessageQueueStats {
    uint32_t depth;
    uint32_t high_watermark;
    uint64_t pushed;
    uint64_t popped;
    uint64_t coalesced_drops;
    uint64_t wake_pushes;
    uint64_t wake_coalesced_skips;
    uint64_t wake_failures;
    uint64_t drain_latency_samples;
    uint64_t drain_latency_total_ns;
    uint64_t drain_latency_max_ns;
} DawMainThreadMessageQueueStats;

// Initializes the DAW main-thread message queue.
void daw_mainthread_message_queue_init(void);
// Shuts down the DAW main-thread message queue.
void daw_mainthread_message_queue_shutdown(void);
// Resets the DAW main-thread message queue and stats counters.
void daw_mainthread_message_queue_reset(void);
// Pushes one message to the DAW main-thread queue.
bool daw_mainthread_message_queue_push(const DawMainThreadMessage* msg);
// Posts one typed message and triggers coalesced wake signaling for producer paths.
bool daw_mainthread_message_post(DawMainThreadMessageType type, uint64_t user_u64, void* user_ptr);
// Pops one message from the DAW main-thread queue.
bool daw_mainthread_message_queue_pop(DawMainThreadMessage* out);
// Drains up to max_count messages and returns pop count.
int daw_mainthread_message_queue_drain(DawMainThreadMessage* out, int max_count);
// Returns true when the DAW main-thread queue has pending messages.
bool daw_mainthread_message_queue_has_pending(void);
// Copies DAW main-thread message queue diagnostics.
void daw_mainthread_message_queue_snapshot(DawMainThreadMessageQueueStats* out);
