#include "engine/engine_internal.h"

#include <math.h>
#include <string.h>

#define ENGINE_SPECTRUM_FFT_SIZE 1024
#define ENGINE_SPECTRUM_UPDATE_INTERVAL 4
#define ENGINE_SPECTRUM_TILT_EXP 0.35f

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void fill_mono(const float* interleaved, float* mono, int frames, int channels) {
    if (!interleaved || !mono || frames <= 0 || channels <= 0) {
        return;
    }
    if (channels == 1) {
        memcpy(mono, interleaved, (size_t)frames * sizeof(float));
        return;
    }
    for (int i = 0; i < frames; ++i) {
        int base = i * channels;
        float l = interleaved[base];
        float r = interleaved[base + 1];
        mono[i] = 0.5f * (l + r);
    }
}

static void compute_spectrum(const float* mono,
                             int frames,
                             int sample_rate,
                             float* out_bins,
                             int bin_count) {
    if (!mono || frames <= 0 || sample_rate <= 0 || !out_bins || bin_count <= 0) {
        return;
    }
    int n = frames;
    if (n > ENGINE_SPECTRUM_FFT_SIZE) {
        n = ENGINE_SPECTRUM_FFT_SIZE;
    }
    float min_hz = ENGINE_SPECTRUM_MIN_HZ;
    float max_hz = ENGINE_SPECTRUM_MAX_HZ;
    float nyquist = (float)sample_rate * 0.5f;
    if (max_hz > nyquist) {
        max_hz = nyquist;
    }
    if (min_hz < 1.0f) {
        min_hz = 1.0f;
    }
    float ratio = powf(max_hz / min_hz, 1.0f / (float)(bin_count - 1));

    for (int b = 0; b < bin_count; ++b) {
        float freq = min_hz * powf(ratio, (float)b);
        float omega = 2.0f * (float)M_PI * freq / (float)sample_rate;
        float re = 0.0f;
        float im = 0.0f;
        for (int i = 0; i < n; ++i) {
            float window = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(n - 1)));
            float sample = mono[i] * window;
            float phase = omega * (float)i;
            re += sample * cosf(phase);
            im -= sample * sinf(phase);
        }
        float mag = sqrtf(re * re + im * im) / (float)n;
        float tilt = powf(freq / min_hz, ENGINE_SPECTRUM_TILT_EXP);
        mag *= tilt;
        float db = 20.0f * log10f(mag + 1e-6f);
        out_bins[b] = clampf(db, ENGINE_SPECTRUM_DB_FLOOR, ENGINE_SPECTRUM_DB_CEIL);
    }
}

bool engine_spectrum_begin_block(Engine* engine) {
    if (!engine) {
        return false;
    }
    if (!atomic_load_explicit(&engine->spectrum_enabled, memory_order_acquire)) {
        engine->spectrum_update_active = false;
        engine->spectrum_update_master = false;
        engine->spectrum_update_track = false;
        return false;
    }
    int view = atomic_load_explicit(&engine->spectrum_view, memory_order_acquire);
    int track = atomic_load_explicit(&engine->spectrum_target_track, memory_order_acquire);
    engine->spectrum_update_master = (view == ENGINE_SPECTRUM_VIEW_MASTER);
    engine->spectrum_update_track = (view == ENGINE_SPECTRUM_VIEW_TRACK);
    engine->spectrum_active_track = track;
    if (engine->spectrum_update_track) {
        if (track < 0 || track >= engine->track_count) {
            engine->spectrum_update_track = false;
        }
    }
    if (!engine->spectrum_update_master && !engine->spectrum_update_track) {
        engine->spectrum_update_active = false;
        return false;
    }
    engine->spectrum_block_skip = ENGINE_SPECTRUM_UPDATE_INTERVAL;
    if (engine->spectrum_block_skip > 0) {
        engine->spectrum_block_counter += 1;
        if (engine->spectrum_block_counter < engine->spectrum_block_skip) {
            engine->spectrum_update_active = false;
            return false;
        }
        engine->spectrum_block_counter = 0;
    }
    engine->spectrum_update_active = true;
    return true;
}

