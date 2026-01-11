#include "engine/engine_internal.h"

#include "engine/graph.h"

#include <math.h>
#include <string.h>

static inline float sanitize_sample(float v) {
    if (!isfinite(v) || fabsf(v) > 64.0f) {
        return 0.0f;
    }
    if (fabsf(v) < 1e-12f) {
        return 0.0f;
    }
    return v;
}

static void apply_track_pan(const EngineTrack* track, float* buffer, int frames, int channels) {
    if (!track || !buffer || frames <= 0 || channels < 2) {
        return;
    }
    float pan = track->pan;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    if (pan == 0.0f) {
        return;
    }

    float left = 1.0f;
    float right = 1.0f;
    if (pan < 0.0f) {
        right = 1.0f + pan;
    } else {
        left = 1.0f - pan;
    }
    for (int i = 0; i < frames; ++i) {
        int base = i * channels;
        buffer[base] *= left;
        buffer[base + 1] *= right;
    }
}

void engine_sanitize_block(float* buf, size_t samples) {
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < samples; ++i) {
        buf[i] = sanitize_sample(buf[i]);
    }
}

void engine_audio_callback(float* output, int frames, int channels, void* userdata) {
    Engine* engine = (Engine*)userdata;
    if (!output || frames <= 0 || !engine) {
        return;
    }
    size_t grabbed = audio_queue_read(&engine->output_queue, output, (size_t)frames);
    if (grabbed < (size_t)frames) {
        size_t missing = (size_t)frames - grabbed;
        memset(output + grabbed * channels, 0, missing * (size_t)channels * sizeof(float));
    }
}

void engine_mix_tracks(Engine* engine,
                       uint64_t start_frame,
                       int frames,
                       float* out,
                       float* track_buffer,
                       int channels) {
    if (!engine || !out || !track_buffer || frames <= 0 || channels <= 0) {
        return;
    }
    memset(out, 0, (size_t)frames * (size_t)channels * sizeof(float));

    int tcount = engine->track_count;
    for (int t = 0; t < tcount; ++t) {
        memset(track_buffer, 0, (size_t)frames * (size_t)channels * sizeof(float));
        engine_graph_render_track(engine->graph,
                                  track_buffer,
                                  frames,
                                  start_frame,
                                  t);
        if (engine->fxm && engine->fxm_mutex) {
            SDL_LockMutex(engine->fxm_mutex);
            fxm_render_track(engine->fxm, t, track_buffer, frames, channels);
            SDL_UnlockMutex(engine->fxm_mutex);
        }
        engine_eq_process(&engine->tracks[t].track_eq, track_buffer, frames, channels);
        engine_spectrum_update_track(engine, t, track_buffer, frames, channels);
        apply_track_pan(&engine->tracks[t], track_buffer, frames, channels);
        for (int s = 0; s < frames * channels; ++s) {
            out[s] += track_buffer[s];
        }
    }

    engine_eq_process(&engine->master_eq, out, frames, channels);
    if (engine->fxm && engine->fxm_mutex) {
        SDL_LockMutex(engine->fxm_mutex);
        fxm_render_master(engine->fxm, out, frames, channels);
        SDL_UnlockMutex(engine->fxm_mutex);
    }

    engine_sanitize_block(out, (size_t)frames * (size_t)channels);
}
