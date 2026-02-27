#include "engine/engine_internal.h"
#include "core/loop/daw_mainthread_messages.h"

#include <math.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Returns the snapshot buffer index currently visible to UI readers.
static int engine_meter_read_index(const Engine* engine) {
    if (!engine) {
        return 0;
    }
    return atomic_load_explicit(&engine->meter_snapshot_index, memory_order_acquire);
}

// Returns the snapshot buffer index reserved for render-thread writes.
static int engine_meter_write_index(const Engine* engine) {
    return 1 - engine_meter_read_index(engine);
}

// Converts LUFS energy to loudness units using BS.1770 scaling.
static float lufs_energy_to_db(double energy) {
    if (energy <= 1e-12) {
        return -90.0f;
    }
    return (float)(-0.691 + 10.0 * log10(energy));
}

// Designs a K-weighting high-pass biquad at the specified frequency.
static void lufs_biquad_design_highpass(EngineFxLufsBiquad* biquad, float sample_rate, float freq_hz, float q) {
    if (!biquad || sample_rate <= 0.0f || freq_hz <= 0.0f) {
        return;
    }
    float w0 = 2.0f * (float)M_PI * freq_hz / sample_rate;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * q);
    float b0 = (1.0f + cos_w0) * 0.5f;
    float b1 = -(1.0f + cos_w0);
    float b2 = (1.0f + cos_w0) * 0.5f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cos_w0;
    float a2 = 1.0f - alpha;
    biquad->b0 = b0 / a0;
    biquad->b1 = b1 / a0;
    biquad->b2 = b2 / a0;
    biquad->a1 = a1 / a0;
    biquad->a2 = a2 / a0;
    biquad->z1 = 0.0f;
    biquad->z2 = 0.0f;
}

// Designs a K-weighting high-shelf biquad at the specified frequency and gain.
static void lufs_biquad_design_highshelf(EngineFxLufsBiquad* biquad,
                                         float sample_rate,
                                         float freq_hz,
                                         float gain_db,
                                         float slope) {
    if (!biquad || sample_rate <= 0.0f || freq_hz <= 0.0f) {
        return;
    }
    float a = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * freq_hz / sample_rate;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float sqrt_a = sqrtf(a);
    float alpha = sin_w0 / 2.0f * sqrtf((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
    float b0 = a * ((a + 1.0f) + (a - 1.0f) * cos_w0 + 2.0f * sqrt_a * alpha);
    float b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cos_w0);
    float b2 = a * ((a + 1.0f) + (a - 1.0f) * cos_w0 - 2.0f * sqrt_a * alpha);
    float a0 = (a + 1.0f) - (a - 1.0f) * cos_w0 + 2.0f * sqrt_a * alpha;
    float a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cos_w0);
    float a2 = (a + 1.0f) - (a - 1.0f) * cos_w0 - 2.0f * sqrt_a * alpha;
    biquad->b0 = b0 / a0;
    biquad->b1 = b1 / a0;
    biquad->b2 = b2 / a0;
    biquad->a1 = a1 / a0;
    biquad->a2 = a2 / a0;
    biquad->z1 = 0.0f;
    biquad->z2 = 0.0f;
}

// Processes a single sample through a biquad filter.
static float lufs_biquad_process(EngineFxLufsBiquad* biquad, float in) {
    if (!biquad) {
        return in;
    }
    float out = biquad->b0 * in + biquad->z1;
    biquad->z1 = biquad->b1 * in - biquad->a1 * out + biquad->z2;
    biquad->z2 = biquad->b2 * in - biquad->a2 * out;
    return out;
}

// Resets LUFS tracking state and reinitializes K-weighting filters.
static void engine_fx_lufs_state_reset(EngineFxLufsState* state, int sample_rate, int channels) {
    if (!state) {
        return;
    }
    SDL_zero(*state);
    state->sample_rate = sample_rate;
    state->channels = channels;
    state->block_target = sample_rate / ENGINE_FX_LUFS_BLOCK_HZ;
    if (state->block_target < 1) {
        state->block_target = 1;
    }
    int used_channels = channels;
    if (used_channels > ENGINE_FX_LUFS_MAX_CHANNELS) {
        used_channels = ENGINE_FX_LUFS_MAX_CHANNELS;
    }
    for (int ch = 0; ch < used_channels; ++ch) {
        lufs_biquad_design_highpass(&state->hp[ch], (float)sample_rate, 60.0f, 0.707f);
        lufs_biquad_design_highshelf(&state->hs[ch], (float)sample_rate, 4000.0f, 4.0f, 1.0f);
    }
    state->lufs_integrated = -90.0f;
    state->lufs_short_term = -90.0f;
    state->lufs_momentary = -90.0f;
}

