#include "core/loop/daw_mainthread_messages.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

#include "core/loop/daw_mainthread_wake.h"
#include "core_queue.h"
#include "core_time.h"

// Wraps one DAW message entry for queue storage.
typedef struct DawMainThreadMessageNode {
    DawMainThreadMessage msg;
    uint64_t enqueue_ns;
} DawMainThreadMessageNode;

enum { DAW_MAINTHREAD_MESSAGE_QUEUE_CAPACITY = 1024 };

static CoreQueueMutex g_queue;
static void* g_queue_backing[DAW_MAINTHREAD_MESSAGE_QUEUE_CAPACITY];
static bool g_queue_initialized = false;
static SDL_mutex* g_stats_mutex = NULL;

static uint64_t g_next_seq = 1;
static uint32_t g_high_watermark = 0;
static uint64_t g_pushed = 0;
static uint64_t g_popped = 0;
static uint64_t g_coalesced_drops = 0;
static uint64_t g_wake_pushes = 0;
static uint64_t g_wake_coalesced_skips = 0;
static uint64_t g_wake_failures = 0;
static uint64_t g_drain_latency_samples = 0;
static uint64_t g_drain_latency_total_ns = 0;
static uint64_t g_drain_latency_max_ns = 0;
static uint32_t g_pending_coalesced_mask = 0;
static bool g_wake_armed = false;

// Returns a bit flag for coalesced message types, zero for non-coalesced types.
static uint32_t coalesced_message_mask(DawMainThreadMessageType type) {
    switch (type) {
        case DAW_MAINTHREAD_MSG_ENGINE_FX_METER: return 1u << 0;
        case DAW_MAINTHREAD_MSG_ENGINE_FX_SCOPE: return 1u << 1;
        case DAW_MAINTHREAD_MSG_ENGINE_TRANSPORT: return 1u << 2;
        default: return 0u;
    }
}

// Locks DAW queue stats mutex when present.
static void stats_lock(void) {
    if (g_stats_mutex) SDL_LockMutex(g_stats_mutex);
}

// Unlocks DAW queue stats mutex when present.
static void stats_unlock(void) {
    if (g_stats_mutex) SDL_UnlockMutex(g_stats_mutex);
}

void daw_mainthread_message_queue_init(void) {
    if (!g_stats_mutex) g_stats_mutex = SDL_CreateMutex();

    if (!g_queue_initialized) {
        g_queue_initialized = core_queue_mutex_init_ex(&g_queue,
                                                       g_queue_backing,
                                                       DAW_MAINTHREAD_MESSAGE_QUEUE_CAPACITY,
                                                       CORE_QUEUE_OVERFLOW_REJECT);
    }
    daw_mainthread_message_queue_reset();
}

void daw_mainthread_message_queue_shutdown(void) {
    daw_mainthread_message_queue_reset();
    if (g_queue_initialized) {
        core_queue_mutex_destroy(&g_queue);
        g_queue_initialized = false;
    }
    if (g_stats_mutex) {
        SDL_DestroyMutex(g_stats_mutex);
        g_stats_mutex = NULL;
    }
}

void daw_mainthread_message_queue_reset(void) {
    if (g_queue_initialized) {
        void* item = NULL;
        while (core_queue_mutex_pop(&g_queue, &item)) {
            DawMainThreadMessageNode* node = (DawMainThreadMessageNode*)item;
            free(node);
        }
    }

    stats_lock();
    g_next_seq = 1;
    g_high_watermark = 0;
    g_pushed = 0;
    g_popped = 0;
    g_coalesced_drops = 0;
    g_wake_pushes = 0;
    g_wake_coalesced_skips = 0;
    g_wake_failures = 0;
    g_drain_latency_samples = 0;
    g_drain_latency_total_ns = 0;
    g_drain_latency_max_ns = 0;
    g_pending_coalesced_mask = 0;
    g_wake_armed = false;
    stats_unlock();
}

