#include "engine/engine_internal.h"

#include "audio/wav_writer.h"
#include "core_time.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool engine_load_wav(Engine* engine, const char* path) {
    if (!engine || !path) {
        return false;
    }
    return engine_add_clip(engine, path, 0);
}

// Restores engine running and transport state after offline rendering.
static bool restore_engine_after_bounce(Engine* engine, bool was_running, bool was_playing, uint64_t prev_transport) {
    if (!was_running) {
        return true;
    }
    if (!engine_start(engine)) {
        return false;
    }
    engine_transport_seek(engine, prev_transport);
    if (was_playing) {
        engine_transport_play(engine);
    }
    return true;
}

void engine_bounce_buffer_free(EngineBounceBuffer* buffer) {
    if (!buffer) {
        return;
    }
    free(buffer->data);
    buffer->data = NULL;
    buffer->frame_count = 0;
    buffer->channels = 0;
    buffer->sample_rate = 0;
}

bool engine_bounce_range_to_buffer(Engine* engine,
                                   uint64_t start_frame,
                                   uint64_t end_frame,
                                   void (*progress_cb)(uint64_t done_frames, uint64_t total_frames, void* user),
                                   void* user,
                                   EngineBounceBuffer* out_buffer) {
    if (!engine || !out_buffer || end_frame <= start_frame) {
        return false;
    }
    out_buffer->data = NULL;
    out_buffer->frame_count = 0;
    out_buffer->channels = 0;
    out_buffer->sample_rate = 0;

    int channels = engine_graph_get_channels(engine->graph);
    int sample_rate = engine->config.sample_rate;
    if (channels <= 0 || sample_rate <= 0) {
        return false;
    }

    uint64_t total_frames = end_frame - start_frame;
    if (total_frames == 0) {
        return false;
    }

    bool was_running = engine_is_running(engine);
    bool was_playing = engine_transport_is_playing(engine);
    uint64_t prev_transport = engine_get_transport_frame(engine);

    if (was_running) {
        engine_transport_stop(engine);
        engine_stop(engine);
    }

    engine_request_rebuild_sources(engine);
    engine_graph_reset(engine->graph);

    int block = engine->config.block_size > 0 ? engine->config.block_size : 512;
    int offline_block = block;
    float* buffer = (float*)calloc((size_t)total_frames * (size_t)channels, sizeof(float));
    float* track_buffer = (float*)calloc((size_t)offline_block * (size_t)channels, sizeof(float));
    if (!buffer || !track_buffer) {
        free(buffer);
        free(track_buffer);
        restore_engine_after_bounce(engine, was_running, was_playing, prev_transport);
        return false;
    }

    uint64_t rendered = 0;
    while (rendered < total_frames) {
        uint64_t remaining = total_frames - rendered;
        int chunk = (int)(remaining > (uint64_t)offline_block ? (uint64_t)offline_block : remaining);
        float* out = buffer + rendered * (uint64_t)channels;

        engine_mix_tracks(engine, start_frame + rendered, chunk, out, track_buffer, channels);

        rendered += (uint64_t)chunk;
        if (progress_cb) {
            progress_cb(rendered, total_frames, user);
        }
    }

    // Normalize to prevent clipping in downstream exports.
    float peak = 0.0f;
    size_t total_samples = (size_t)total_frames * (size_t)channels;
    for (size_t i = 0; i < total_samples; ++i) {
        float a = fabsf(buffer[i]);
        if (a > peak) {
            peak = a;
        }
    }
    float norm = (peak > 1.0f) ? (1.0f / peak) : 1.0f;
    if (norm < 1.0f) {
        for (size_t i = 0; i < total_samples; ++i) {
            buffer[i] *= norm;
        }
    }

    free(track_buffer);

    if (!restore_engine_after_bounce(engine, was_running, was_playing, prev_transport)) {
        free(buffer);
        return false;
    }

    out_buffer->data = buffer;
    out_buffer->frame_count = total_frames;
    out_buffer->channels = channels;
    out_buffer->sample_rate = sample_rate;
    return true;
}

bool engine_bounce_range(Engine* engine,
                         uint64_t start_frame,
                         uint64_t end_frame,
                         const char* out_path,
                         void (*progress_cb)(uint64_t done_frames, uint64_t total_frames, void* user),
                         void* user) {
    if (!engine || !out_path || end_frame <= start_frame) {
        return false;
    }
    EngineBounceBuffer bounce = {0};
    if (!engine_bounce_range_to_buffer(engine, start_frame, end_frame, progress_cb, user, &bounce)) {
        return false;
    }

    // Write a float WAV alongside the PCM16 for comparison.
    char float_path[512];
    snprintf(float_path, sizeof(float_path), "%s.f32.wav", out_path);

    bool ok_float = wav_write_f32(float_path,
                                  bounce.data,
                                  bounce.frame_count,
                                  bounce.channels,
                                  bounce.sample_rate);
    uint32_t dither_seed = (uint32_t)(core_time_now_ns() & 0xffffffffu);
    bool ok = wav_write_pcm16_dithered(out_path,
                                       bounce.data,
                                       bounce.frame_count,
                                       bounce.channels,
                                       bounce.sample_rate,
                                       dither_seed);
    (void)ok_float;
    engine_bounce_buffer_free(&bounce);
    return ok;
}
