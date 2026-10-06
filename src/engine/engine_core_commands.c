#include "engine/engine_internal.h"

#include "engine/instrument.h"

#include <stdarg.h>
#include "core_time.h"

enum {
    ENGINE_COMMAND_RESERVED_PACKETS = 8,
    ENGINE_COMMAND_SAFETY_STOP = 1u,
    ENGINE_COMMAND_SAFETY_ALL_OFF = 2u
};

void engine_trace(const Engine* engine, const char* fmt, ...) {
    if (!engine || !atomic_load(&engine->runtime_engine_logs) || !fmt) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, args);
    va_end(args);
}

void engine_timing_trace(const Engine* engine, const char* fmt, ...) {
    if (!engine || !atomic_load(&engine->runtime_timing_logs) || !fmt) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE, fmt, args);
    va_end(args);
}

// Preserves packet boundaries and reserves headroom for safety-critical control actions.
bool engine_post_command(Engine* engine, const EngineCommand* cmd) {
    if (!engine || !cmd) {
        return false;
    }
    if (SDL_ThreadID() != engine->control_thread_id ||
        cmd->type < ENGINE_CMD_PLAY || cmd->type > ENGINE_CMD_PAUSE) {
        atomic_fetch_add_explicit(&engine->commands_rejected, 1, memory_order_relaxed);
        return false;
    }
    unsigned safety = 0;
    if ((cmd->type == ENGINE_CMD_STOP || cmd->type == ENGINE_CMD_PAUSE)) {
        safety = ENGINE_COMMAND_SAFETY_STOP;
    } else if (cmd->type == ENGINE_CMD_MIDI_AUDITION_NOTE_OFF ||
               cmd->type == ENGINE_CMD_MIDI_AUDITION_ALL_OFF) {
        safety = ENGINE_COMMAND_SAFETY_ALL_OFF;
    }
    // Freeze regular admission until the preceding FIFO drains and safety is acknowledged.
    if (atomic_load_explicit(&engine->command_safety_pending, memory_order_acquire) != 0) {
        if (safety) {
            if (cmd->type == ENGINE_CMD_STOP || cmd->type == ENGINE_CMD_PAUSE) {
                atomic_store_explicit(&engine->playback_safety_token, (cmd->payload.transport_token << 1) | 1u | (cmd->type == ENGINE_CMD_PAUSE ? 2u : 0u), memory_order_release);
            }
            atomic_fetch_or_explicit(&engine->command_safety_pending, safety, memory_order_release);
            atomic_fetch_add_explicit(&engine->command_safety_fallbacks, 1, memory_order_relaxed);
            return true;
        }
        atomic_fetch_add_explicit(&engine->commands_rejected, 1, memory_order_relaxed);
        return false;
    }
    EngineCommand packet = *cmd;
    packet.submitted_ns = core_time_now_ns();
    size_t reserve = safety ? 0 : ENGINE_COMMAND_RESERVED_PACKETS * sizeof(*cmd);
    if (ringbuf_available_write(&engine->command_queue) >= sizeof(*cmd) + reserve &&
        ringbuf_write_exact(&engine->command_queue, &packet, sizeof(packet))) {
        atomic_fetch_add_explicit(&engine->commands_accepted, 1, memory_order_relaxed);
        return true;
    }
    if (safety) {
        // Overload note-off silences every audition voice rather than leaving a stuck note.
        if (cmd->type == ENGINE_CMD_STOP || cmd->type == ENGINE_CMD_PAUSE) {
            atomic_store_explicit(&engine->playback_safety_token, (cmd->payload.transport_token << 1) | 1u | (cmd->type == ENGINE_CMD_PAUSE ? 2u : 0u), memory_order_release);
        }
        atomic_fetch_or_explicit(&engine->command_safety_pending, safety, memory_order_release);
        atomic_fetch_add_explicit(&engine->command_safety_fallbacks, 1, memory_order_relaxed);
        return true;
    }
    atomic_fetch_add_explicit(&engine->commands_rejected, 1, memory_order_relaxed);
    return false;
}

// Reads independent atomic counters without touching packet bytes or reader indices.
bool engine_get_command_stats(const Engine* engine, EngineCommandStats* out_stats) {
    if (!engine || !out_stats) {
        return false;
    }
    out_stats->accepted = atomic_load_explicit(&engine->commands_accepted, memory_order_relaxed);
    out_stats->rejected = atomic_load_explicit(&engine->commands_rejected, memory_order_relaxed);
    out_stats->applied = atomic_load_explicit(&engine->commands_applied, memory_order_relaxed);
    out_stats->safety_fallbacks = atomic_load_explicit(&engine->command_safety_fallbacks, memory_order_relaxed);
    return true;
}