bool daw_mainthread_message_queue_push(const DawMainThreadMessage* msg) {
    if (!msg || msg->type == DAW_MAINTHREAD_MSG_NONE || !g_queue_initialized) return false;

    DawMainThreadMessageNode* node = (DawMainThreadMessageNode*)calloc(1, sizeof(*node));
    if (!node) return false;
    node->msg = *msg;
    node->enqueue_ns = core_time_now_ns();

    stats_lock();
    node->msg.seq = g_next_seq++;
    const uint32_t coalesced_mask = coalesced_message_mask(node->msg.type);
    if ((coalesced_mask != 0u) && (g_pending_coalesced_mask & coalesced_mask) != 0u) {
        g_coalesced_drops++;
        stats_unlock();
        free(node);
        return true;
    }
    stats_unlock();

    if (!core_queue_mutex_push(&g_queue, node)) {
        free(node);
        return false;
    }

    stats_lock();
    g_pushed++;
    uint32_t depth = (uint32_t)core_queue_mutex_size(&g_queue);
    if (depth > g_high_watermark) g_high_watermark = depth;
    if (coalesced_mask != 0u) {
        g_pending_coalesced_mask |= coalesced_mask;
    }

    if (!g_wake_armed) {
        if (daw_mainthread_wake_push()) {
            g_wake_pushes++;
            g_wake_armed = true;
        } else {
            g_wake_failures++;
        }
    } else {
        g_wake_coalesced_skips++;
    }
    stats_unlock();
    return true;
}

bool daw_mainthread_message_post(DawMainThreadMessageType type, uint64_t user_u64, void* user_ptr) {
    DawMainThreadMessage msg = {
        .type = type,
        .seq = 0,
        .user_u64 = user_u64,
        .user_ptr = user_ptr,
    };
    return daw_mainthread_message_queue_push(&msg);
}

bool daw_mainthread_message_queue_pop(DawMainThreadMessage* out) {
    if (!out || !g_queue_initialized) return false;
    memset(out, 0, sizeof(*out));

    void* item = NULL;
    if (!core_queue_mutex_pop(&g_queue, &item)) return false;

    DawMainThreadMessageNode* node = (DawMainThreadMessageNode*)item;
    if (!node) return false;

    *out = node->msg;

    uint64_t now_ns = core_time_now_ns();
    uint64_t latency_ns = core_time_diff_ns(now_ns, node->enqueue_ns);
    free(node);

    stats_lock();
    g_popped++;
    g_drain_latency_samples++;
    g_drain_latency_total_ns = core_time_add_ns(g_drain_latency_total_ns, latency_ns);
    if (latency_ns > g_drain_latency_max_ns) {
        g_drain_latency_max_ns = latency_ns;
    }
    uint32_t coalesced_mask = coalesced_message_mask(out->type);
    if (coalesced_mask != 0u) {
        g_pending_coalesced_mask &= ~coalesced_mask;
    }
    if (g_queue_initialized && core_queue_mutex_size(&g_queue) == 0) {
        g_wake_armed = false;
    }
    stats_unlock();
    return true;
}

int daw_mainthread_message_queue_drain(DawMainThreadMessage* out, int max_count) {
    if (!out || max_count <= 0) return 0;

    int n = 0;
    while (n < max_count && daw_mainthread_message_queue_pop(&out[n])) {
        n++;
    }
    return n;
}

bool daw_mainthread_message_queue_has_pending(void) {
    if (!g_queue_initialized) return false;
    return core_queue_mutex_size(&g_queue) > 0;
}

void daw_mainthread_message_queue_snapshot(DawMainThreadMessageQueueStats* out) {
    if (!out) return;
    stats_lock();
    out->depth = g_queue_initialized ? (uint32_t)core_queue_mutex_size(&g_queue) : 0u;
    out->high_watermark = g_high_watermark;
    out->pushed = g_pushed;
    out->popped = g_popped;
    out->coalesced_drops = g_coalesced_drops;
    out->wake_pushes = g_wake_pushes;
    out->wake_coalesced_skips = g_wake_coalesced_skips;
    out->wake_failures = g_wake_failures;
    out->drain_latency_samples = g_drain_latency_samples;
    out->drain_latency_total_ns = g_drain_latency_total_ns;
    out->drain_latency_max_ns = g_drain_latency_max_ns;
    stats_unlock();
}
