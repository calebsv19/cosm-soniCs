#include "engine/engine_internal.h"

#include <math.h>
#include <string.h>

#define ENGINE_SPECTROGRAM_UPDATE_INTERVAL 4

// Clamps a value between bounds for stable spectrogram output.
static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Resets the spectrogram history to the configured dB floor.
static void engine_spectrogram_reset_history(Engine* engine) {
    if (!engine) {
        return;
    }
    engine->spectrogram_state.head = 0;
    engine->spectrogram_state.count = 0;
    engine->spectrogram_state.bins = ENGINE_SPECTROGRAM_BINS;
    for (int i = 0; i < ENGINE_SPECTROGRAM_HISTORY; ++i) {
        for (int b = 0; b < ENGINE_SPECTROGRAM_BINS; ++b) {
            engine->spectrogram_state.history[i][b] = ENGINE_SPECTROGRAM_DB_FLOOR;
        }
    }
}

// Clears the spectrogram queue and history for a fresh capture window.
void engine_spectrogram_clear_history(Engine* engine) {
    if (!engine) {
        return;
    }
    ringbuf_reset(&engine->spectrogram_queue);
    if (engine->spectrogram_mutex) {
        SDL_LockMutex(engine->spectrogram_mutex);
        engine_spectrogram_reset_history(engine);
        SDL_UnlockMutex(engine->spectrogram_mutex);
    }
}

// Adds mono samples to the spectrogram ring buffer, discarding old data if needed.
static void spectrogram_queue_write(Engine* engine, const float* mono, int frames) {
    if (!engine || !mono || frames <= 0) {
        return;
    }
    size_t bytes = (size_t)frames * sizeof(float);
    size_t avail = ringbuf_available_write(&engine->spectrogram_queue);
    if (avail < bytes) {
        uint8_t scratch[256];
        size_t need = bytes - avail;
        while (need > 0) {
            size_t chunk = need < sizeof(scratch) ? need : sizeof(scratch);
            size_t read = ringbuf_read(&engine->spectrogram_queue, scratch, chunk);
            if (read == 0) {
                break;
            }
            need -= read;
        }
    }
    ringbuf_write(&engine->spectrogram_queue, mono, bytes);
}

// Mixes interleaved audio into a mono buffer for spectrogram analysis.
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

// Computes log-spaced spectrogram bins from the current window.
static void compute_spectrogram_bins(const float* window,
                                     int sample_rate,
                                     float* out_bins,
                                     int bin_count) {
    if (!window || sample_rate <= 0 || !out_bins || bin_count <= 0) {
        return;
    }
    int n = ENGINE_SPECTROGRAM_FFT_SIZE;
    float min_hz = ENGINE_SPECTROGRAM_MIN_HZ;
    float max_hz = ENGINE_SPECTROGRAM_MAX_HZ;
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
            float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(n - 1)));
            float sample = window[i] * w;
            float phase = omega * (float)i;
            re += sample * cosf(phase);
            im -= sample * sinf(phase);
        }
        float mag = sqrtf(re * re + im * im) / (float)n;
        float db = 20.0f * log10f(mag + 1e-6f);
        out_bins[b] = clampf(db, ENGINE_SPECTROGRAM_DB_FLOOR, ENGINE_SPECTROGRAM_DB_CEIL);
    }
}

// Prepares spectrogram capture for the current audio block.
bool engine_spectrogram_begin_block(Engine* engine) {
    if (!engine) {
        return false;
    }
    if (!atomic_load_explicit(&engine->spectrogram_enabled, memory_order_acquire)) {
        engine->spectrogram_update_active = false;
        return false;
    }
    int track = atomic_load_explicit(&engine->spectrogram_target_track, memory_order_acquire);
    FxInstId id = (FxInstId)atomic_load_explicit(&engine->spectrogram_target_id, memory_order_acquire);
    if (engine->spectrogram_state.last_track != track || engine->spectrogram_state.last_id != id) {
        ringbuf_reset(&engine->spectrogram_queue);
        if (engine->spectrogram_mutex) {
            SDL_LockMutex(engine->spectrogram_mutex);
            engine_spectrogram_reset_history(engine);
            SDL_UnlockMutex(engine->spectrogram_mutex);
        }
        engine->spectrogram_state.last_track = track;
        engine->spectrogram_state.last_id = id;
    }
    if (id == 0) {
        engine->spectrogram_update_active = false;
        return false;
    }
    if (track >= engine->track_count) {
        engine->spectrogram_update_active = false;
        return false;
    }
    engine->spectrogram_block_skip = ENGINE_SPECTROGRAM_UPDATE_INTERVAL;
    if (engine->spectrogram_block_skip > 0) {
        engine->spectrogram_block_counter += 1;
        if (engine->spectrogram_block_counter < engine->spectrogram_block_skip) {
            engine->spectrogram_update_active = false;
            return false;
        }
        engine->spectrogram_block_counter = 0;
    }
    engine->spectrogram_update_active = true;
    return true;
}