// Updates LUFS tracking state using K-weighted audio samples.
static void engine_fx_lufs_state_update(EngineFxLufsState* state,
                                        const float* buffer,
                                        int frames,
                                        int channels) {
    if (!state || !buffer || frames <= 0 || channels <= 0) {
        return;
    }
    int used_channels = channels;
    if (used_channels > ENGINE_FX_LUFS_MAX_CHANNELS) {
        used_channels = ENGINE_FX_LUFS_MAX_CHANNELS;
    }
    for (int i = 0; i < frames; ++i) {
        double frame_sum = 0.0;
        int base = i * channels;
        for (int ch = 0; ch < used_channels; ++ch) {
            float v = buffer[base + ch];
            v = lufs_biquad_process(&state->hp[ch], v);
            v = lufs_biquad_process(&state->hs[ch], v);
            frame_sum += (double)v * (double)v;
        }
        state->block_sum += frame_sum;
        state->block_samples += 1;
        if (state->block_samples >= state->block_target) {
            double mean_square = state->block_sum / (double)state->block_samples;
            state->block_sum = 0.0;
            state->block_samples = 0;

            state->block_history[state->block_head] = mean_square;
            state->block_head = (state->block_head + 1) % ENGINE_FX_LUFS_SHORT_BLOCKS;
            if (state->block_count < ENGINE_FX_LUFS_SHORT_BLOCKS) {
                state->block_count += 1;
            }

            int momentary_blocks = ENGINE_FX_LUFS_MOMENTARY_BLOCKS;
            int short_blocks = ENGINE_FX_LUFS_SHORT_BLOCKS;
            if (state->block_count < momentary_blocks) {
                momentary_blocks = state->block_count;
            }
            if (state->block_count < short_blocks) {
                short_blocks = state->block_count;
            }
            double momentary_energy = mean_square;
            if (momentary_blocks > 0) {
                double sum = 0.0;
                for (int m = 0; m < momentary_blocks; ++m) {
                    int idx = state->block_head - 1 - m;
                    while (idx < 0) idx += ENGINE_FX_LUFS_SHORT_BLOCKS;
                    idx %= ENGINE_FX_LUFS_SHORT_BLOCKS;
                    sum += state->block_history[idx];
                }
                momentary_energy = sum / (double)momentary_blocks;
                state->lufs_momentary = lufs_energy_to_db(momentary_energy);
            }
            if (short_blocks > 0) {
                double sum = 0.0;
                for (int s = 0; s < short_blocks; ++s) {
                    int idx = state->block_head - 1 - s;
                    while (idx < 0) idx += ENGINE_FX_LUFS_SHORT_BLOCKS;
                    idx %= ENGINE_FX_LUFS_SHORT_BLOCKS;
                    sum += state->block_history[idx];
                }
                state->lufs_short_term = lufs_energy_to_db(sum / (double)short_blocks);
            }

            float lufs_block = lufs_energy_to_db(momentary_energy);
            int bin = (int)lroundf((lufs_block - ENGINE_FX_LUFS_HIST_MIN_DB) / ENGINE_FX_LUFS_HIST_STEP_DB);
            if (bin < 0) bin = 0;
            if (bin >= ENGINE_FX_LUFS_HIST_BINS) bin = ENGINE_FX_LUFS_HIST_BINS - 1;
            state->hist_sum[bin] += momentary_energy;
            state->hist_count[bin] += 1;

            double abs_sum = 0.0;
            int abs_count = 0;
            int abs_start = (int)ceilf((-70.0f - ENGINE_FX_LUFS_HIST_MIN_DB) / ENGINE_FX_LUFS_HIST_STEP_DB);
            if (abs_start < 0) abs_start = 0;
            for (int b = abs_start; b < ENGINE_FX_LUFS_HIST_BINS; ++b) {
                abs_sum += state->hist_sum[b];
                abs_count += state->hist_count[b];
            }
            if (abs_count > 0) {
                double mean_abs = abs_sum / (double)abs_count;
                float rel_gate_db = lufs_energy_to_db(mean_abs) - 10.0f;
                int rel_start = (int)ceilf((rel_gate_db - ENGINE_FX_LUFS_HIST_MIN_DB) / ENGINE_FX_LUFS_HIST_STEP_DB);
                if (rel_start < abs_start) rel_start = abs_start;
                if (rel_start < 0) rel_start = 0;
                double rel_sum = 0.0;
                int rel_count = 0;
                for (int b = rel_start; b < ENGINE_FX_LUFS_HIST_BINS; ++b) {
                    rel_sum += state->hist_sum[b];
                    rel_count += state->hist_count[b];
                }
                if (rel_count > 0) {
                    state->lufs_integrated = lufs_energy_to_db(rel_sum / (double)rel_count);
                }
            }
        }
    }
}