// Publishes the applied playback state and acknowledges a serial-tagged request, including safety stop.
static void engine_playback_applied(Engine* engine, uint64_t token, bool playing) {
    if (!token) token = atomic_load_explicit(&engine->playback_applied_token, memory_order_relaxed);
    atomic_store_explicit(&engine->transport_playing, playing, memory_order_release);
    atomic_store_explicit(&engine->playback_applied_token, (token & ~UINT64_C(1)) | (playing ? 1u : 0u), memory_order_release);
}

// Resets processor histories after the short callback exclusion window has ended.
void engine_transport_reset_history(Engine* engine) {
    engine_graph_reset(engine_render_source_graph(engine));
    engine_graph_reset_control_ramps(engine_render_source_graph(engine));
    EngineMixState* mix = engine_render_mix_state(engine);
    if (!mix) return;
    engine_eq_reset(&mix->master_eq);
    for (int t = 0; t < mix->track_count; ++t) {
        engine_eq_reset(&mix->tracks[t].track_eq);
        fx_sample_ramp_reset(&mix->track_pan_ramps[t], mix->tracks[t].pan);
        fx_sample_ramp_reset(&mix->audition_gain_ramps[t], mix->audition_gain_ramps[t].target);
    }
    fxm_reset_render_state(mix->fxm);
}