void engine_spectrum_update(Engine* engine, const float* interleaved, int frames, int channels) {
    if (!engine || !interleaved || frames <= 0 || channels <= 0) {
        return;
    }
    if (!engine->spectrum_update_active || !engine->spectrum_update_master) {
        return;
    }

    float mono[ENGINE_SPECTRUM_FFT_SIZE];
    memset(mono, 0, sizeof(mono));
    int n = frames;
    if (n > ENGINE_SPECTRUM_FFT_SIZE) {
        n = ENGINE_SPECTRUM_FFT_SIZE;
    }
    fill_mono(interleaved, mono, n, channels);

    float bins[ENGINE_SPECTRUM_BINS];
    compute_spectrum(mono, n, engine->config.sample_rate, bins, ENGINE_SPECTRUM_BINS);

    if (engine->spectrum_mutex) {
        SDL_LockMutex(engine->spectrum_mutex);
        int next = (engine->spectrum_history_index + 1) % ENGINE_SPECTRUM_HISTORY;
        engine->spectrum_history_index = next;
        memcpy(engine->spectrum_history[next], bins, sizeof(bins));
        engine->spectrum_bins = ENGINE_SPECTRUM_BINS;
        SDL_UnlockMutex(engine->spectrum_mutex);
    }
    engine->spectrum_update_active = false;
}

int engine_get_spectrum_snapshot(const Engine* engine, float* out_bins, int max_bins) {
    if (!engine || !out_bins || max_bins <= 0) {
        return 0;
    }
    if (!engine->spectrum_mutex) {
        return 0;
    }
    SDL_LockMutex(engine->spectrum_mutex);
    int count = engine->spectrum_bins;
    if (count > max_bins) {
        count = max_bins;
    }
    if (count > 0) {
        int idx = engine->spectrum_history_index;
        memcpy(out_bins, engine->spectrum_history[idx], (size_t)count * sizeof(float));
    }
    SDL_UnlockMutex(engine->spectrum_mutex);
    return count;
}

void engine_spectrum_update_track(Engine* engine, int track_index, const float* interleaved, int frames, int channels) {
    if (!engine || !interleaved || frames <= 0 || channels <= 0) {
        return;
    }
    if (!engine->spectrum_update_active || !engine->spectrum_update_track) {
        return;
    }
    if (!engine->track_spectra || track_index < 0 || track_index >= engine->track_count) {
        return;
    }
    if (track_index != engine->spectrum_active_track) {
        return;
    }
    float mono[ENGINE_SPECTRUM_FFT_SIZE];
    memset(mono, 0, sizeof(mono));
    int n = frames;
    if (n > ENGINE_SPECTRUM_FFT_SIZE) {
        n = ENGINE_SPECTRUM_FFT_SIZE;
    }
    fill_mono(interleaved, mono, n, channels);

    float bins[ENGINE_SPECTRUM_BINS];
    compute_spectrum(mono, n, engine->config.sample_rate, bins, ENGINE_SPECTRUM_BINS);

    if (engine->spectrum_mutex) {
        SDL_LockMutex(engine->spectrum_mutex);
        memcpy(&engine->track_spectra[track_index * ENGINE_SPECTRUM_BINS], bins, sizeof(bins));
        SDL_UnlockMutex(engine->spectrum_mutex);
    }
}

int engine_get_track_spectrum_snapshot(const Engine* engine, int track_index, float* out_bins, int max_bins) {
    if (!engine || !out_bins || max_bins <= 0) {
        return 0;
    }
    if (!engine->spectrum_mutex || !engine->track_spectra) {
        return 0;
    }
    if (track_index < 0 || track_index >= engine->track_count) {
        return 0;
    }
    SDL_LockMutex(engine->spectrum_mutex);
    int count = engine->spectrum_bins;
    if (count > max_bins) {
        count = max_bins;
    }
    if (count > 0) {
        memcpy(out_bins,
               &engine->track_spectra[track_index * ENGINE_SPECTRUM_BINS],
               (size_t)count * sizeof(float));
    }
    SDL_UnlockMutex(engine->spectrum_mutex);
    return count;
}

void engine_set_spectrum_target(Engine* engine, EngineSpectrumView view, int track_index, bool enabled) {
    if (!engine) {
        return;
    }
    atomic_store_explicit(&engine->spectrum_enabled, enabled, memory_order_release);
    atomic_store_explicit(&engine->spectrum_view, (int)view, memory_order_release);
    atomic_store_explicit(&engine->spectrum_target_track, track_index, memory_order_release);
}