// Captures per-FX audio for the active spectrogram meter.
void engine_spectrogram_update_fx(Engine* engine,
                                  bool is_master,
                                  int track_index,
                                  FxInstId id,
                                  const float* interleaved,
                                  int frames,
                                  int channels) {
    if (!engine || !interleaved || frames <= 0 || channels <= 0) {
        return;
    }
    if (!engine->spectrogram_update_active ||
        !atomic_load_explicit(&engine->spectrogram_enabled, memory_order_acquire)) {
        return;
    }
    int target_track = atomic_load_explicit(&engine->spectrogram_target_track, memory_order_acquire);
    FxInstId target_id = (FxInstId)atomic_load_explicit(&engine->spectrogram_target_id, memory_order_acquire);
    bool match = false;
    if (is_master) {
        match = (target_track < 0);
    } else {
        match = (track_index == target_track);
    }
    if (!match || id != target_id) {
        return;
    }
    float mono[ENGINE_SPECTROGRAM_FFT_SIZE];
    int n = frames;
    if (n > ENGINE_SPECTROGRAM_FFT_SIZE) {
        n = ENGINE_SPECTROGRAM_FFT_SIZE;
    }
    fill_mono(interleaved, mono, n, channels);
    spectrogram_queue_write(engine, mono, n);
    engine->spectrogram_update_active = false;
}

// Consumes queued samples and updates the spectrogram history in a background thread.
int engine_spectrogram_thread_main(void* userdata) {
    Engine* engine = (Engine*)userdata;
    if (!engine) {
        return -1;
    }
    int hop = engine->config.block_size;
    if (hop < 64) hop = 64;
    if (hop > ENGINE_SPECTROGRAM_FFT_SIZE) hop = ENGINE_SPECTROGRAM_FFT_SIZE;

    float window[ENGINE_SPECTROGRAM_FFT_SIZE];
    int filled = 0;
    float hop_buf[ENGINE_SPECTROGRAM_FFT_SIZE];

    while (atomic_load_explicit(&engine->spectrogram_running, memory_order_acquire)) {
        if (!atomic_load_explicit(&engine->spectrogram_enabled, memory_order_acquire)) {
            ringbuf_reset(&engine->spectrogram_queue);
            if (engine->spectrogram_mutex) {
                SDL_LockMutex(engine->spectrogram_mutex);
                engine_spectrogram_reset_history(engine);
                SDL_UnlockMutex(engine->spectrogram_mutex);
            }
            filled = 0;
            SDL_Delay(4);
            continue;
        }

        int track = atomic_load_explicit(&engine->spectrogram_target_track, memory_order_acquire);
        FxInstId id = (FxInstId)atomic_load_explicit(&engine->spectrogram_target_id, memory_order_acquire);
        if (engine->spectrogram_state.last_track != track || engine->spectrogram_state.last_id != id) {
            ringbuf_reset(&engine->spectrogram_queue);
            if (engine->spectrogram_mutex) {
                SDL_LockMutex(engine->spectrogram_mutex);
                engine_spectrogram_reset_history(engine);
                SDL_UnlockMutex(engine->spectrogram_mutex);
            }
            filled = 0;
            engine->spectrogram_state.last_track = track;
            engine->spectrogram_state.last_id = id;
        }

        size_t avail = ringbuf_available_read(&engine->spectrogram_queue);
        size_t hop_bytes = (size_t)hop * sizeof(float);
        if (avail < hop_bytes) {
            SDL_Delay(1);
            continue;
        }
        size_t read_bytes = ringbuf_read(&engine->spectrogram_queue, hop_buf, hop_bytes);
        if (read_bytes < hop_bytes) {
            SDL_Delay(1);
            continue;
        }
        int got_samples = (int)(read_bytes / sizeof(float));
        if (got_samples <= 0) {
            SDL_Delay(1);
            continue;
        }

        if (filled < ENGINE_SPECTROGRAM_FFT_SIZE) {
            int to_copy = ENGINE_SPECTROGRAM_FFT_SIZE - filled;
            if (to_copy > got_samples) {
                to_copy = got_samples;
            }
            memcpy(window + filled, hop_buf, (size_t)to_copy * sizeof(float));
            filled += to_copy;
            if (filled < ENGINE_SPECTROGRAM_FFT_SIZE) {
                continue;
            }
            if (got_samples > to_copy) {
                int remaining = got_samples - to_copy;
                if (remaining >= ENGINE_SPECTROGRAM_FFT_SIZE) {
                    memcpy(window,
                           hop_buf + got_samples - ENGINE_SPECTROGRAM_FFT_SIZE,
                           ENGINE_SPECTROGRAM_FFT_SIZE * sizeof(float));
                } else {
                    memmove(window,
                            window + remaining,
                            (size_t)(ENGINE_SPECTROGRAM_FFT_SIZE - remaining) * sizeof(float));
                    memcpy(window + ENGINE_SPECTROGRAM_FFT_SIZE - remaining,
                           hop_buf + to_copy,
                           (size_t)remaining * sizeof(float));
                }
                filled = ENGINE_SPECTROGRAM_FFT_SIZE;
            }
        } else {
            int shift = got_samples;
            if (shift >= ENGINE_SPECTROGRAM_FFT_SIZE) {
                memcpy(window,
                       hop_buf + got_samples - ENGINE_SPECTROGRAM_FFT_SIZE,
                       ENGINE_SPECTROGRAM_FFT_SIZE * sizeof(float));
            } else {
                memmove(window,
                        window + shift,
                        (size_t)(ENGINE_SPECTROGRAM_FFT_SIZE - shift) * sizeof(float));
                memcpy(window + ENGINE_SPECTROGRAM_FFT_SIZE - shift,
                       hop_buf,
                       (size_t)shift * sizeof(float));
            }
        }

        float bins[ENGINE_SPECTROGRAM_BINS];
        compute_spectrogram_bins(window, engine->config.sample_rate, bins, ENGINE_SPECTROGRAM_BINS);

        if (engine->spectrogram_mutex) {
            SDL_LockMutex(engine->spectrogram_mutex);
            int next = (engine->spectrogram_state.head + 1) % ENGINE_SPECTROGRAM_HISTORY;
            engine->spectrogram_state.head = next;
            memcpy(engine->spectrogram_state.history[next], bins, sizeof(bins));
            if (engine->spectrogram_state.count < ENGINE_SPECTROGRAM_HISTORY) {
                engine->spectrogram_state.count += 1;
            }
            SDL_UnlockMutex(engine->spectrogram_mutex);
        }
    }
    return 0;
}