// Clears LUFS tracking state for a meter tap.
static void engine_fx_lufs_state_clear(EngineFxLufsState* state) {
    if (!state) {
        return;
    }
    SDL_zero(*state);
}

// Clears the FX meter bank and resets per-meter state.
static void engine_fx_meter_clear_bank(EngineFxMeterBank* bank) {
    if (!bank) {
        return;
    }
    for (int i = 0; i < bank->count; ++i) {
        engine_fx_lufs_state_clear(&bank->taps[i].lufs_state);
    }
    SDL_zero(*bank);
}

// Finds an FX meter tap entry by id in the given bank.
static EngineFxMeterTap* engine_fx_meter_find_tap(EngineFxMeterBank* bank, FxInstId id) {
    if (!bank || id == 0) {
        return NULL;
    }
    for (int i = 0; i < bank->count; ++i) {
        if (bank->taps[i].id == id) {
            return &bank->taps[i];
        }
    }
    return NULL;
}

// Returns an FX meter tap entry, creating or overwriting one if needed.
static EngineFxMeterTap* engine_fx_meter_get_or_add_tap(EngineFxMeterBank* bank, FxInstId id) {
    if (!bank || id == 0) {
        return NULL;
    }
    EngineFxMeterTap* tap = engine_fx_meter_find_tap(bank, id);
    if (tap) {
        return tap;
    }
    if (bank->count < FX_MASTER_MAX) {
        tap = &bank->taps[bank->count++];
    } else {
        tap = &bank->taps[FX_MASTER_MAX - 1];
        engine_fx_lufs_state_clear(&tap->lufs_state);
    }
    SDL_zero(*tap);
    tap->id = id;
    return tap;
}

// Copies LUFS state from a meter tap into the provided output.
static bool engine_fx_meter_copy_lufs_state(const EngineFxMeterBank* bank,
                                            FxInstId id,
                                            EngineFxLufsState* out_state) {
    if (!bank || !out_state || id == 0) {
        return false;
    }
    for (int i = 0; i < bank->count; ++i) {
        if (bank->taps[i].id == id) {
            *out_state = bank->taps[i].lufs_state;
            return true;
        }
    }
    return false;
}

static bool engine_fx_meter_find_in_bank(const EngineFxMeterBank* bank,
                                         FxInstId id,
                                         EngineFxMeterSnapshot* out_snapshot) {
    if (!bank || !out_snapshot || id == 0) {
        return false;
    }
    for (int i = 0; i < bank->count; ++i) {
        if (bank->taps[i].id == id) {
            *out_snapshot = bank->taps[i].snapshot;
            return out_snapshot->valid;
        }
    }
    return false;
}

