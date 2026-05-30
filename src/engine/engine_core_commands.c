#include "engine/engine_internal.h"

#include "engine/instrument.h"

#include <stdarg.h>

void engine_trace(const Engine* engine, const char* fmt, ...) {
    if (!engine || !engine->config.enable_engine_logs || !fmt) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, args);
    va_end(args);
}

void engine_timing_trace(const Engine* engine, const char* fmt, ...) {
    if (!engine || !engine->config.enable_timing_logs || !fmt) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE, fmt, args);
    va_end(args);
}

bool engine_post_command(Engine* engine, const EngineCommand* cmd) {
    if (!engine || !cmd) {
        return false;
    }
    return ringbuf_write(&engine->command_queue, cmd, sizeof(*cmd)) == sizeof(*cmd);
}

void engine_rebuild_sources(Engine* engine) {
    if (!engine || !engine->graph) {
        return;
    }
    if (engine->fxm && engine->fxm_mutex) {
        SDL_LockMutex(engine->fxm_mutex);
        fxm_set_track_count(engine->fxm, engine->track_count);
        SDL_UnlockMutex(engine->fxm_mutex);
    }
    engine_graph_clear_sources(engine->graph);
    bool any_solo = false;
    int record_armed_track = atomic_load_explicit(&engine->record_armed_track_index, memory_order_acquire);
    for (int i = 0; i < engine->track_count; ++i) {
        EngineTrack* track = &engine->tracks[i];
        if (i == record_armed_track) {
            if (!track->muted && track->solo) {
                any_solo = true;
                break;
            }
            continue;
        }
        if (!track->active || track->muted || track->clip_count == 0) {
            continue;
        }
        if (track->solo) {
            any_solo = true;
            break;
        }
    }

    for (int i = 0; i < engine->track_count; ++i) {
        EngineTrack* track = &engine->tracks[i];
        if (!track->active || track->clip_count == 0 || track->muted) {
            continue;
        }
        if (any_solo && !track->solo) {
            continue;
        }
        float track_gain = track->gain != 0.0f ? track->gain : 1.0f;
        for (int c = 0; c < track->clip_count; ++c) {
            EngineClip* clip = &track->clips[c];
            if (!clip->active) {
                continue;
            }
            float clip_gain = clip->gain != 0.0f ? clip->gain : 1.0f;
            if (clip->kind == ENGINE_CLIP_KIND_MIDI) {
                if (!clip->instrument) {
                    continue;
                }
                EngineInstrumentPresetId preset =
                    engine_clip_midi_effective_instrument_preset(engine, i, c);
                EngineInstrumentParams params =
                    engine_clip_midi_effective_instrument_params(engine, i, c);
                const EngineAutomationLane* track_lanes = NULL;
                int track_lane_count = 0;
                if (engine_clip_midi_inherits_track_instrument(clip)) {
                    engine_track_midi_get_instrument_automation_lanes(engine, i, &track_lanes, &track_lane_count);
                }
                if (engine_instrument_source_set_midi_clip(clip->instrument,
                                                           clip->timeline_start_frames,
                                                           clip->duration_frames,
                                                           preset,
                                                           params,
                                                           clip->midi_notes.notes,
                                                           clip->midi_notes.note_count,
                                                           track_lanes,
                                                           track_lane_count,
                                                           clip->automation_lanes,
                                                           clip->automation_lane_count)) {
                    engine_graph_add_source(engine->graph,
                                            &engine->instrument_ops,
                                            clip->instrument,
                                            track_gain * clip_gain,
                                            i);
                }
            } else if (clip->sampler) {
                engine_graph_add_source(engine->graph, &engine->sampler_ops, clip->sampler, track_gain * clip_gain, i);
            }
        }
    }
    if (engine->midi_audition_source && engine->midi_audition_notes.note_count > 0) {
        int track_index = engine->midi_audition_track_index;
        if (track_index < 0 || track_index >= engine->track_count) {
            track_index = 0;
        }
        EngineTrack* track = (track_index >= 0 && track_index < engine->track_count)
            ? &engine->tracks[track_index]
            : NULL;
        bool track_allowed = track && track->active && !track->muted && (!any_solo || track->solo);
        if (track_allowed &&
            engine_instrument_source_set_midi_clip(engine->midi_audition_source,
                                                   0,
                                                   UINT64_MAX,
                                                   engine->midi_audition_preset,
                                                   engine->midi_audition_params,
                                                   engine->midi_audition_notes.notes,
                                                   engine->midi_audition_notes.note_count,
                                                   NULL,
                                                   0,
                                                   NULL,
                                                   0)) {
            float track_gain = track->gain != 0.0f ? track->gain : 1.0f;
            engine_graph_add_source(engine->graph,
                                    &engine->instrument_ops,
                                    engine->midi_audition_source,
                                    track_gain,
                                    track_index);
        }
    }
    engine_graph_reset(engine->graph);
}

