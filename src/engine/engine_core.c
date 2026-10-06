#include "engine/engine_internal.h"

#include "engine/instrument.h"
#include "engine/sampler.h"
#include "effects/effects_builtin.h"
#include "core/loop/daw_mainthread_messages.h"
#include "core_time.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

// Services accepted edits and produces queued audio while measuring each complete busy iteration.
static int engine_worker_main(void* userdata) {
    Engine* engine = (Engine*)userdata;
    if (!engine) {
        return -1;
    }
    engine->worker_thread_id = SDL_ThreadID();
    // Prefer prompt render-ahead service without requiring privileged real-time scheduling.
    atomic_store(&engine->diag_worker_priority_status,
                 SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) == 0 ? 1 : -1);
    engine->render_warned_fxm_mutex = false;
    engine->render_warned_meter_mutex = false;
    int channels = engine_graph_get_channels(engine->graph);
    if (channels <= 0) {
        channels = engine->output_queue.channels > 0 ? engine->output_queue.channels : 2;
    }
    const int block = engine->config.block_size;
    float* block_buffer = engine->render_buffer;
    float* track_buffer = engine->render_track_buffer;
    if (!block_buffer || !track_buffer) return -1;

    uint64_t last_report_ns = core_time_now_ns();
    uint64_t last_transport_notify_ns = last_report_ns;
    double accum_ms = 0.0;
    int accum_blocks = 0;

    while (atomic_load_explicit(&engine->worker_running, memory_order_acquire)) {
        uint64_t cycle_started_ns = core_time_now_ns();
        engine_process_commands(engine);
        bool transport_playing = atomic_load_explicit(&engine->transport_playing, memory_order_acquire);
        engine_midi_audition_retire(engine);
        bool audition_active = engine->midi_audition_notes.note_count > 0 || engine->midi_audition_tail_until > 0;
        uint64_t service_ns = core_time_diff_ns(core_time_now_ns(), cycle_started_ns);
        if (!transport_playing && !audition_active) {
            engine_diagnostics_worker(engine, service_ns, service_ns, false);
            SDL_Delay(2);
            continue;
        }

        size_t queued = audio_queue_available_frames(&engine->output_queue);
        size_t target = atomic_load_explicit(&engine->output_target_frames, memory_order_relaxed);
        if (queued > target || target - queued < (size_t)block ||
            audio_queue_space_frames(&engine->output_queue) < (size_t)block) {
            engine_diagnostics_worker(engine, core_time_diff_ns(core_time_now_ns(), cycle_started_ns), service_ns, false);
            SDL_Delay(1);
            continue;
        }

        if (!atomic_load(&engine->runtime_timing_logs)) {
            accum_ms = 0.0;
            accum_blocks = 0;
            last_report_ns = core_time_now_ns();
        }

        uint64_t render_started_ns = core_time_now_ns();
        int frames_remaining = block;
        int produced = 0;
        while (frames_remaining > 0) {
            bool loop_enabled = atomic_load_explicit(&engine->loop_enabled, memory_order_acquire);
            uint64_t loop_start = atomic_load_explicit(&engine->loop_start_frame, memory_order_acquire);
            uint64_t loop_end = atomic_load_explicit(&engine->loop_end_frame, memory_order_acquire);
            if (loop_enabled && loop_end <= loop_start) {
                loop_enabled = false;
            }
            int chunk = frames_remaining;
            uint64_t current = transport_playing
                ? engine->transport_frame
                : engine->transport_frame + engine->midi_audition_idle_frame;
            bool loop_active = loop_enabled && loop_end > loop_start;
            bool loop_this_block = false;
            if (loop_active && transport_playing) {
                if (current >= loop_start && current < loop_end) {
                    uint64_t loop_len = loop_end - loop_start;
                    uint64_t frames_until_end = loop_end - current;
                    loop_this_block = true;
                    if (loop_len == 0) {
                        loop_active = false;
                        loop_this_block = false;
                    } else if ((uint64_t)chunk > frames_until_end) {
                        chunk = (int)frames_until_end;
                    }
                } else if (current < loop_start) {
                    uint64_t frames_until_start = loop_start - current;
                    if ((uint64_t)chunk > frames_until_start) {
                        chunk = (int)frames_until_start;
                    }
                }
            }

            if (chunk <= 0) {
                chunk = frames_remaining;
            }

            uint64_t start_ns = atomic_load(&engine->runtime_timing_logs) ? core_time_now_ns() : 0;

            engine_spectrum_begin_block(engine);
            engine_spectrogram_begin_block(engine);

            // mix tracks with per-track FX
            float* out_ptr = block_buffer + produced * channels;
            uint64_t render_frame = transport_playing
                ? engine->transport_frame
                : engine->transport_frame + engine->midi_audition_idle_frame;
            if (transport_playing) {
                engine_mix_tracks(engine, render_frame, chunk, out_ptr, track_buffer, channels);
            } else {
                engine_mix_midi_audition_only(engine, render_frame, chunk, out_ptr, track_buffer, channels);
            }
            engine_spectrum_update(engine, out_ptr, chunk, channels);

            if (transport_playing) {
                uint64_t frame = engine->transport_frame;
                engine->transport_frame = engine_clock_advance(frame, (uint64_t)chunk, 0, 0);
            } else {
                engine->midi_audition_idle_frame += (uint64_t)chunk;
            }
            produced += chunk;
            frames_remaining -= chunk;

            if (atomic_load(&engine->runtime_timing_logs)) {
                uint64_t end_ns = core_time_now_ns();
                double elapsed_ms = core_time_ns_to_seconds(core_time_diff_ns(end_ns, start_ns)) * 1000.0;
                accum_ms += elapsed_ms;
                accum_blocks += 1;
                if (core_time_diff_ns(end_ns, last_report_ns) >= 1000000000ULL && accum_blocks > 0) {
                    double avg_ms = accum_ms / (double)accum_blocks;
                    size_t queued = audio_queue_available_frames(&engine->output_queue);
                    engine_timing_trace(engine, "worker avg render %.3fms (%d blocks) queue=%zu", avg_ms, accum_blocks, queued);
                    char health[256];
                    (void)engine_format_diagnostics(engine, health, sizeof(health));
                    engine_timing_trace(engine, "%s", health);
                    accum_ms = 0.0;
                    accum_blocks = 0;
                    last_report_ns = end_ns;
                }
            }

            if (transport_playing && loop_this_block) {
                uint64_t loop_start_cur = atomic_load_explicit(&engine->loop_start_frame, memory_order_acquire);
                uint64_t loop_end_cur = atomic_load_explicit(&engine->loop_end_frame, memory_order_acquire);
                if (atomic_load_explicit(&engine->loop_enabled, memory_order_acquire) && loop_end_cur > loop_start_cur &&
                    engine->transport_frame >= loop_end_cur) {
                    engine_midi_audition_apply_all_off(engine);
                    engine->transport_frame = loop_start_cur;
                    engine_graph_reset(engine_render_source_graph(engine));
                }
            }
        }

        engine_sanitize_block(block_buffer, (size_t)block * (size_t)channels);

        uint64_t now_ns = core_time_now_ns();
        if (transport_playing && core_time_diff_ns(now_ns, last_transport_notify_ns) >= 16000000ULL) {
            (void)daw_mainthread_message_post(DAW_MAINTHREAD_MSG_ENGINE_TRANSPORT,
                                              engine->transport_frame,
                                              engine);
            last_transport_notify_ns = now_ns;
        }

        engine_clock_write(engine, block_buffer, (size_t)block);
        engine_diagnostics_render(engine, core_time_diff_ns(core_time_now_ns(), render_started_ns), (size_t)block);
        engine_diagnostics_worker(engine, core_time_diff_ns(core_time_now_ns(), cycle_started_ns), service_ns, true);
    }

    return 0;
}