// Computes peak/RMS/correlation data and updates LUFS state if requested.
static void engine_fx_meter_compute(const float* buffer,
                                    int frames,
                                    int channels,
                                    FxTypeId type,
                                    int sample_rate,
                                    EngineFxLufsState* lufs_state,
                                    EngineFxMeterSnapshot* snapshot) {
    if (!buffer || frames <= 0 || channels <= 0 || !snapshot) {
        return;
    }
    double sum = 0.0;
    float peak = 0.0f;
    int count = frames * channels;
    double sum_l2 = 0.0;
    double sum_r2 = 0.0;
    double sum_lr = 0.0;
    double sum_mid2 = 0.0;
    double sum_side2 = 0.0;
    double sum_l = 0.0;
    double sum_r = 0.0;

    snapshot->vec_point_count = 0;
    if (channels >= 2) {
        int points = ENGINE_FX_METER_VEC_POINTS;
        if (frames < points) {
            points = frames;
        }
        for (int i = 0; i < points; ++i) {
            int frame_idx = (i * frames) / points;
            int base = frame_idx * channels;
            float l = buffer[base];
            float r = buffer[base + 1];
            snapshot->vec_points_x[i] = clampf(l, -1.0f, 1.0f);
            snapshot->vec_points_y[i] = clampf(r, -1.0f, 1.0f);
        }
        snapshot->vec_point_count = points;
        for (int i = 0; i < frames; ++i) {
            int base = i * channels;
            float l = buffer[base];
            float r = buffer[base + 1];
            float la = fabsf(l);
            float ra = fabsf(r);
            if (la > peak) peak = la;
            if (ra > peak) peak = ra;
            sum += (double)l * (double)l + (double)r * (double)r;
            sum_l2 += (double)l * (double)l;
            sum_r2 += (double)r * (double)r;
            sum_lr += (double)l * (double)r;
            float mid = 0.5f * (l + r);
            float side = 0.5f * (l - r);
            sum_mid2 += (double)mid * (double)mid;
            sum_side2 += (double)side * (double)side;
            sum_l += (double)l;
            sum_r += (double)r;
        }
        double denom = sqrt(sum_l2 * sum_r2);
        if (denom > 1e-12) {
            snapshot->corr = (float)(sum_lr / denom);
        } else {
            snapshot->corr = 0.0f;
        }
        snapshot->mid_rms = frames > 0 ? (float)sqrt(sum_mid2 / (double)frames) : 0.0f;
        snapshot->side_rms = frames > 0 ? (float)sqrt(sum_side2 / (double)frames) : 0.0f;
        snapshot->vec_x = frames > 0 ? (float)(sum_l / (double)frames) : 0.0f;
        snapshot->vec_y = frames > 0 ? (float)(sum_r / (double)frames) : 0.0f;
        snapshot->rms = count > 0 ? (float)sqrt(sum / (double)count) : 0.0f;
    } else {
        for (int i = 0; i < count; ++i) {
            float v = buffer[i];
            float a = fabsf(v);
            if (a > peak) {
                peak = a;
            }
            sum += (double)v * (double)v;
        }
        snapshot->rms = count > 0 ? (float)sqrt(sum / (double)count) : 0.0f;
        snapshot->corr = 1.0f;
        snapshot->mid_rms = snapshot->rms;
        snapshot->side_rms = 0.0f;
        snapshot->vec_x = 0.0f;
        snapshot->vec_y = 0.0f;
        snapshot->vec_point_count = 0;
    }
    snapshot->peak = peak;
    snapshot->clipped = peak > 1.0f;
    if (lufs_state && type == 104u) {
        if (lufs_state->sample_rate != sample_rate || lufs_state->channels != channels) {
            engine_fx_lufs_state_reset(lufs_state, sample_rate, channels);
        }
        engine_fx_lufs_state_update(lufs_state, buffer, frames, channels);
        snapshot->lufs_integrated = lufs_state->lufs_integrated;
        snapshot->lufs_short_term = lufs_state->lufs_short_term;
        snapshot->lufs_momentary = lufs_state->lufs_momentary;
    }
}

static void engine_fx_meter_tap_callback(void* user,
                                         bool is_master,
                                         int track_index,
                                         FxInstId id,
                                         FxTypeId type,
                                         const float* interleaved,
                                         int frames,
                                         int channels) {
    Engine* engine = (Engine*)user;
    if (!engine || !interleaved || frames <= 0 || channels <= 0 || id == 0) {
        return;
    }
    EngineFxLufsState lufs_state;
    SDL_zero(lufs_state);
    EngineFxMeterBank* bank = NULL;
    if (!is_master && (!engine->track_fx_meters ||
                       track_index < 0 ||
                       track_index >= engine->track_fx_meter_capacity)) {
        return;
    }
    bank = is_master ? &engine->master_fx_meters : &engine->track_fx_meters[track_index];
    if (bank) {
        engine_fx_meter_copy_lufs_state(bank, id, &lufs_state);
    }
    EngineFxMeterSnapshot snapshot = {0};
    snapshot.type = type;
    snapshot.valid = true;
    snapshot.lufs_integrated = -90.0f;
    snapshot.lufs_short_term = -90.0f;
    snapshot.lufs_momentary = -90.0f;
    engine_fx_meter_compute(interleaved,
                            frames,
                            channels,
                            type,
                            engine->config.sample_rate,
                            &lufs_state,
                            &snapshot);
    if (type == 105u) {
        engine_spectrogram_update_fx(engine, is_master, track_index, id, interleaved, frames, channels);
    }

    bank = is_master ? &engine->master_fx_meters : &engine->track_fx_meters[track_index];
    if (bank) {
        EngineFxMeterTap* tap = engine_fx_meter_get_or_add_tap(bank, id);
        if (tap) {
            tap->lufs_state = lufs_state;
        }
    }

    const int write_index = engine_meter_write_index(engine);
    EngineFxMeterBank* snap_bank = NULL;
    if (is_master) {
        snap_bank = &engine->master_fx_meter_snapshots[write_index];
    } else if (engine->track_fx_meter_snapshots &&
               track_index >= 0 &&
               track_index < engine->track_fx_meter_capacity) {
        snap_bank = &engine->track_fx_meter_snapshots[(size_t)write_index * (size_t)engine->track_fx_meter_capacity +
                                                      (size_t)track_index];
    }
    if (snap_bank) {
        EngineFxMeterTap* tap = engine_fx_meter_get_or_add_tap(snap_bank, id);
        if (tap) {
            tap->snapshot = snapshot;
            tap->snapshot.valid = true;
        }
    }

    // Notify main-thread UI that FX meter-visible data changed; posting is coalesced.
    (void)daw_mainthread_message_post(DAW_MAINTHREAD_MSG_ENGINE_FX_METER,
                                      (uint64_t)id,
                                      engine);
}

