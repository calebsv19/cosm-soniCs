#include "core/loop/daw_mainthread_wake.h"

#include <string.h>

static Uint32 g_wake_event_type = 0;
static bool g_wake_initialized = false;
static SDL_atomic_t g_wake_pushes;
static SDL_atomic_t g_wake_received;
static SDL_atomic_t g_wake_push_failures;
static CoreWake g_wake_bridge;

// Signals the DAW wake event through SDL for external core_wake integration.
static bool external_signal(void* ctx) {
    (void)ctx;
    if (!g_wake_initialized || g_wake_event_type == 0) {
        SDL_AtomicAdd(&g_wake_push_failures, 1);
        return false;
    }

    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = g_wake_event_type;
    ev.user.type = g_wake_event_type;

    if (SDL_PushEvent(&ev) < 0) {
        SDL_AtomicAdd(&g_wake_push_failures, 1);
        return false;
    }

    SDL_AtomicAdd(&g_wake_pushes, 1);
    return true;
}

// Waits for SDL events and maps the result into core_wake wait semantics.
static CoreWakeWaitResult external_wait(void* ctx, uint32_t timeout_ms) {
    (void)ctx;
    SDL_Event ignored;
    if (SDL_WaitEventTimeout(&ignored, (int)timeout_ms)) {
        return CORE_WAKE_WAIT_SIGNALED;
    }
    return CORE_WAKE_WAIT_TIMEOUT;
}

bool daw_mainthread_wake_init(void) {
    if (g_wake_initialized) return true;

    SDL_AtomicSet(&g_wake_pushes, 0);
    SDL_AtomicSet(&g_wake_received, 0);
    SDL_AtomicSet(&g_wake_push_failures, 0);

    Uint32 event_type = SDL_RegisterEvents(1);
    if (event_type == (Uint32)-1) {
        g_wake_event_type = 0;
        g_wake_initialized = false;
        return false;
    }

    if (!core_wake_init_external(&g_wake_bridge, external_signal, external_wait, NULL)) {
        g_wake_event_type = 0;
        g_wake_initialized = false;
        return false;
    }

    g_wake_event_type = event_type;
    g_wake_initialized = true;
    return true;
}

void daw_mainthread_wake_shutdown(void) {
    core_wake_shutdown(&g_wake_bridge);
    g_wake_event_type = 0;
    g_wake_initialized = false;
}

bool daw_mainthread_wake_push(void) {
    if (!g_wake_initialized) return false;
    return core_wake_signal(&g_wake_bridge);
}

bool daw_mainthread_wake_is_event(const SDL_Event* event) {
    if (!event || !g_wake_initialized || g_wake_event_type == 0) return false;
    return event->type == g_wake_event_type;
}

void daw_mainthread_wake_note_received(void) {
    SDL_AtomicAdd(&g_wake_received, 1);
}

SDL_bool daw_mainthread_wake_wait_for_event(uint32_t timeout_ms, SDL_Event* out_event) {
    if (!g_wake_initialized || !out_event) return SDL_FALSE;
    memset(out_event, 0, sizeof(*out_event));
    return SDL_WaitEventTimeout(out_event, (int)timeout_ms) ? SDL_TRUE : SDL_FALSE;
}

void daw_mainthread_wake_snapshot(DawMainThreadWakeStats* out) {
    if (!out) return;
    out->event_type = g_wake_event_type;
    out->pushes = (uint32_t)SDL_AtomicGet(&g_wake_pushes);
    out->received = (uint32_t)SDL_AtomicGet(&g_wake_received);
    out->push_failures = (uint32_t)SDL_AtomicGet(&g_wake_push_failures);
}

CoreWake* daw_mainthread_wake_get_core_wake(void) {
    if (!g_wake_initialized) return NULL;
    return &g_wake_bridge;
}