// Applies a bounded FIFO batch, then coalesced rebuilds and overflow safety requests.
void engine_process_commands(Engine* engine) {
    if (!engine) {
        return;
    }
    engine_source_plan_apply(engine);
    EngineCommand cmd;
    size_t budget = ringbuf_available_read(&engine->command_queue) / sizeof(cmd);
    while (budget-- > 0 && ringbuf_read_exact(&engine->command_queue, &cmd, sizeof(cmd))) {
        uint64_t now = core_time_now_ns();
        uint64_t age = now > cmd.submitted_ns ? now - cmd.submitted_ns : 0;
        atomic_store_explicit(&engine->diag_command_last_age_ns, age, memory_order_relaxed);
        if (age > atomic_load_explicit(&engine->diag_command_max_age_ns, memory_order_relaxed))
            atomic_store_explicit(&engine->diag_command_max_age_ns, age, memory_order_relaxed);
        atomic_fetch_add_explicit(&engine->commands_applied, 1, memory_order_relaxed);
        if (cmd.transport_serial && cmd.transport_serial <= atomic_load(&engine->transport_applied_serial)) continue;
        switch (cmd.type) {
            case ENGINE_CMD_PLAY:
                if (!atomic_load(&engine->transport_playing)) engine_midi_audition_apply_all_off(engine);
                if (!atomic_load(&engine->transport_playing))
                    engine_clock_discontinuity(engine, engine->transport_frame, false, true);
                engine_playback_applied(engine, cmd.payload.transport_token, true);
                break;
            case ENGINE_CMD_STOP:
            case ENGINE_CMD_PAUSE:
                engine_clock_discontinuity(engine, 0, cmd.type == ENGINE_CMD_PAUSE, false);
                engine_transport_reset_history(engine);
                engine_midi_audition_apply_all_off(engine);
                engine_playback_applied(engine, cmd.payload.transport_token, false);
                break;
            case ENGINE_CMD_GRAPH_SWAP:
                if (cmd.payload.graph_swap.plan) {
                    engine_source_plan_adopt(engine, cmd.payload.graph_swap.plan);
                    engine_graph_reset(engine_render_source_graph(engine));
                }
                break;
            case ENGINE_CMD_SEEK:
                engine_midi_audition_apply_all_off(engine);
                engine_clock_discontinuity(engine, cmd.payload.seek.frame, false, atomic_load(&engine->transport_playing));
                engine_transport_reset_history(engine);
                break;
            case ENGINE_CMD_SET_LOOP:
                engine_midi_audition_apply_all_off(engine);
                atomic_store_explicit(&engine->loop_enabled, cmd.payload.loop.enabled, memory_order_release);
                atomic_store_explicit(&engine->loop_start_frame, cmd.payload.loop.start_frame, memory_order_release);
                atomic_store_explicit(&engine->loop_end_frame, cmd.payload.loop.end_frame, memory_order_release);
                engine_clock_discontinuity(engine, 0, true, atomic_load(&engine->transport_playing));
                engine_transport_reset_history(engine);
                break;
            case ENGINE_CMD_SET_EQ: {
                EngineMixState* mix = engine_render_mix_state(engine);
                if (mix && cmd.payload.eq.target == 0)
                    engine_eq_set_curve(&mix->master_eq, &cmd.payload.eq.curve);
                else if (mix && cmd.payload.eq.track_index >= 0 && cmd.payload.eq.track_index < mix->track_count)
                    engine_eq_set_curve(&mix->tracks[cmd.payload.eq.track_index].track_eq, &cmd.payload.eq.curve);
                break;
            }
            case ENGINE_CMD_REBUILD_SOURCES:
                engine_source_plan_apply(engine);
                atomic_store_explicit(&engine->rebuild_sources_pending, false, memory_order_release);
                break;
            case ENGINE_CMD_SET_FX_PARAM: {
                EngineMixState* mix = engine_render_mix_state(engine);
                if (mix && mix->fxm) {
                    if (cmd.payload.fx_param.is_master) {
                        fxm_master_set_param_target(mix->fxm,
                                                    cmd.payload.fx_param.id,
                                                    cmd.payload.fx_param.param_index,
                                                    cmd.payload.fx_param.value,
                                                    cmd.payload.fx_param.mode,
                                                    cmd.payload.fx_param.beat_value,
                                                    &engine->tempo);
                    } else {
                        fxm_track_set_param_target(mix->fxm,
                                                   cmd.payload.fx_param.track_index,
                                                   cmd.payload.fx_param.id,
                                                   cmd.payload.fx_param.param_index,
                                                   cmd.payload.fx_param.value,
                                                   cmd.payload.fx_param.mode,
                                                   cmd.payload.fx_param.beat_value,
                                                   &engine->tempo);
                    }
                }
                break;
            }
            case ENGINE_CMD_SET_TEMPO: {
                TempoState tempo = cmd.payload.tempo.tempo;
                if (tempo.sample_rate <= 0.0) {
                    tempo.sample_rate = engine->config.sample_rate;
                }
                tempo_state_clamp(&tempo);
                engine->tempo = tempo;
                break;
            }
            case ENGINE_CMD_MIDI_AUDITION_NOTE_ON: {
                EngineMixState* mix = engine_render_mix_state(engine);
                int track = -1;
                for (int t = 0; mix && t < mix->track_count; ++t) {
                    if (mix->tracks[t].runtime_id == cmd.payload.midi_audition.track_runtime_id) { track = t; break; }
                }
                if (track < 0) break;
                engine_midi_audition_apply_note_on(engine,
                                                   track,
                                                   cmd.payload.midi_audition.preset,
                                                   cmd.payload.midi_audition.params,
                                                   cmd.payload.midi_audition.note,
                                                   cmd.payload.midi_audition.velocity);
                break;
            }
            case ENGINE_CMD_MIDI_AUDITION_NOTE_OFF:
                engine_midi_audition_apply_note_off(engine, cmd.payload.midi_audition.note);
                break;
            case ENGINE_CMD_MIDI_AUDITION_ALL_OFF:
                engine_midi_audition_apply_all_off(engine);
                break;
            default:
                break;
        }
        if (cmd.transport_serial) atomic_store(&engine->transport_applied_serial, cmd.transport_serial);
    }
    if (atomic_exchange_explicit(&engine->rebuild_sources_pending, false, memory_order_acq_rel)) {
        engine_source_plan_apply(engine);
    }
    // Wait until the FIFO preceding an emergency is drained; new play/note-on are rejected.
    if (ringbuf_available_read(&engine->command_queue) == 0) {
        unsigned safety = atomic_exchange_explicit(&engine->command_safety_pending, 0, memory_order_acq_rel);
        if (safety & ENGINE_COMMAND_SAFETY_ALL_OFF) {
            engine_midi_audition_apply_all_off(engine);
        }
        if (safety & ENGINE_COMMAND_SAFETY_STOP) {
            uint64_t request = atomic_exchange_explicit(&engine->playback_safety_token, 0, memory_order_acq_rel);
            // A coalesced request can be consumed before its producer republishes the notification bit.
            // An empty repeated notification must not turn an already-applied pause into stop-to-zero.
            if (request & 1u) {
                uint64_t token = (request >> 2) << 1;
                engine_clock_discontinuity(engine, 0, (request & 2u) != 0, false);
                engine_midi_audition_apply_all_off(engine);
                engine_transport_reset_history(engine);
                engine_playback_applied(engine, token, false);
                if (token >> 1) atomic_store(&engine->transport_applied_serial, token >> 1);
            }
        }
    }
}

// Discards queued work only when no worker can still read packets or own their payloads.
void engine_cancel_commands(Engine* engine) {
    if (!engine || engine->worker_thread) return;
    EngineCommand cmd;
    while (ringbuf_read_exact(&engine->command_queue, &cmd, sizeof(cmd))) {
        if (cmd.type == ENGINE_CMD_GRAPH_SWAP) engine_source_plan_discard(engine, cmd.payload.graph_swap.plan);
    }
    atomic_store_explicit(&engine->command_safety_pending, 0, memory_order_release);
    atomic_store(&engine->playback_safety_token, 0);
    atomic_store(&engine->playback_requested_token, atomic_load(&engine->playback_applied_token));
    atomic_store(&engine->transport_requested_serial, atomic_load(&engine->transport_applied_serial));
}