void engine_fx_meter_clear_all(Engine* engine) {
    if (!engine) {
        return;
    }
    engine_fx_meter_clear_bank(&engine->master_fx_meters);
    if (engine->track_fx_meters) {
        for (int i = 0; i < engine->track_fx_meter_capacity; ++i) {
            engine_fx_meter_clear_bank(&engine->track_fx_meters[i]);
        }
    }
    SDL_zero(engine->master_fx_meter_snapshots);
    if (engine->track_fx_meter_snapshots) {
        size_t count = (size_t)engine->track_fx_meter_capacity * 2u;
        memset(engine->track_fx_meter_snapshots, 0, sizeof(EngineFxMeterBank) * count);
    }
}

bool engine_get_master_meter_snapshot(const Engine* engine, EngineMeterSnapshot* out_snapshot) {
    if (!engine || !out_snapshot) {
        return false;
    }
    int read_index = engine_meter_read_index(engine);
    *out_snapshot = engine->master_meter_snapshots[read_index];
    return true;
}

bool engine_get_track_meter_snapshot(const Engine* engine, int track_index, EngineMeterSnapshot* out_snapshot) {
    if (!engine || !out_snapshot || track_index < 0) {
        return false;
    }
    if (!engine->track_meters || track_index >= engine->track_meter_capacity) {
        return false;
    }
    int read_index = engine_meter_read_index(engine);
    const EngineMeterSnapshot* snaps = engine->track_meter_snapshots +
                                       (size_t)read_index * (size_t)engine->track_meter_capacity;
    *out_snapshot = snaps[track_index];
    return true;
}

bool engine_get_master_fx_meter_snapshot(const Engine* engine, FxInstId id, EngineFxMeterSnapshot* out_snapshot) {
    if (!engine || !out_snapshot) {
        return false;
    }
    bool ok = false;
    int read_index = engine_meter_read_index(engine);
    ok = engine_fx_meter_find_in_bank(&engine->master_fx_meter_snapshots[read_index], id, out_snapshot);
    return ok;
}

bool engine_get_track_fx_meter_snapshot(const Engine* engine,
                                        int track_index,
                                        FxInstId id,
                                        EngineFxMeterSnapshot* out_snapshot) {
    if (!engine || !out_snapshot || track_index < 0) {
        return false;
    }
    if (!engine->track_fx_meters || track_index >= engine->track_fx_meter_capacity) {
        return false;
    }
    if (!engine->track_fx_meter_snapshots) {
        return false;
    }
    bool ok = false;
    int read_index = engine_meter_read_index(engine);
    const EngineFxMeterBank* bank = &engine->track_fx_meter_snapshots[(size_t)read_index *
                                                                      (size_t)engine->track_fx_meter_capacity +
                                                                      (size_t)track_index];
    ok = engine_fx_meter_find_in_bank(bank, id, out_snapshot);
    return ok;
}

void engine_set_active_fx_meter(Engine* engine, bool is_master, int track_index, FxInstId id) {
    if (!engine) {
        return;
    }
    engine->active_fx_meter_id = id;
    engine->active_fx_meter_is_master = is_master;
    engine->active_fx_meter_track = track_index;
}

void engine_register_fx_meter_tap(Engine* engine) {
    if (!engine || !engine->fxm_mutex) {
        return;
    }
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        fxm_set_meter_tap_callback(engine->fxm, engine_fx_meter_tap_callback, engine);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
}
