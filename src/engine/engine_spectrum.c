#include "engine/engine_internal.h"

#include <math.h>
#include <string.h>

#define ENGINE_SPECTRUM_UPDATE_INTERVAL 4
#define ENGINE_SPECTRUM_TILT_EXP 0.0f

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float a_weighting_db(float freq) {
    if (freq <= 0.0f) {
        return -80.0f;
    }
    float f2 = freq * freq;
    float ra_num = (12200.0f * 12200.0f) * f2 * f2;
    float ra_den = (f2 + 20.6f * 20.6f) *
                   sqrtf((f2 + 107.7f * 107.7f) * (f2 + 737.9f * 737.9f)) *
                   (f2 + 12200.0f * 12200.0f);
    if (ra_den <= 0.0f) {
        return -80.0f;
    }
    float ra = ra_num / ra_den;
    return 2.0f + 20.0f * log10f(ra);
}

static void spectrum_queue_discard(Engine* engine, size_t bytes) {
    if (!engine || bytes == 0) {
        return;
    }
    uint8_t scratch[256];
    size_t remaining = bytes;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(scratch) ? remaining : sizeof(scratch);
        size_t read = ringbuf_read(&engine->spectrum_queue, scratch, chunk);
        if (read == 0) {
            break;
        }
        remaining -= read;
    }
}

static void spectrum_queue_write(Engine* engine, const float* mono, int frames) {
    if (!engine || !mono || frames <= 0) {
        return;
    }
    size_t bytes = (size_t)frames * sizeof(float);
    size_t avail = ringbuf_available_write(&engine->spectrum_queue);
    if (avail < bytes) {
        size_t need = bytes - avail;
        spectrum_queue_discard(engine, need);
    }
    ringbuf_write(&engine->spectrum_queue, mono, bytes);
}

static void spectrum_avg_reset(Engine* engine) {
    if (!engine) {
        return;
    }
    engine->spectrum_avg_index = 0;
    engine->spectrum_avg_count = 0;
}

static void spectrum_ring_reset(Engine* engine) {
    if (!engine) {
        return;
    }
    engine->spectrum_window_index = 0;
    engine->spectrum_window_filled = 0;
    spectrum_avg_reset(engine);
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
        float db = 20.0f * log10f(mag + 1e-6f) + a_weighting_db(freq);
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
    if (engine->spectrum_last_view != view || engine->spectrum_last_track != track) {
        spectrum_ring_reset(engine);
        ringbuf_reset(&engine->spectrum_queue);
        engine->spectrum_last_view = view;
        engine->spectrum_last_track = track;
    }
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
    int n = frames;
    if (n > ENGINE_SPECTRUM_FFT_SIZE) {
        n = ENGINE_SPECTRUM_FFT_SIZE;
    }
    fill_mono(interleaved, mono, n, channels);
    spectrum_queue_write(engine, mono, n);
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
    int n = frames;
    if (n > ENGINE_SPECTRUM_FFT_SIZE) {
        n = ENGINE_SPECTRUM_FFT_SIZE;
    }
    fill_mono(interleaved, mono, n, channels);
    spectrum_queue_write(engine, mono, n);
}