bool engine_request_rebuild_sources(Engine* engine) {
    if (!engine) {
        return false;
    }
    if (!engine->device_started || !engine->worker_thread) {
        atomic_store_explicit(&engine->rebuild_sources_pending, false, memory_order_release);
        engine_rebuild_sources(engine);
        return true;
    }
    if (atomic_exchange_explicit(&engine->rebuild_sources_pending, true, memory_order_acq_rel)) {
        return true;
    }
    EngineCommand cmd = {.type = ENGINE_CMD_REBUILD_SOURCES};
    if (!engine_post_command(engine, &cmd)) {
        atomic_store_explicit(&engine->rebuild_sources_pending, false, memory_order_release);
        return false;
    }
    return true;
}

void engine_process_commands(Engine* engine) {
    EngineCommand cmd;
    while (ringbuf_read(&engine->command_queue, &cmd, sizeof(cmd)) == sizeof(cmd)) {
        switch (cmd.type) {
            case ENGINE_CMD_PLAY:
                atomic_store_explicit(&engine->transport_playing, true, memory_order_release);
                break;
            case ENGINE_CMD_STOP:
                atomic_store_explicit(&engine->transport_playing, false, memory_order_release);
                audio_queue_clear(&engine->output_queue);
                engine_graph_reset(engine->graph);
                break;
            case ENGINE_CMD_GRAPH_SWAP:
                if (cmd.payload.graph_swap.new_graph) {
                    EngineGraph* old = engine->graph;
                    engine->graph = cmd.payload.graph_swap.new_graph;
                    if (old) {
                        engine_graph_destroy(old);
                    }
                    engine_graph_reset(engine->graph);
                }
                break;
            case ENGINE_CMD_SEEK:
                engine->transport_frame = cmd.payload.seek.frame;
                audio_queue_clear(&engine->output_queue);
                engine_graph_reset(engine->graph);
                break;
            case ENGINE_CMD_SET_LOOP:
                atomic_store_explicit(&engine->loop_enabled, cmd.payload.loop.enabled, memory_order_release);
                atomic_store_explicit(&engine->loop_start_frame, cmd.payload.loop.start_frame, memory_order_release);
                atomic_store_explicit(&engine->loop_end_frame, cmd.payload.loop.end_frame, memory_order_release);
                break;
            case ENGINE_CMD_SET_EQ:
                if (cmd.payload.eq.target == 0) {
                    if (engine->eq_mutex) {
                        SDL_LockMutex(engine->eq_mutex);
                    }
                    engine_eq_set_curve(&engine->master_eq, &cmd.payload.eq.curve);
                    if (engine->eq_mutex) {
                        SDL_UnlockMutex(engine->eq_mutex);
                    }
                } else if (cmd.payload.eq.track_index >= 0 && cmd.payload.eq.track_index < engine->track_count) {
                    if (engine->eq_mutex) {
                        SDL_LockMutex(engine->eq_mutex);
                    }
                    engine_eq_set_curve(&engine->tracks[cmd.payload.eq.track_index].track_eq, &cmd.payload.eq.curve);
                    if (engine->eq_mutex) {
                        SDL_UnlockMutex(engine->eq_mutex);
                    }
                }
                break;
            case ENGINE_CMD_REBUILD_SOURCES:
                engine_rebuild_sources(engine);
                atomic_store_explicit(&engine->rebuild_sources_pending, false, memory_order_release);
                break;
            case ENGINE_CMD_SET_FX_PARAM:
                if (engine->fxm) {
                    if (cmd.payload.fx_param.is_master) {
                        fxm_master_set_param_target(engine->fxm,
                                                    cmd.payload.fx_param.id,
                                                    cmd.payload.fx_param.param_index,
                                                    cmd.payload.fx_param.value,
                                                    cmd.payload.fx_param.mode,
                                                    cmd.payload.fx_param.beat_value,
                                                    &engine->tempo);
                    } else {
                        fxm_track_set_param_target(engine->fxm,
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
            case ENGINE_CMD_SET_TEMPO: {
                TempoState tempo = cmd.payload.tempo.tempo;
                if (tempo.sample_rate <= 0.0) {
                    tempo.sample_rate = engine->config.sample_rate;
                }
                tempo_state_clamp(&tempo);
                engine->tempo = tempo;
                break;
            }
            case ENGINE_CMD_MIDI_AUDITION_NOTE_ON:
                engine_midi_audition_apply_note_on(engine,
                                                   cmd.payload.midi_audition.track_index,
                                                   cmd.payload.midi_audition.preset,
                                                   cmd.payload.midi_audition.params,
                                                   cmd.payload.midi_audition.note,
                                                   cmd.payload.midi_audition.velocity);
                break;
            case ENGINE_CMD_MIDI_AUDITION_NOTE_OFF:
                engine_midi_audition_apply_note_off(engine, cmd.payload.midi_audition.note);
                break;
            case ENGINE_CMD_MIDI_AUDITION_ALL_OFF:
                engine_midi_audition_apply_all_off(engine);
                break;
            default:
                break;
        }
    }
}