// Copies the latest spectrogram history into a caller-owned buffer (newest-first rows).
bool engine_get_fx_spectrogram_snapshot(const Engine* engine,
                                        EngineSpectrogramSnapshot* out_meta,
                                        float* out_frames,
                                        int max_frames,
                                        int max_bins) {
    if (!engine || !out_meta || !out_frames || max_frames <= 0 || max_bins <= 0) {
        return false;
    }
    if (!engine->spectrogram_mutex) {
        return false;
    }
    SDL_LockMutex(engine->spectrogram_mutex);
    int bins = engine->spectrogram_state.bins;
    int count = engine->spectrogram_state.count;
    int head = engine->spectrogram_state.head;
    if (bins > max_bins) {
        bins = max_bins;
    }
    if (count > max_frames) {
        count = max_frames;
    }
    for (int i = 0; i < count; ++i) {
        int idx = head - i;
        while (idx < 0) idx += ENGINE_SPECTROGRAM_HISTORY;
        idx %= ENGINE_SPECTROGRAM_HISTORY;
        memcpy(&out_frames[i * bins],
               engine->spectrogram_state.history[idx],
               (size_t)bins * sizeof(float));
    }
    SDL_UnlockMutex(engine->spectrogram_mutex);
    out_meta->bins = bins;
    out_meta->frames = count;
    out_meta->db_floor = ENGINE_SPECTROGRAM_DB_FLOOR;
    out_meta->db_ceil = ENGINE_SPECTROGRAM_DB_CEIL;
    return count > 0;
}

// Updates the active spectrogram target used for per-FX capture.
void engine_set_fx_spectrogram_target(Engine* engine, int track_index, FxInstId id, bool enabled) {
    if (!engine) {
        return;
    }
    atomic_store_explicit(&engine->spectrogram_enabled, enabled, memory_order_release);
    atomic_store_explicit(&engine->spectrogram_target_track, track_index, memory_order_release);
    atomic_store_explicit(&engine->spectrogram_target_id, id, memory_order_release);
}
