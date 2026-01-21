#include "engine/engine_internal.h"

#include "engine/sampler.h"
#include "effects/effects_builtin.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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
    for (int i = 0; i < engine->track_count; ++i) {
        EngineTrack* track = &engine->tracks[i];
        if (!track->active || track->clip_count == 0 || track->muted) {
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
            if (!clip->active || !clip->sampler) {
                continue;
            }
            float clip_gain = clip->gain != 0.0f ? clip->gain : 1.0f;
            engine_graph_add_source(engine->graph, &engine->sampler_ops, clip->sampler, track_gain * clip_gain, i);
        }
    }
    engine_graph_reset(engine->graph);
}

static void engine_process_commands(Engine* engine) {
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
            default:
                break;
        }
    }
}

static int engine_worker_main(void* userdata) {
    Engine* engine = (Engine*)userdata;
    if (!engine) {
        return -1;
    }
    int channels = engine_graph_get_channels(engine->graph);
    if (channels <= 0) {
        channels = engine->output_queue.channels > 0 ? engine->output_queue.channels : 2;
    }
    const int block = engine->config.block_size;
    float* block_buffer = (float*)malloc((size_t)block * (size_t)channels * sizeof(float));
    float* track_buffer = (float*)malloc((size_t)block * (size_t)channels * sizeof(float));
    if (!block_buffer || !track_buffer) {
        return -1;
    }

    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 last_report_counter = SDL_GetPerformanceCounter();
    double accum_ms = 0.0;
    int accum_blocks = 0;

    while (atomic_load_explicit(&engine->worker_running, memory_order_acquire)) {
        engine_process_commands(engine);
        if (!atomic_load_explicit(&engine->transport_playing, memory_order_acquire)) {
            SDL_Delay(2);
            continue;
        }

        if (audio_queue_space_frames(&engine->output_queue) < (size_t)block) {
            SDL_Delay(1);
            continue;
        }

        if (!engine->config.enable_timing_logs) {
            accum_ms = 0.0;
            accum_blocks = 0;
            last_report_counter = SDL_GetPerformanceCounter();
        }

        int graph_channels = engine_graph_get_channels(engine->graph);
        if (graph_channels <= 0) {
            graph_channels = channels;
        }
        if (graph_channels != channels) {
            float* new_buffer = (float*)realloc(block_buffer, (size_t)block * (size_t)graph_channels * sizeof(float));
            if (!new_buffer) {
                break;
            }
            block_buffer = new_buffer;
            channels = graph_channels;
        }

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
            uint64_t current = engine->transport_frame;
            bool loop_active = loop_enabled && loop_end > loop_start;
            bool loop_this_block = false;
            if (loop_active) {
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

            Uint64 start_counter = engine->config.enable_timing_logs ? SDL_GetPerformanceCounter() : 0;

            engine_spectrum_begin_block(engine);
            engine_spectrogram_begin_block(engine);

            // mix tracks with per-track FX
            float* out_ptr = block_buffer + produced * channels;
            engine_mix_tracks(engine, engine->transport_frame, chunk, out_ptr, track_buffer, channels);

            engine->transport_frame += (uint64_t)chunk;
            produced += chunk;
            frames_remaining -= chunk;

            if (engine->config.enable_timing_logs && perf_freq > 0) {
                Uint64 end_counter = SDL_GetPerformanceCounter();
                double elapsed_ms = (double)(end_counter - start_counter) * 1000.0 / (double)perf_freq;
                accum_ms += elapsed_ms;
                accum_blocks += 1;
                if ((end_counter - last_report_counter) >= perf_freq && accum_blocks > 0) {
                    double avg_ms = accum_ms / (double)accum_blocks;
                    size_t queued = audio_queue_available_frames(&engine->output_queue);
                    engine_timing_trace(engine, "worker avg render %.3fms (%d blocks) queue=%zu", avg_ms, accum_blocks, queued);
                    accum_ms = 0.0;
                    accum_blocks = 0;
                    last_report_counter = end_counter;
                }
            }

            if (loop_this_block) {
                uint64_t loop_start_cur = atomic_load_explicit(&engine->loop_start_frame, memory_order_acquire);
                uint64_t loop_end_cur = atomic_load_explicit(&engine->loop_end_frame, memory_order_acquire);
                if (atomic_load_explicit(&engine->loop_enabled, memory_order_acquire) && loop_end_cur > loop_start_cur &&
                    engine->transport_frame >= loop_end_cur) {
                    engine->transport_frame = loop_start_cur;
                    engine_graph_reset(engine->graph);
                }
            }
        }

        // master effects chain over the completed block
        if (engine->fxm) {
            SDL_LockMutex(engine->fxm_mutex);
            fxm_render_master(engine->fxm, block_buffer, block, channels);
            SDL_UnlockMutex(engine->fxm_mutex);
        }

        engine_sanitize_block(block_buffer, (size_t)block * (size_t)channels);
        engine_spectrum_update(engine, block_buffer, block, channels);

        audio_queue_write(&engine->output_queue, block_buffer, (size_t)block);
    }

    free(block_buffer);
    free(track_buffer);
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
    engine->device_started = false;
    engine->worker_thread = NULL;
    atomic_init(&engine->worker_running, false);
    atomic_init(&engine->transport_playing, false);
    atomic_init(&engine->loop_enabled, false);
    atomic_init(&engine->loop_start_frame, 0);
    atomic_init(&engine->loop_end_frame, 0);
    atomic_init(&engine->spectrum_enabled, false);
    atomic_init(&engine->spectrum_view, ENGINE_SPECTRUM_VIEW_MASTER);
    atomic_init(&engine->spectrum_target_track, -1);
    atomic_init(&engine->spectrogram_enabled, false);
    atomic_init(&engine->spectrogram_target_track, -1);
    atomic_init(&engine->spectrogram_target_id, 0);
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
    engine->track_fx_meter_capacity = engine->track_capacity;
    engine->track_fx_meters = (EngineFxMeterBank*)calloc((size_t)engine->track_capacity, sizeof(EngineFxMeterBank));
    if (!engine->track_fx_meters) {
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
    engine->transport_frame = 0;
    engine->next_clip_id = 1;
    engine->spectrum_history_index = 0;
    engine->spectrum_bins = ENGINE_SPECTRUM_BINS;
    engine->spectrum_block_counter = 0;
    engine->spectrum_block_skip = 4;
    engine->spectrum_update_active = false;
    engine->spectrum_update_master = false;
    engine->spectrum_update_track = false;
    engine->spectrum_active_track = -1;
    engine->spectrum_thread = NULL;
    atomic_init(&engine->spectrum_running, false);
    ringbuf_reset(&engine->spectrum_queue);
    engine->spectrum_window_index = 0;
    engine->spectrum_window_filled = 0;
    engine->spectrum_avg_index = 0;
    engine->spectrum_avg_count = 0;
    engine->spectrum_last_view = -1;
    engine->spectrum_last_track = -1;
    engine->spectrogram_thread = NULL;
    atomic_init(&engine->spectrogram_running, false);
    ringbuf_reset(&engine->spectrogram_queue);
    engine->spectrogram_block_counter = 0;
    engine->spectrogram_block_skip = 4;
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
    engine->active_fx_meter_id = 0;
    engine->active_fx_meter_is_master = true;
    engine->active_fx_meter_track = -1;

    engine_graph_clear_sources(engine->graph);
    engine_graph_add_source(engine->graph, &engine->tone_ops, engine->tone_source, 1.0f, -1);
    engine_graph_reset(engine->graph);

    audio_media_cache_init(&engine->media_cache, engine->config.enable_cache_logs);
    engine_add_track(engine);
    return engine;
}

void engine_destroy(Engine* engine) {
    if (!engine) {
        return;
    }
    engine_stop(engine);
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
    for (int i = 0; i < engine->track_capacity; ++i) {
        engine_track_clear(engine, &engine->tracks[i]);
    }
    engine_eq_free(&engine->master_eq);
    free(engine->tracks);
    engine->tracks = NULL;
    engine->track_count = 0;
    engine->track_capacity = 0;
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
    free(engine->track_fx_meters);
    engine->track_fx_meters = NULL;
    engine->track_fx_meter_capacity = 0;
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

bool engine_start(Engine* engine) {
    if (!engine) {
        return false;
    }

    AudioDeviceSpec want = {
        .sample_rate = engine->config.sample_rate,
        .block_size = engine->config.block_size,
        .channels = 2
    };
    EngineFxSnapshot fx_snap = {0};
    bool had_fxm = engine_fx_snapshot_all(engine, &fx_snap);

    if (!engine->device.is_open) {
        if (!audio_device_open(&engine->device, &want, engine_audio_callback, engine)) {
            SDL_Log("engine_start: failed to open audio device");
            free(fx_snap.tracks);
            return false;
        }
    }
    const AudioDeviceSpec* have = &engine->device.spec;
    engine->config.sample_rate = have->sample_rate;
    engine->config.block_size = have->block_size;

    size_t capacity_frames = (size_t)engine->config.block_size * 32;
    if (engine->output_queue.channels != have->channels || engine->output_queue.buffer.data == NULL) {
        audio_queue_free(&engine->output_queue);
        if (!audio_queue_init(&engine->output_queue, have->channels, capacity_frames)) {
            SDL_Log("engine_start: failed to init audio queue");
            return false;
        }
    } else {
        audio_queue_clear(&engine->output_queue);
    }

    ringbuf_reset(&engine->command_queue);
    ringbuf_reset(&engine->spectrum_queue);
    ringbuf_reset(&engine->spectrogram_queue);
    if (engine_graph_configure(engine->graph, have->sample_rate, have->channels, engine->config.block_size) != 0) {
        SDL_Log("engine_start: failed to configure graph");
        return false;
    }
    engine_rebuild_sources(engine);

    FxConfig fxcfg = {
        .sample_rate  = have->sample_rate,
        .max_block    = engine->config.block_size,
        .max_channels = have->channels,
        .pool         = NULL, // not needed for interleaved v1
    };

    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        fxm_destroy(engine->fxm);
        engine->fxm = NULL;
    }

    EffectsManager* new_fxm = fxm_create(&fxcfg);
    if (!new_fxm) {
        SDL_UnlockMutex(engine->fxm_mutex);
        SDL_Log("engine_start: failed to create effects manager");
        return false;
    }

    fx_register_builtins_all(new_fxm);
    engine->fxm = new_fxm;

    if (had_fxm) {
        engine_fx_restore_all(engine, &fx_snap);
    }
    free(fx_snap.tracks);

    SDL_UnlockMutex(engine->fxm_mutex);

    engine_register_fx_meter_tap(engine);
    engine_fx_meter_clear_all(engine);

    engine->transport_frame = 0;

    atomic_store_explicit(&engine->worker_running, true, memory_order_release);
    engine->worker_thread = SDL_CreateThread(engine_worker_main, "engine_worker", engine);
    if (!engine->worker_thread) {
        SDL_Log("engine_start: failed to create worker thread: %s", SDL_GetError());
        atomic_store_explicit(&engine->worker_running, false, memory_order_release);
        return false;
    }

    atomic_store_explicit(&engine->spectrum_running, true, memory_order_release);
    engine->spectrum_thread = SDL_CreateThread(engine_spectrum_thread_main, "engine_spectrum", engine);
    if (!engine->spectrum_thread) {
        SDL_Log("engine_start: failed to create spectrum thread: %s", SDL_GetError());
        atomic_store_explicit(&engine->spectrum_running, false, memory_order_release);
        atomic_store_explicit(&engine->worker_running, false, memory_order_release);
        SDL_WaitThread(engine->worker_thread, NULL);
        engine->worker_thread = NULL;
        return false;
    }

    atomic_store_explicit(&engine->spectrogram_running, true, memory_order_release);
    engine->spectrogram_thread = SDL_CreateThread(engine_spectrogram_thread_main, "engine_spectrogram", engine);
    if (!engine->spectrogram_thread) {
        SDL_Log("engine_start: failed to create spectrogram thread: %s", SDL_GetError());
        atomic_store_explicit(&engine->spectrogram_running, false, memory_order_release);
        atomic_store_explicit(&engine->spectrum_running, false, memory_order_release);
        SDL_WaitThread(engine->spectrum_thread, NULL);
        engine->spectrum_thread = NULL;
        atomic_store_explicit(&engine->worker_running, false, memory_order_release);
        SDL_WaitThread(engine->worker_thread, NULL);
        engine->worker_thread = NULL;
        return false;
    }

    if (!audio_device_start(&engine->device)) {
        SDL_Log("engine_start: failed to start audio device");
        atomic_store_explicit(&engine->spectrogram_running, false, memory_order_release);
        SDL_WaitThread(engine->spectrogram_thread, NULL);
        engine->spectrogram_thread = NULL;
        atomic_store_explicit(&engine->spectrum_running, false, memory_order_release);
        SDL_WaitThread(engine->spectrum_thread, NULL);
        engine->spectrum_thread = NULL;
        atomic_store_explicit(&engine->worker_running, false, memory_order_release);
        SDL_WaitThread(engine->worker_thread, NULL);
        engine->worker_thread = NULL;
        return false;
    }
    engine->device_started = true;

    SDL_Log("Audio device running: %d Hz, %d channels, block size %d",
            have->sample_rate, have->channels, have->block_size);

    return true;
}

void engine_stop(Engine* engine) {
    if (!engine) {
        return;
    }
    if (engine->device_started) {
        audio_device_stop(&engine->device);
        engine->device_started = false;
    }
    if (engine->spectrum_thread) {
        atomic_store_explicit(&engine->spectrum_running, false, memory_order_release);
        SDL_WaitThread(engine->spectrum_thread, NULL);
        engine->spectrum_thread = NULL;
    }
    if (engine->spectrogram_thread) {
        atomic_store_explicit(&engine->spectrogram_running, false, memory_order_release);
        SDL_WaitThread(engine->spectrogram_thread, NULL);
        engine->spectrogram_thread = NULL;
    }
    if (engine->worker_thread) {
        atomic_store_explicit(&engine->worker_running, false, memory_order_release);
        SDL_WaitThread(engine->worker_thread, NULL);
        engine->worker_thread = NULL;
    }
    atomic_store_explicit(&engine->transport_playing, false, memory_order_release);
    engine_graph_reset(engine->graph);
    engine->transport_frame = 0;
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
    return audio_queue_available_frames(&engine->output_queue);
}

bool engine_queue_graph_swap(Engine* engine, EngineGraph* new_graph) {
    if (!engine || !new_graph) {
        return false;
    }
    EngineCommand cmd = {
        .type = ENGINE_CMD_GRAPH_SWAP,
        .payload.graph_swap.new_graph = new_graph,
    };
    if (!engine_post_command(engine, &cmd)) {
        SDL_Log("engine_queue_graph_swap: command queue full");
        engine_graph_destroy(new_graph);
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
    audio_media_cache_set_verbose(&engine->media_cache, cache_logs);
    SDL_Log("engine logging flags: engine=%s cache=%s timing=%s",
            engine_logs ? "on" : "off",
            cache_logs ? "on" : "off",
            timing_logs ? "on" : "off");
}
