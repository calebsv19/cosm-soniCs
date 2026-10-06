#include "engine/engine_internal.h"

#include "engine/graph.h"

// Serializes accepted transport intent across playback, seeks, and loop changes.
static bool engine_transport_submit(Engine* engine, EngineCommand* cmd) {
    uint64_t serial = atomic_load(&engine->transport_requested_serial);
    if (serial == UINT64_MAX >> 2) return false;
    cmd->transport_serial = serial + 1;
    if (!engine_post_command(engine, cmd)) return false;
    atomic_store(&engine->transport_requested_serial, serial + 1);
    return true;
}

// Submits serial-tagged playback intent without changing applied worker state.
static bool engine_transport_submit_playback(Engine* engine, EngineCommandType type) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id) return false;
    bool playing = type == ENGINE_CMD_PLAY;
    uint64_t serial = atomic_load(&engine->transport_requested_serial);
    if (serial == UINT64_MAX >> 2) return false;
    uint64_t token = ((serial + 1) << 1) | (playing ? 1u : 0u);
    EngineCommand cmd = {.type = type};
    cmd.payload.transport_token = token;
    if (!engine_transport_submit(engine, &cmd)) return false;
    atomic_store_explicit(&engine->playback_requested_token, token, memory_order_release);
    if (!engine->device_started || !engine->worker_thread) engine_process_commands(engine);
    engine_trace(engine, "transport %s", playing ? "play" : type == ENGINE_CMD_PAUSE ? "pause" : "stop");
    return true;
}

// Requests playback and reports acceptance separately from worker application.
bool engine_transport_play(Engine* engine) {
    return engine_transport_submit_playback(engine, ENGINE_CMD_PLAY);
}

// Requests stop, including the overload safety lane, without pretending it is already applied.
bool engine_transport_stop(Engine* engine) {
    return engine_transport_submit_playback(engine, ENGINE_CMD_STOP);
}

// Retains callback-delivered position and discards render-ahead audio for a later resume.
bool engine_transport_pause(Engine* engine) {
    return engine_transport_submit_playback(engine, ENGINE_CMD_PAUSE);
}

// Reads stable intent around an applied token, allowing application to precede submission's return.
EnginePlaybackSnapshot engine_transport_get_playback_snapshot(const Engine* engine) {
    EnginePlaybackSnapshot snapshot = {0};
    if (!engine) return snapshot;
    uint64_t requested, applied;
    do {
        requested = atomic_load_explicit(&engine->playback_requested_token, memory_order_acquire);
        applied = atomic_load_explicit(&engine->playback_applied_token, memory_order_acquire);
    } while (requested != atomic_load_explicit(&engine->playback_requested_token, memory_order_acquire));
    snapshot.applied_serial = applied >> 1;
    snapshot.applied_playing = (applied & 1u) != 0;
    snapshot.pending = (requested >> 1) > snapshot.applied_serial;
    snapshot.requested_serial = snapshot.pending ? requested >> 1 : snapshot.applied_serial;
    snapshot.requested_playing = snapshot.pending ? (requested & 1u) != 0 : snapshot.applied_playing;
    return snapshot;
}

// Returns accepted intent for controls that must respond before the worker consumes a command.
bool engine_transport_requested_playing(const Engine* engine) {
    return engine_transport_get_playback_snapshot(engine).requested_playing;
}

// Reports applied playback state rather than treating queue acceptance as worker execution.
bool engine_transport_is_playing(const Engine* engine) {
    if (!engine) {
        return false;
    }
    return atomic_load_explicit(&engine->transport_playing, memory_order_acquire);
}

// Preserves playback mode while atomically discarding queued audio from the preceding position.
bool engine_transport_seek(Engine* engine, uint64_t frame) {
    if (!engine_is_control_thread(engine)) return false;
    EngineCommand cmd = {.type = ENGINE_CMD_SEEK, .payload.seek.frame = frame};
    if (!engine_transport_submit(engine, &cmd)) return false;
    if (!engine->device_started || !engine->worker_thread) engine_process_commands(engine);
    return true;
}

// Applies one complete half-open loop interval and rejects invalid enabled ranges.
bool engine_transport_set_loop(Engine* engine, bool enabled, uint64_t start_frame, uint64_t end_frame) {
    if (!engine_is_control_thread(engine) || (enabled && end_frame <= start_frame)) return false;
    EngineCommand cmd = {.type = ENGINE_CMD_SET_LOOP,
        .payload.loop = {.enabled = enabled, .start_frame = start_frame, .end_frame = end_frame}};
    if (!engine_transport_submit(engine, &cmd)) return false;
    if (!engine->device_started || !engine->worker_thread) engine_process_commands(engine);
    return true;
}

// Retains the explicit render cursor API for engine scheduling and recording integration.
uint64_t engine_get_transport_frame(const Engine* engine) {
    if (!engine) {
        return 0;
    }
    return engine->transport_frame;
}
