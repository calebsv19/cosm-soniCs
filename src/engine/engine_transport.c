#include "engine/engine_internal.h"

#include "engine/graph.h"

bool engine_transport_play(Engine* engine) {
    if (!engine) {
        return false;
    }
    EngineCommand cmd = {.type = ENGINE_CMD_PLAY};
    if (!engine_post_command(engine, &cmd)) {
        SDL_Log("engine_transport_play: command queue full");
        return false;
    }
    atomic_store_explicit(&engine->transport_playing, true, memory_order_release);
    engine_trace(engine, "transport play");
    return true;
}

bool engine_transport_stop(Engine* engine) {
    if (!engine) {
        return false;
    }
    EngineCommand cmd = {.type = ENGINE_CMD_STOP};
    if (!engine_post_command(engine, &cmd)) {
        SDL_Log("engine_transport_stop: command queue full");
        return false;
    }
    atomic_store_explicit(&engine->transport_playing, false, memory_order_release);
    engine_trace(engine, "transport stop");
    return true;
}

bool engine_transport_is_playing(const Engine* engine) {
    if (!engine) {
        return false;
    }
    return atomic_load_explicit(&engine->transport_playing, memory_order_acquire);
}

bool engine_transport_seek(Engine* engine, uint64_t frame) {
    if (!engine) {
        return false;
    }
    if (!engine->device_started || !engine->worker_thread) {
        engine->transport_frame = frame;
        audio_queue_clear(&engine->output_queue);
        engine_graph_reset(engine->graph);
        engine_trace(engine, "transport seek frame=%llu", (unsigned long long)frame);
        return true;
    }

    EngineCommand cmd = {
        .type = ENGINE_CMD_SEEK,
        .payload.seek.frame = frame,
    };
    if (!engine_post_command(engine, &cmd)) {
        return false;
    }
    engine_trace(engine, "transport seek queued frame=%llu", (unsigned long long)frame);
    return true;
}

bool engine_transport_set_loop(Engine* engine, bool enabled, uint64_t start_frame, uint64_t end_frame) {
    if (!engine) {
        return false;
    }
    if (enabled && end_frame <= start_frame) {
        enabled = false;
    }

    if (!engine->device_started || !engine->worker_thread) {
        atomic_store_explicit(&engine->loop_enabled, enabled, memory_order_release);
        atomic_store_explicit(&engine->loop_start_frame, start_frame, memory_order_release);
        atomic_store_explicit(&engine->loop_end_frame, end_frame, memory_order_release);
        engine_trace(engine, "transport loop %s start=%llu end=%llu",
                     enabled ? "enabled" : "disabled",
                     (unsigned long long)start_frame,
                     (unsigned long long)end_frame);
        return true;
    }

    EngineCommand cmd = {
        .type = ENGINE_CMD_SET_LOOP,
        .payload.loop = {
            .enabled = enabled,
            .start_frame = start_frame,
            .end_frame = end_frame,
        },
    };
    if (!engine_post_command(engine, &cmd)) {
        return false;
    }
    engine_trace(engine, "transport loop queued %s start=%llu end=%llu",
                 enabled ? "enabled" : "disabled",
                 (unsigned long long)start_frame,
                 (unsigned long long)end_frame);
    return true;
}

uint64_t engine_get_transport_frame(const Engine* engine) {
    if (!engine) {
        return 0;
    }
    return engine->transport_frame;
}
