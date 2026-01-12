#include "engine/engine_internal.h"

#include <math.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void engine_fx_meter_clear_bank(EngineFxMeterBank* bank) {
    if (!bank) {
        return;
    }
    SDL_zero(*bank);
}

static void engine_fx_meter_update_bank(EngineFxMeterBank* bank,
                                        FxInstId id,
                                        const EngineFxMeterSnapshot* snapshot) {
    if (!bank || !snapshot || id == 0) {
        return;
    }
    for (int i = 0; i < bank->count; ++i) {
        if (bank->taps[i].id == id) {
            bank->taps[i].snapshot = *snapshot;
            return;
        }
    }
    if (bank->count < FX_MASTER_MAX) {
        bank->taps[bank->count].id = id;
        bank->taps[bank->count].snapshot = *snapshot;
        bank->count += 1;
        return;
    }
    bank->taps[FX_MASTER_MAX - 1].id = id;
    bank->taps[FX_MASTER_MAX - 1].snapshot = *snapshot;
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

static void engine_fx_meter_compute(const float* buffer,
                                    int frames,
                                    int channels,
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
    if (engine->meter_mutex) {
        SDL_LockMutex(engine->meter_mutex);
    }
    FxInstId active_id = engine->active_fx_meter_id;
    bool active_master = engine->active_fx_meter_is_master;
    int active_track = engine->active_fx_meter_track;
    if (engine->meter_mutex) {
        SDL_UnlockMutex(engine->meter_mutex);
    }
    if (active_id == 0) {
        return;
    }
    if (active_id != id) {
        return;
    }
    if (active_master != is_master) {
        return;
    }
    if (!active_master && track_index != active_track) {
        return;
    }
    EngineFxMeterSnapshot snapshot = {0};
    snapshot.type = type;
    snapshot.valid = true;
    engine_fx_meter_compute(interleaved, frames, channels, &snapshot);

    if (engine->meter_mutex) {
        SDL_LockMutex(engine->meter_mutex);
    }
    if (is_master) {
        engine_fx_meter_update_bank(&engine->master_fx_meters, id, &snapshot);
    } else if (engine->track_fx_meters && track_index >= 0 && track_index < engine->track_fx_meter_capacity) {
        engine_fx_meter_update_bank(&engine->track_fx_meters[track_index], id, &snapshot);
    }
    if (engine->meter_mutex) {
        SDL_UnlockMutex(engine->meter_mutex);
    }
}

void engine_fx_meter_clear_all(Engine* engine) {
    if (!engine) {
        return;
    }
    if (engine->meter_mutex) {
        SDL_LockMutex(engine->meter_mutex);
    }
    engine_fx_meter_clear_bank(&engine->master_fx_meters);
    if (engine->track_fx_meters) {
        for (int i = 0; i < engine->track_fx_meter_capacity; ++i) {
            engine_fx_meter_clear_bank(&engine->track_fx_meters[i]);
        }
    }
    if (engine->meter_mutex) {
        SDL_UnlockMutex(engine->meter_mutex);
    }
}

// Copies the latest meter state into a snapshot for UI use.
static bool engine_meter_snapshot_from_state(const Engine* engine,
                                             const EngineMeterState* state,
                                             EngineMeterSnapshot* out_snapshot) {
    if (!engine || !state || !out_snapshot) {
        return false;
    }
    out_snapshot->peak = state->peak;
    out_snapshot->rms = state->rms;
    out_snapshot->clipped = state->clip_hold > 0;
    return true;
}

bool engine_get_master_meter_snapshot(const Engine* engine, EngineMeterSnapshot* out_snapshot) {
    if (!engine || !out_snapshot) {
        return false;
    }
    if (engine->meter_mutex) {
        SDL_LockMutex(engine->meter_mutex);
    }
    bool ok = engine_meter_snapshot_from_state(engine, &engine->master_meter, out_snapshot);
    if (engine->meter_mutex) {
        SDL_UnlockMutex(engine->meter_mutex);
    }
    return ok;
}

bool engine_get_track_meter_snapshot(const Engine* engine, int track_index, EngineMeterSnapshot* out_snapshot) {
    if (!engine || !out_snapshot || track_index < 0) {
        return false;
    }
    if (!engine->track_meters || track_index >= engine->track_meter_capacity) {
        return false;
    }
    if (engine->meter_mutex) {
        SDL_LockMutex(engine->meter_mutex);
    }
    bool ok = engine_meter_snapshot_from_state(engine, &engine->track_meters[track_index], out_snapshot);
    if (engine->meter_mutex) {
        SDL_UnlockMutex(engine->meter_mutex);
    }
    return ok;
}

bool engine_get_master_fx_meter_snapshot(const Engine* engine, FxInstId id, EngineFxMeterSnapshot* out_snapshot) {
    if (!engine || !out_snapshot) {
        return false;
    }
    bool ok = false;
    if (engine->meter_mutex) {
        SDL_LockMutex(engine->meter_mutex);
    }
    ok = engine_fx_meter_find_in_bank(&engine->master_fx_meters, id, out_snapshot);
    if (engine->meter_mutex) {
        SDL_UnlockMutex(engine->meter_mutex);
    }
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
    bool ok = false;
    if (engine->meter_mutex) {
        SDL_LockMutex(engine->meter_mutex);
    }
    ok = engine_fx_meter_find_in_bank(&engine->track_fx_meters[track_index], id, out_snapshot);
    if (engine->meter_mutex) {
        SDL_UnlockMutex(engine->meter_mutex);
    }
    return ok;
}

void engine_set_active_fx_meter(Engine* engine, bool is_master, int track_index, FxInstId id) {
    if (!engine) {
        return;
    }
    if (engine->meter_mutex) {
        SDL_LockMutex(engine->meter_mutex);
    }
    engine->active_fx_meter_id = id;
    engine->active_fx_meter_is_master = is_master;
    engine->active_fx_meter_track = track_index;
    if (engine->meter_mutex) {
        SDL_UnlockMutex(engine->meter_mutex);
    }
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