Engine* engine_create(const EngineRuntimeConfig* cfg) {
    Engine* engine = (Engine*)calloc(1, sizeof(Engine));
    if (!engine) {
        return NULL;
    }
    if (cfg) {
        engine->config = *cfg;
    } else {
        config_set_defaults(&engine->config);
    }
    if (engine->config.output_queue_blocks < 2 || engine->config.output_queue_blocks > 32)
        engine->config.output_queue_blocks = 32;
    atomic_init(&engine->output_target_frames, 0);
    atomic_init(&engine->diag_worker_priority_status, 0);
    atomic_init(&engine->diag_worker_cycles, 0);
    atomic_init(&engine->diag_worker_render_cycles, 0);
    atomic_init(&engine->diag_worker_over_budget, 0);
    atomic_init(&engine->diag_worker_max_ns, 0);
    atomic_init(&engine->diag_service_max_ns, 0);
    engine->tempo = tempo_state_default(engine->config.sample_rate);
    engine->device_started = false;
    engine->worker_thread = NULL;
    engine->control_thread_id = SDL_ThreadID();
    atomic_init(&engine->worker_thread_id, 0);
    atomic_init(&engine->runtime_engine_logs, engine->config.enable_engine_logs);
    atomic_init(&engine->runtime_timing_logs, engine->config.enable_timing_logs);
    engine_analysis_init(&engine->spectrum_stream);
    engine_analysis_init(&engine->spectrogram_stream);
    atomic_init(&engine->pending_source_plan, NULL);
    atomic_init(&engine->retired_source_plans, NULL);
    atomic_init(&engine->command_safety_pending, 0);
    atomic_init(&engine->diag_processing_latency, 0);
    atomic_init(&engine->diag_callback_count, 0);
    atomic_init(&engine->diag_underrun_callbacks, 0);
    atomic_init(&engine->diag_underrun_frames, 0);
    atomic_init(&engine->diag_render_blocks, 0);
    atomic_init(&engine->diag_render_over_budget, 0);
    atomic_init(&engine->diag_render_max_ns, 0);
    atomic_init(&engine->diag_command_max_age_ns, 0);
    atomic_init(&engine->diag_command_last_age_ns, 0);
    atomic_init(&engine->diag_queue_high_water_frames, 0);
    atomic_init(&engine->commands_accepted, 0);
    atomic_init(&engine->commands_rejected, 0);
    atomic_init(&engine->commands_applied, 0);
    atomic_init(&engine->command_safety_fallbacks, 0);
    atomic_init(&engine->worker_running, false);
    atomic_init(&engine->transport_playing, false);
    atomic_init(&engine->playback_requested_token, 0);
    atomic_init(&engine->playback_applied_token, 0);
    atomic_init(&engine->playback_safety_token, 0);
    atomic_init(&engine->rebuild_sources_pending, false);
    atomic_init(&engine->record_armed_track_index, -1);
    atomic_init(&engine->loop_enabled, false);
    atomic_init(&engine->loop_start_frame, 0);
    atomic_init(&engine->loop_end_frame, 0);
    if (!ringbuf_init(&engine->command_queue, sizeof(EngineCommand) * 64)) {
        free(engine);
        return NULL;
    }
    if (!ringbuf_init(&engine->spectrum_queue, ENGINE_SPECTRUM_QUEUE_BYTES)) {
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    if (!ringbuf_init(&engine->spectrogram_queue, ENGINE_SPECTROGRAM_QUEUE_BYTES)) {
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->fxm_mutex = SDL_CreateMutex();
    if (!engine->fxm_mutex) {
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->eq_mutex = SDL_CreateMutex();
    if (!engine->eq_mutex) {
        SDL_DestroyMutex(engine->fxm_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->spectrum_mutex = SDL_CreateMutex();
    if (!engine->spectrum_mutex) {
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->spectrogram_mutex = SDL_CreateMutex();
    if (!engine->spectrogram_mutex) {
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        SDL_DestroyMutex(engine->spectrum_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->meter_mutex = SDL_CreateMutex();
    if (!engine->meter_mutex) {
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        SDL_DestroyMutex(engine->spectrum_mutex);
        SDL_DestroyMutex(engine->spectrogram_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->graph = engine_graph_create(engine->config.sample_rate, 2, engine->config.block_size);
    if (!engine->graph) {
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        SDL_DestroyMutex(engine->spectrum_mutex);
        SDL_DestroyMutex(engine->spectrogram_mutex);
        SDL_DestroyMutex(engine->meter_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine_eq_init(&engine->master_eq, (float)engine->config.sample_rate, engine_graph_get_channels(engine->graph));
    engine_tone_source_ops(&engine->tone_ops);
    engine_sampler_source_ops(&engine->sampler_ops);
    engine_instrument_source_ops(&engine->instrument_ops);
    engine->tone_source = engine_tone_source_create();
    if (!engine->tone_source) {
        engine_graph_destroy(engine->graph);
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        SDL_DestroyMutex(engine->spectrum_mutex);
        SDL_DestroyMutex(engine->spectrogram_mutex);
        SDL_DestroyMutex(engine->meter_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }

    engine->track_capacity = 4;
    engine->tracks = (EngineTrack*)calloc((size_t)engine->track_capacity, sizeof(EngineTrack));
    if (!engine->tracks) {
        engine_tone_source_destroy(engine->tone_source);
        engine_graph_destroy(engine->graph);
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        SDL_DestroyMutex(engine->spectrum_mutex);
        SDL_DestroyMutex(engine->spectrogram_mutex);
        SDL_DestroyMutex(engine->meter_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->audio_sources = NULL;
    engine->audio_source_count = 0;
    engine->audio_source_capacity = 0;
    engine->track_spectrum_capacity = engine->track_capacity;
    engine->track_spectra = (float*)malloc(sizeof(float) * (size_t)engine->track_capacity * ENGINE_SPECTRUM_BINS);
    if (!engine->track_spectra) {
        free(engine->tracks);
        engine->tracks = NULL;
        engine_tone_source_destroy(engine->tone_source);
        engine_graph_destroy(engine->graph);
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        SDL_DestroyMutex(engine->spectrum_mutex);
        SDL_DestroyMutex(engine->spectrogram_mutex);
        SDL_DestroyMutex(engine->meter_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->track_meter_capacity = engine->track_capacity;
    engine->track_meters = (EngineMeterState*)malloc(sizeof(EngineMeterState) * (size_t)engine->track_capacity);
    if (!engine->track_meters) {
        free(engine->track_spectra);
        engine->track_spectra = NULL;
        free(engine->tracks);
        engine->tracks = NULL;
        engine_tone_source_destroy(engine->tone_source);
        engine_graph_destroy(engine->graph);
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        SDL_DestroyMutex(engine->spectrum_mutex);
        SDL_DestroyMutex(engine->spectrogram_mutex);
        SDL_DestroyMutex(engine->meter_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->track_meter_snapshots = (EngineMeterSnapshot*)calloc((size_t)engine->track_capacity * 2u,
                                                                 sizeof(EngineMeterSnapshot));
    if (!engine->track_meter_snapshots) {
        free(engine->track_meters);
        engine->track_meters = NULL;
        free(engine->track_spectra);
        engine->track_spectra = NULL;
        free(engine->tracks);
        engine->tracks = NULL;
        engine_tone_source_destroy(engine->tone_source);
        engine_graph_destroy(engine->graph);
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        SDL_DestroyMutex(engine->spectrum_mutex);
        SDL_DestroyMutex(engine->spectrogram_mutex);
        SDL_DestroyMutex(engine->meter_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->track_fx_meter_capacity = engine->track_capacity;
    engine->track_fx_meters = (EngineFxMeterBank*)calloc((size_t)engine->track_capacity, sizeof(EngineFxMeterBank));
    if (!engine->track_fx_meters) {
        free(engine->track_meter_snapshots);
        engine->track_meter_snapshots = NULL;
        free(engine->track_meters);
        engine->track_meters = NULL;
        free(engine->track_spectra);
        engine->track_spectra = NULL;
        free(engine->tracks);
        engine->tracks = NULL;
        engine_tone_source_destroy(engine->tone_source);
        engine_graph_destroy(engine->graph);
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        SDL_DestroyMutex(engine->spectrum_mutex);
        SDL_DestroyMutex(engine->spectrogram_mutex);
        SDL_DestroyMutex(engine->meter_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    engine->track_fx_meter_snapshots = calloc((size_t)engine->track_capacity * 2u,
                                               sizeof(*engine->track_fx_meter_snapshots));
    if (!engine->track_fx_meter_snapshots) {
        free(engine->track_fx_meters);
        engine->track_fx_meters = NULL;
        free(engine->track_meter_snapshots);
        engine->track_meter_snapshots = NULL;
        free(engine->track_meters);
        engine->track_meters = NULL;
        free(engine->track_spectra);
        engine->track_spectra = NULL;
        free(engine->tracks);
        engine->tracks = NULL;
        engine_tone_source_destroy(engine->tone_source);
        engine_graph_destroy(engine->graph);
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        SDL_DestroyMutex(engine->spectrum_mutex);
        SDL_DestroyMutex(engine->spectrogram_mutex);
        SDL_DestroyMutex(engine->meter_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    if (!engine_scope_host_init(engine, engine->track_capacity)) {
        free(engine->track_fx_meters);
        engine->track_fx_meters = NULL;
        free(engine->track_meters);
        engine->track_meters = NULL;
        free(engine->track_spectra);
        engine->track_spectra = NULL;
        free(engine->tracks);
        engine->tracks = NULL;
        engine_tone_source_destroy(engine->tone_source);
        engine_graph_destroy(engine->graph);
        SDL_DestroyMutex(engine->fxm_mutex);
        SDL_DestroyMutex(engine->eq_mutex);
        SDL_DestroyMutex(engine->spectrum_mutex);
        SDL_DestroyMutex(engine->spectrogram_mutex);
        SDL_DestroyMutex(engine->meter_mutex);
        ringbuf_free(&engine->spectrogram_queue);
        ringbuf_free(&engine->spectrum_queue);
        ringbuf_free(&engine->command_queue);
        free(engine);
        return NULL;
    }
    for (int i = 0; i < engine->track_capacity; ++i) {
        engine_track_init(&engine->tracks[i]);
        engine_eq_init(&engine->tracks[i].track_eq, (float)engine->config.sample_rate, engine_graph_get_channels(engine->graph));
    }
    engine->track_count = 0;
    atomic_init(&engine->transport_frame, 0);
    atomic_init(&engine->clock_epoch, 0);
    atomic_init(&engine->clock_capture_epoch, 0);
    atomic_init(&engine->clock_render_seq, 0);
    atomic_init(&engine->clock_callback_seq, 0);
    atomic_init(&engine->clock_origin, 0);
    atomic_init(&engine->clock_fallback_frame, 0);
    atomic_init(&engine->clock_rendered, 0);
    atomic_init(&engine->clock_consumed, 0);
    atomic_init(&engine->clock_callback_start, 0);
    atomic_init(&engine->clock_callback_frames, 0);
    atomic_init(&engine->clock_callback_ns, 0);
    atomic_init(&engine->clock_loop_start, 0);
    atomic_init(&engine->clock_loop_end, 0);
    atomic_init(&engine->clock_advancing, 0);
    atomic_init(&engine->transport_requested_serial, 0);
    atomic_init(&engine->transport_applied_serial, 0);

    engine->next_clip_id = 1;
    engine->spectrum_history_index = 0;
    engine->spectrum_bins = ENGINE_SPECTRUM_BINS;
    engine->spectrum_update_active = false;
    engine->spectrum_update_master = false;
    engine->spectrum_update_track = false;
    engine->spectrum_active_track = -1;
    engine->spectrum_thread = NULL;
    atomic_init(&engine->spectrum_running, false);
    ringbuf_reset(&engine->spectrum_queue);
    engine->spectrogram_thread = NULL;
    atomic_init(&engine->spectrogram_running, false);
    ringbuf_reset(&engine->spectrogram_queue);
    engine->spectrogram_update_active = false;
    engine->spectrogram_state.head = 0;
    engine->spectrogram_state.count = 0;
    engine->spectrogram_state.bins = ENGINE_SPECTROGRAM_BINS;
    engine->spectrogram_state.last_track = -1;
    engine->spectrogram_state.last_id = 0;
    for (int i = 0; i < ENGINE_SPECTRUM_HISTORY; ++i) {
        for (int b = 0; b < ENGINE_SPECTRUM_BINS; ++b) {
            engine->spectrum_history[i][b] = ENGINE_SPECTRUM_DB_FLOOR;
        }
    }
    for (int i = 0; i < ENGINE_SPECTROGRAM_HISTORY; ++i) {
        for (int b = 0; b < ENGINE_SPECTROGRAM_BINS; ++b) {
            engine->spectrogram_state.history[i][b] = ENGINE_SPECTROGRAM_DB_FLOOR;
        }
    }
    for (int t = 0; t < engine->track_capacity; ++t) {
        for (int b = 0; b < ENGINE_SPECTRUM_BINS; ++b) {
            engine->track_spectra[t * ENGINE_SPECTRUM_BINS + b] = ENGINE_SPECTRUM_DB_FLOOR;
        }
        engine_meter_reset_state(&engine->track_meters[t]);
    }
    engine_meter_reset_state(&engine->master_meter);
    SDL_zero(engine->master_fx_meters);
    SDL_zero(engine->master_meter_snapshots);
    SDL_zero(engine->master_fx_meter_snapshots);
    atomic_init(&engine->meter_snapshot_index, 0);
    engine->active_fx_meter_id = 0;
    engine->active_fx_meter_is_master = true;
    engine->active_fx_meter_track = -1;
    engine->midi_audition_source = engine_instrument_source_create();
    engine_midi_note_list_init(&engine->midi_audition_notes);
    engine->midi_audition_notes.notes = calloc(256, sizeof(EngineMidiNote));
    engine->midi_audition_notes.note_capacity = 256;
    if (!engine->midi_audition_notes.notes ||
        !engine_instrument_source_reserve_notes(engine->midi_audition_source, 256)) {
        engine_destroy(engine);
        return NULL;
    }
    engine->midi_audition_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    engine->midi_audition_params = engine_instrument_default_params(engine->midi_audition_preset);
    engine->midi_audition_idle_frame = 0;
    engine->midi_audition_track_index = -1;

    engine_graph_clear_sources(engine->graph);
    engine_graph_add_source(engine->graph, &engine->tone_ops, engine->tone_source, 1.0f, -1);
    engine_graph_reset(engine_render_source_graph(engine));

    audio_media_cache_init(&engine->media_cache, engine->config.enable_cache_logs);
    engine_add_track(engine);
    FxConfig fxcfg = {.sample_rate = engine->config.sample_rate, .max_block = engine->config.block_size,
                      .max_channels = engine_graph_get_channels(engine->graph)};
    engine->fxm = fxm_create(&fxcfg);
    if (!engine->fxm) { engine_destroy(engine); return NULL; }
    fx_register_builtins_all(engine->fxm);
    if (!engine_request_rebuild_sources(engine)) {
        engine_destroy(engine);
        return NULL;
    }
    return engine;
}

void engine_destroy(Engine* engine) {
    if (!engine) {
        return;
    }
    engine_stop(engine);
    engine_source_plan_shutdown(engine);
    audio_device_close(&engine->device);

    // >>> NEW: destroy effects manager <<<
    if (engine->fxm) {
        fxm_destroy(engine->fxm);
        engine->fxm = NULL;
    }

    if (engine->graph) {
        engine_graph_destroy(engine->graph);
        engine->graph = NULL;
    }
    if (engine->tone_source) {
        engine_tone_source_destroy(engine->tone_source);
        engine->tone_source = NULL;
    }
    if (engine->midi_audition_source) {
        engine_instrument_source_destroy(engine->midi_audition_source);
        engine->midi_audition_source = NULL;
    }
    engine_midi_note_list_free(&engine->midi_audition_notes);
    engine_scope_host_free(engine);
    for (int i = 0; i < engine->track_capacity; ++i) {
        engine_track_clear(engine, &engine->tracks[i]);
    }
    engine_eq_free(&engine->master_eq);
    free(engine->tracks);
    engine->tracks = NULL;
    engine->track_count = 0;
    engine->track_capacity = 0;
    engine_audio_source_clear_all(engine);
    free(engine->audio_sources);
    engine->audio_sources = NULL;
    engine->audio_source_count = 0;
    engine->audio_source_capacity = 0;
    free(engine->track_spectra);
    engine->track_spectra = NULL;
    engine->track_spectrum_capacity = 0;
    free(engine->track_meters);
    engine->track_meters = NULL;
    engine->track_meter_capacity = 0;
    free(engine->track_meter_snapshots);
    engine->track_meter_snapshots = NULL;
    free(engine->track_fx_meters);
    engine->track_fx_meters = NULL;
    engine->track_fx_meter_capacity = 0;
    free(engine->track_fx_meter_snapshots);
    engine->track_fx_meter_snapshots = NULL;
    engine_clip_history_invalidate(engine);
    audio_media_cache_shutdown(&engine->media_cache);
    ringbuf_free(&engine->command_queue);
    audio_queue_free(&engine->output_queue);
    if (engine->fxm_mutex) {
        SDL_DestroyMutex(engine->fxm_mutex);
        engine->fxm_mutex = NULL;
    }
    if (engine->eq_mutex) {
        SDL_DestroyMutex(engine->eq_mutex);
        engine->eq_mutex = NULL;
    }
    if (engine->spectrum_mutex) {
        SDL_DestroyMutex(engine->spectrum_mutex);
        engine->spectrum_mutex = NULL;
    }
    if (engine->spectrogram_mutex) {
        SDL_DestroyMutex(engine->spectrogram_mutex);
        engine->spectrogram_mutex = NULL;
    }
    if (engine->meter_mutex) {
        SDL_DestroyMutex(engine->meter_mutex);
        engine->meter_mutex = NULL;
    }
    ringbuf_free(&engine->spectrum_queue);
    ringbuf_free(&engine->spectrogram_queue);
    free(engine);
}

// Opens the callback endpoint and starts all workers with one rollback path for partial startup.
bool engine_start(Engine* engine) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id) return false;
    if (engine->device_started) return true;
    if (engine->worker_thread || engine->spectrum_thread || engine->spectrogram_thread) engine_stop(engine);
    AudioDeviceSpec want = {.sample_rate = engine->config.sample_rate, .block_size = engine->config.block_size,
                            .channels = engine_graph_get_channels(engine->graph)};
    if (!engine->device.is_open && !audio_device_open(&engine->device, &want, engine_audio_callback, engine)) goto fail;
    size_t target = engine_output_target_frames(want.block_size, engine->device.spec.block_size,
                                               engine->config.output_queue_blocks);
    if (!target) goto fail;
    size_t capacity = target > (size_t)want.block_size * 32 ? target : (size_t)want.block_size * 32;
    if (!engine->output_queue.buffer.data || engine->output_queue.channels != want.channels ||
        engine->output_queue.buffer.capacity / (size_t)engine->output_queue.frame_stride_bytes < capacity) {
        audio_queue_free(&engine->output_queue);
        if (!audio_queue_init(&engine->output_queue, want.channels, capacity)) goto fail;
    }
    atomic_store_explicit(&engine->output_target_frames, target, memory_order_relaxed);
    // Callback buffer size may differ; the DSP block size and project rate remain stable.
    size_t samples = (size_t)engine->config.block_size * (size_t)want.channels;
    if (!engine->render_buffer) engine->render_buffer = calloc(samples, sizeof(float));
    if (!engine->render_track_buffer) engine->render_track_buffer = calloc(samples, sizeof(float));
    if (!engine->render_buffer || !engine->render_track_buffer) goto fail;
    engine_cancel_commands(engine);
    ringbuf_reset(&engine->command_queue);
    ringbuf_reset(&engine->spectrum_queue);
    ringbuf_reset(&engine->spectrogram_queue);
    engine->spectrum_stream.filled = 0;
    engine->spectrogram_stream.filled = 0;
    if (!engine_request_rebuild_sources(engine)) goto fail;
    engine_clock_discontinuity(engine, engine->transport_frame, false, atomic_load(&engine->transport_playing));
    atomic_store(&engine->diag_worker_priority_status, 0);
    atomic_store(&engine->worker_running, true);
    engine->worker_thread = SDL_CreateThread(engine_worker_main, "engine_worker", engine);
    if (!engine->worker_thread) goto fail;
    atomic_store(&engine->spectrum_running, true);
    engine->spectrum_thread = SDL_CreateThread(engine_spectrum_thread_main, "engine_spectrum", engine);
    if (!engine->spectrum_thread) goto fail;
    atomic_store(&engine->spectrogram_running, true);
    engine->spectrogram_thread = SDL_CreateThread(engine_spectrogram_thread_main, "engine_spectrogram", engine);
    if (!engine->spectrogram_thread) goto fail;
    if (!audio_device_start(&engine->device)) goto fail;
    engine->device_started = true;
    engine_trace(engine, "Audio running: %d Hz, %d channels, DSP block %d, callback block %d",
                 want.sample_rate, want.channels, engine->config.block_size, engine->device.spec.block_size);
    return true;
fail:
    engine_trace(engine, "engine_start: rolling back incomplete startup: %s", SDL_GetError());
    engine_stop(engine);
    audio_device_close(&engine->device);
    return false;
}

// Stops publication before joining consumers and reclaims storage only after every reader exits.
void engine_stop(Engine* engine) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id) return;
    if (engine->device.is_open) audio_device_stop(&engine->device);
    engine->device_started = false;
    atomic_store(&engine->worker_running, false);
    atomic_store(&engine->spectrum_running, false);
    atomic_store(&engine->spectrogram_running, false);
    if (engine->worker_thread) { SDL_WaitThread(engine->worker_thread, NULL); engine->worker_thread = NULL; }
    if (engine->spectrum_thread) { SDL_WaitThread(engine->spectrum_thread, NULL); engine->spectrum_thread = NULL; }
    if (engine->spectrogram_thread) { SDL_WaitThread(engine->spectrogram_thread, NULL); engine->spectrogram_thread = NULL; }
    atomic_store(&engine->worker_thread_id, 0);
    atomic_store(&engine->diag_worker_priority_status, 0);
    engine_cancel_commands(engine);
    engine_source_plan_apply(engine);
    engine_source_plan_collect(engine);
    atomic_store(&engine->transport_playing, false);
    atomic_store(&engine->playback_requested_token, 0);
    atomic_store(&engine->playback_applied_token, 0);
    atomic_store(&engine->playback_safety_token, 0);
    atomic_store(&engine->rebuild_sources_pending, false);
    engine_midi_audition_apply_all_off(engine);
    engine_graph_reset(engine_render_source_graph(engine));
    engine_clock_discontinuity(engine, 0, false, false);
    atomic_store(&engine->transport_requested_serial, 0);
    atomic_store(&engine->transport_applied_serial, 0);
    engine->spectrum_stream.filled = 0;
    engine->spectrogram_stream.filled = 0;
    engine_analysis_invalidate(&engine->spectrum_stream);
    engine_analysis_invalidate(&engine->spectrogram_stream);
    // All queue endpoints are now quiescent, so resetting both indices is safe.
    ringbuf_reset(&engine->output_queue.buffer);
    atomic_store(&engine->output_queue.flush_head, 0);
    free(engine->render_buffer); engine->render_buffer = NULL;
    free(engine->render_track_buffer); engine->render_track_buffer = NULL;
}

const EngineRuntimeConfig* engine_get_config(const Engine* engine) {
    if (!engine) {
        return NULL;
    }
    return &engine->config;
}

bool engine_is_running(const Engine* engine) {
    if (!engine) {
        return false;
    }
    return engine->device_started;
}

size_t engine_get_queued_frames(const Engine* engine) {
    if (!engine) {
        return 0;
    }
    EngineClockSnapshot snapshot;
    return engine_get_clock_snapshot(engine, &snapshot) ? (size_t)snapshot.queued_frames : 0;
}

// Validates tempo on its sole control writer and transfers one complete value to the worker.
bool engine_set_tempo_state(Engine* engine, const TempoState* tempo) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || !tempo ||
        !isfinite(tempo->bpm) || !isfinite(tempo->sample_rate)) return false;
    TempoState accepted = *tempo;
    // Tempo conversions always use the immutable project rate, including after device restart.
    accepted.sample_rate = engine->config.sample_rate;
    tempo_state_clamp(&accepted);
    if (!engine->device_started || !engine->worker_thread) {
        engine->tempo = accepted;
        return true;
    }
    EngineCommand cmd = {.type = ENGINE_CMD_SET_TEMPO, .payload.tempo.tempo = accepted};
    return engine_post_command(engine, &cmd);
}

// Queues an owned graph revision whose source userdata must remain valid until engine shutdown.
bool engine_queue_graph_swap(Engine* engine, EngineGraph* new_graph) {
    if (!engine || !new_graph) {
        return false;
    }
    EngineSourcePlan* plan = engine_source_plan_wrap_graph(engine, new_graph);
    if (!plan) return false;
    EngineCommand cmd = {
        .type = ENGINE_CMD_GRAPH_SWAP,
        .payload.graph_swap.plan = plan,
    };
    if (!engine_post_command(engine, &cmd)) {
        SDL_Log("engine_queue_graph_swap: command queue full");
        engine_source_plan_discard(engine, plan);
        return false;
    }
    return true;
}

void engine_set_logging(Engine* engine, bool engine_logs, bool cache_logs, bool timing_logs) {
    if (!engine) {
        return;
    }
    engine->config.enable_engine_logs = engine_logs;
    engine->config.enable_cache_logs = cache_logs;
    engine->config.enable_timing_logs = timing_logs;
    atomic_store(&engine->runtime_timing_logs, timing_logs);
    atomic_store(&engine->runtime_engine_logs, engine_logs);
    audio_media_cache_set_verbose(&engine->media_cache, cache_logs);
    SDL_Log("engine logging flags: engine=%s cache=%s timing=%s",
            engine_logs ? "on" : "off",
            cache_logs ? "on" : "off",
            timing_logs ? "on" : "off");
}