int engine_spectrum_thread_main(void* userdata) {
    Engine* engine = (Engine*)userdata;
    if (!engine) {
        return -1;
    }
    int hop = engine->config.block_size;
    if (hop < 64) hop = 64;
    if (hop > ENGINE_SPECTRUM_FFT_SIZE) hop = ENGINE_SPECTRUM_FFT_SIZE;

    float window[ENGINE_SPECTRUM_FFT_SIZE];
    int filled = 0;
    float hop_buf[ENGINE_SPECTRUM_FFT_SIZE];

    while (atomic_load_explicit(&engine->spectrum_running, memory_order_acquire)) {
        if (!atomic_load_explicit(&engine->spectrum_enabled, memory_order_acquire)) {
            ringbuf_reset(&engine->spectrum_queue);
            spectrum_ring_reset(engine);
            filled = 0;
            SDL_Delay(4);
            continue;
        }

        int view = atomic_load_explicit(&engine->spectrum_view, memory_order_acquire);
        int track = atomic_load_explicit(&engine->spectrum_target_track, memory_order_acquire);
        if (engine->spectrum_last_view != view || engine->spectrum_last_track != track) {
            ringbuf_reset(&engine->spectrum_queue);
            spectrum_ring_reset(engine);
            filled = 0;
            engine->spectrum_last_view = view;
            engine->spectrum_last_track = track;
        }

        size_t avail = ringbuf_available_read(&engine->spectrum_queue);
        size_t hop_bytes = (size_t)hop * sizeof(float);
        if (avail < hop_bytes) {
            SDL_Delay(1);
            continue;
        }
        size_t read_bytes = ringbuf_read(&engine->spectrum_queue, hop_buf, hop_bytes);
        if (read_bytes < hop_bytes) {
            SDL_Delay(1);
            continue;
        }
        int got_samples = (int)(read_bytes / sizeof(float));
        if (got_samples <= 0) {
            SDL_Delay(1);
            continue;
        }

        if (filled < ENGINE_SPECTRUM_FFT_SIZE) {
            int to_copy = ENGINE_SPECTRUM_FFT_SIZE - filled;
            if (to_copy > got_samples) {
                to_copy = got_samples;
            }
            memcpy(window + filled, hop_buf, (size_t)to_copy * sizeof(float));
            filled += to_copy;
            if (filled < ENGINE_SPECTRUM_FFT_SIZE) {
                continue;
            }
            if (got_samples > to_copy) {
                int remaining = got_samples - to_copy;
                if (remaining >= ENGINE_SPECTRUM_FFT_SIZE) {
                    memcpy(window,
                           hop_buf + got_samples - ENGINE_SPECTRUM_FFT_SIZE,
                           ENGINE_SPECTRUM_FFT_SIZE * sizeof(float));
                } else {
                    memmove(window,
                            window + remaining,
                            (size_t)(ENGINE_SPECTRUM_FFT_SIZE - remaining) * sizeof(float));
                    memcpy(window + ENGINE_SPECTRUM_FFT_SIZE - remaining,
                           hop_buf + to_copy,
                           (size_t)remaining * sizeof(float));
                }
                filled = ENGINE_SPECTRUM_FFT_SIZE;
            }
        } else {
            int shift = got_samples;
            if (shift >= ENGINE_SPECTRUM_FFT_SIZE) {
                memcpy(window,
                       hop_buf + got_samples - ENGINE_SPECTRUM_FFT_SIZE,
                       ENGINE_SPECTRUM_FFT_SIZE * sizeof(float));
            } else {
                memmove(window,
                        window + shift,
                        (size_t)(ENGINE_SPECTRUM_FFT_SIZE - shift) * sizeof(float));
                memcpy(window + ENGINE_SPECTRUM_FFT_SIZE - shift,
                       hop_buf,
                       (size_t)shift * sizeof(float));
            }
        }

        if (view == ENGINE_SPECTRUM_VIEW_TRACK &&
            (track < 0 || track >= engine->track_count)) {
            SDL_Delay(2);
            continue;
        }

        float bins[ENGINE_SPECTRUM_BINS];
        compute_spectrum(window, ENGINE_SPECTRUM_FFT_SIZE, engine->config.sample_rate, bins, ENGINE_SPECTRUM_BINS);
        for (int b = 0; b < ENGINE_SPECTRUM_BINS; ++b) {
            engine->spectrum_avg_history[engine->spectrum_avg_index][b] = bins[b];
        }
        engine->spectrum_avg_index = (engine->spectrum_avg_index + 1) % ENGINE_SPECTRUM_AVG_FRAMES;
        if (engine->spectrum_avg_count < ENGINE_SPECTRUM_AVG_FRAMES) {
            engine->spectrum_avg_count += 1;
        }
        float avg_bins[ENGINE_SPECTRUM_BINS];
        for (int b = 0; b < ENGINE_SPECTRUM_BINS; ++b) {
            float sum = 0.0f;
            for (int i = 0; i < engine->spectrum_avg_count; ++i) {
                sum += engine->spectrum_avg_history[i][b];
            }
            avg_bins[b] = sum / (float)engine->spectrum_avg_count;
        }

        if (engine->spectrum_mutex) {
            SDL_LockMutex(engine->spectrum_mutex);
            if (view == ENGINE_SPECTRUM_VIEW_TRACK) {
                memcpy(&engine->track_spectra[track * ENGINE_SPECTRUM_BINS], avg_bins, sizeof(avg_bins));
            } else {
                int next = (engine->spectrum_history_index + 1) % ENGINE_SPECTRUM_HISTORY;
                engine->spectrum_history_index = next;
                memcpy(engine->spectrum_history[next], avg_bins, sizeof(avg_bins));
                engine->spectrum_bins = ENGINE_SPECTRUM_BINS;
            }
            SDL_UnlockMutex(engine->spectrum_mutex);
        }
    }
    return 0;
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
