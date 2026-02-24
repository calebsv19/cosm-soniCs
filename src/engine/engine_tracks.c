#include "engine/engine_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void engine_track_init(EngineTrack* track) {
    if (!track) {
        return;
    }
    track->clips = NULL;
    track->clip_count = 0;
    track->clip_capacity = 0;
    track->gain = 1.0f;
    track->pan = 0.0f;
    track->muted = false;
    track->solo = false;
    track->active = true;
    track->name[0] = '\0';
    memset(&track->track_eq, 0, sizeof(track->track_eq));
}

void engine_meter_reset_state(EngineMeterState* state) {
    if (!state) {
        return;
    }
    state->peak = 0.0f;
    state->rms = 0.0f;
    state->clip_hold = 0;
}

void engine_track_clear(Engine* engine, EngineTrack* track) {
    if (!track) {
        return;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        engine_clip_destroy(engine, &track->clips[i]);
    }
    free(track->clips);
    track->clips = NULL;
    track->clip_count = 0;
    track->clip_capacity = 0;
    track->gain = 1.0f;
    track->pan = 0.0f;
    track->muted = false;
    track->solo = false;
    track->active = true;
    track->name[0] = '\0';
    engine_eq_free(&track->track_eq);
}

bool engine_ensure_track_capacity(Engine* engine, int required_tracks) {
    if (!engine) {
        return false;
    }
    if (required_tracks <= engine->track_capacity) {
        return true;
    }
    int new_capacity = engine->track_capacity;
    while (new_capacity < required_tracks) {
        new_capacity *= 2;
    }
    EngineTrack* resized = (EngineTrack*)realloc(engine->tracks, sizeof(EngineTrack) * (size_t)new_capacity);
    if (!resized) {
        return false;
    }
    engine->tracks = resized;
    for (int i = engine->track_capacity; i < new_capacity; ++i) {
        engine_track_init(&resized[i]);
        engine_eq_init(&resized[i].track_eq, (float)engine->config.sample_rate, engine_graph_get_channels(engine->graph));
    }
    if (engine->track_spectra) {
        if (engine->spectrum_mutex) {
            SDL_LockMutex(engine->spectrum_mutex);
        }
        size_t count = (size_t)new_capacity * ENGINE_SPECTRUM_BINS;
        float* resized_spec = (float*)realloc(engine->track_spectra, sizeof(float) * count);
        if (!resized_spec) {
            if (engine->spectrum_mutex) {
                SDL_UnlockMutex(engine->spectrum_mutex);
            }
            return false;
        }
        engine->track_spectra = resized_spec;
        for (int t = engine->track_capacity; t < new_capacity; ++t) {
            for (int b = 0; b < ENGINE_SPECTRUM_BINS; ++b) {
                resized_spec[t * ENGINE_SPECTRUM_BINS + b] = ENGINE_SPECTRUM_DB_FLOOR;
            }
        }
        engine->track_spectrum_capacity = new_capacity;
        if (engine->spectrum_mutex) {
            SDL_UnlockMutex(engine->spectrum_mutex);
        }
    }
    if (engine->track_meters) {
        if (engine->meter_mutex) {
            SDL_LockMutex(engine->meter_mutex);
        }
        EngineMeterState* resized_meters = (EngineMeterState*)realloc(engine->track_meters,
                                                                      sizeof(EngineMeterState) * (size_t)new_capacity);
        if (!resized_meters) {
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
            return false;
        }
        engine->track_meters = resized_meters;
        for (int t = engine->track_meter_capacity; t < new_capacity; ++t) {
            engine_meter_reset_state(&engine->track_meters[t]);
        }
        engine->track_meter_capacity = new_capacity;
        if (engine->meter_mutex) {
            SDL_UnlockMutex(engine->meter_mutex);
        }
    }
    if (engine->track_meter_snapshots) {
        if (engine->meter_mutex) {
            SDL_LockMutex(engine->meter_mutex);
        }
        size_t snap_count = (size_t)new_capacity * 2u;
        EngineMeterSnapshot* resized_snaps = (EngineMeterSnapshot*)realloc(engine->track_meter_snapshots,
                                                                           sizeof(EngineMeterSnapshot) * snap_count);
        if (!resized_snaps) {
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
            return false;
        }
        engine->track_meter_snapshots = resized_snaps;
        for (int t = engine->track_meter_capacity; t < new_capacity; ++t) {
            for (int b = 0; b < 2; ++b) {
                size_t idx = (size_t)b * (size_t)new_capacity + (size_t)t;
                SDL_zero(engine->track_meter_snapshots[idx]);
            }
        }
        if (engine->meter_mutex) {
            SDL_UnlockMutex(engine->meter_mutex);
        }
    }
    if (engine->track_fx_meters) {
        if (engine->meter_mutex) {
            SDL_LockMutex(engine->meter_mutex);
        }
        EngineFxMeterBank* resized_banks = (EngineFxMeterBank*)realloc(engine->track_fx_meters,
                                                                       sizeof(EngineFxMeterBank) * (size_t)new_capacity);
        if (!resized_banks) {
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
            return false;
        }
        engine->track_fx_meters = resized_banks;
        for (int t = engine->track_fx_meter_capacity; t < new_capacity; ++t) {
            SDL_zero(engine->track_fx_meters[t]);
        }
        engine->track_fx_meter_capacity = new_capacity;
        if (engine->meter_mutex) {
            SDL_UnlockMutex(engine->meter_mutex);
        }
    }
    if (engine->track_fx_meter_snapshots) {
        if (engine->meter_mutex) {
            SDL_LockMutex(engine->meter_mutex);
        }
        size_t snap_count = (size_t)new_capacity * 2u;
        EngineFxMeterBank* resized_banks = (EngineFxMeterBank*)realloc(engine->track_fx_meter_snapshots,
                                                                       sizeof(EngineFxMeterBank) * snap_count);
        if (!resized_banks) {
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
            return false;
        }
        engine->track_fx_meter_snapshots = resized_banks;
        for (int t = engine->track_fx_meter_capacity; t < new_capacity; ++t) {
            for (int b = 0; b < 2; ++b) {
                size_t idx = (size_t)b * (size_t)new_capacity + (size_t)t;
                SDL_zero(engine->track_fx_meter_snapshots[idx]);
            }
        }
        if (engine->meter_mutex) {
            SDL_UnlockMutex(engine->meter_mutex);
        }
    }
    if (!engine_scope_ensure_track_capacity(engine, new_capacity)) {
        return false;
    }
    engine->track_capacity = new_capacity;
    return true;
}

EngineTrack* engine_get_track_mutable(Engine* engine, int track_index) {
    if (!engine || track_index < 0) {
        return NULL;
    }
    if (!engine_ensure_track_capacity(engine, track_index + 1)) {
        return NULL;
    }
    while (engine->track_count <= track_index) {
        engine_track_init(&engine->tracks[engine->track_count]);
        engine_eq_init(&engine->tracks[engine->track_count].track_eq,
                       (float)engine->config.sample_rate,
                       engine_graph_get_channels(engine->graph));
        engine->tracks[engine->track_count].active = false;
        engine_scope_reset_track_bank(engine, engine->track_count);
        ++engine->track_count;
    }
    return &engine->tracks[track_index];
}

int engine_add_track(Engine* engine) {
    if (!engine) {
        return -1;
    }
    int index = engine->track_count;
    if (!engine_get_track_mutable(engine, index)) {
        return -1;
    }
    engine->tracks[index].active = false;
    engine_track_set_name(engine, index, NULL);
    return index;
}

bool engine_insert_track(Engine* engine, int track_index) {
    if (!engine) {
        return false;
    }
    if (track_index < 0) {
        track_index = 0;
    }
    if (track_index > engine->track_count) {
        track_index = engine->track_count;
    }
    if (!engine_ensure_track_capacity(engine, engine->track_count + 1)) {
        return false;
    }
    if (track_index < engine->track_count) {
        memmove(&engine->tracks[track_index + 1],
                &engine->tracks[track_index],
                (size_t)(engine->track_count - track_index) * sizeof(EngineTrack));
        if (engine->track_spectra) {
            if (engine->spectrum_mutex) {
                SDL_LockMutex(engine->spectrum_mutex);
            }
            memmove(&engine->track_spectra[(track_index + 1) * ENGINE_SPECTRUM_BINS],
                    &engine->track_spectra[track_index * ENGINE_SPECTRUM_BINS],
                    (size_t)(engine->track_count - track_index) * ENGINE_SPECTRUM_BINS * sizeof(float));
            if (engine->spectrum_mutex) {
                SDL_UnlockMutex(engine->spectrum_mutex);
            }
        }
        if (engine->track_meters) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            memmove(&engine->track_meters[track_index + 1],
                    &engine->track_meters[track_index],
                    (size_t)(engine->track_count - track_index) * sizeof(EngineMeterState));
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
        if (engine->track_meter_snapshots) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            for (int b = 0; b < 2; ++b) {
                EngineMeterSnapshot* snaps = engine->track_meter_snapshots +
                                             (size_t)b * (size_t)engine->track_meter_capacity;
                memmove(&snaps[track_index + 1],
                        &snaps[track_index],
                        (size_t)(engine->track_count - track_index) * sizeof(EngineMeterSnapshot));
            }
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
        if (engine->track_fx_meters) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            memmove(&engine->track_fx_meters[track_index + 1],
                    &engine->track_fx_meters[track_index],
                    (size_t)(engine->track_count - track_index) * sizeof(EngineFxMeterBank));
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
        if (engine->track_fx_meter_snapshots) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            for (int b = 0; b < 2; ++b) {
                EngineFxMeterBank* banks = engine->track_fx_meter_snapshots +
                                           (size_t)b * (size_t)engine->track_fx_meter_capacity;
                memmove(&banks[track_index + 1],
                        &banks[track_index],
                        (size_t)(engine->track_count - track_index) * sizeof(EngineFxMeterBank));
            }
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
        if (engine->scope_host.tracks) {
            memmove(&engine->scope_host.tracks[track_index + 1],
                    &engine->scope_host.tracks[track_index],
                    (size_t)(engine->track_count - track_index) * sizeof(EngineFxScopeBank));
        }
    }
    engine_track_init(&engine->tracks[track_index]);
    engine_eq_init(&engine->tracks[track_index].track_eq,
                   (float)engine->config.sample_rate,
                   engine_graph_get_channels(engine->graph));
    engine->tracks[track_index].active = false;
    engine_track_set_name(engine, track_index, NULL);
    engine->track_count += 1;
    if (engine->track_spectra) {
        if (engine->spectrum_mutex) {
            SDL_LockMutex(engine->spectrum_mutex);
        }
        for (int b = 0; b < ENGINE_SPECTRUM_BINS; ++b) {
            engine->track_spectra[track_index * ENGINE_SPECTRUM_BINS + b] = ENGINE_SPECTRUM_DB_FLOOR;
        }
        if (engine->spectrum_mutex) {
            SDL_UnlockMutex(engine->spectrum_mutex);
        }
    }
    if (engine->track_meters) {
        if (engine->meter_mutex) {
            SDL_LockMutex(engine->meter_mutex);
        }
        engine_meter_reset_state(&engine->track_meters[track_index]);
        if (engine->meter_mutex) {
            SDL_UnlockMutex(engine->meter_mutex);
        }
    }
    if (engine->track_meter_snapshots) {
        if (engine->meter_mutex) {
            SDL_LockMutex(engine->meter_mutex);
        }
        for (int b = 0; b < 2; ++b) {
            size_t idx = (size_t)b * (size_t)engine->track_meter_capacity + (size_t)track_index;
            SDL_zero(engine->track_meter_snapshots[idx]);
        }
        if (engine->meter_mutex) {
            SDL_UnlockMutex(engine->meter_mutex);
        }
    }
    if (engine->track_fx_meters) {
        if (engine->meter_mutex) {
            SDL_LockMutex(engine->meter_mutex);
        }
        SDL_zero(engine->track_fx_meters[track_index]);
        if (engine->meter_mutex) {
            SDL_UnlockMutex(engine->meter_mutex);
        }
    }
    if (engine->track_fx_meter_snapshots) {
        if (engine->meter_mutex) {
            SDL_LockMutex(engine->meter_mutex);
        }
        for (int b = 0; b < 2; ++b) {
            size_t idx = (size_t)b * (size_t)engine->track_fx_meter_capacity + (size_t)track_index;
            SDL_zero(engine->track_fx_meter_snapshots[idx]);
        }
        if (engine->meter_mutex) {
            SDL_UnlockMutex(engine->meter_mutex);
        }
    }
    engine_scope_reset_track_bank(engine, track_index);
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_remove_track(Engine* engine, int track_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }

    EngineTrack* track = &engine->tracks[track_index];
    engine_track_clear(engine, track);

    if (track_index < engine->track_count - 1) {
        memmove(&engine->tracks[track_index],
                &engine->tracks[track_index + 1],
                (size_t)(engine->track_count - track_index - 1) * sizeof(EngineTrack));
        if (engine->track_spectra) {
            if (engine->spectrum_mutex) {
                SDL_LockMutex(engine->spectrum_mutex);
            }
            memmove(&engine->track_spectra[track_index * ENGINE_SPECTRUM_BINS],
                    &engine->track_spectra[(track_index + 1) * ENGINE_SPECTRUM_BINS],
                    (size_t)(engine->track_count - track_index - 1) * ENGINE_SPECTRUM_BINS * sizeof(float));
            if (engine->spectrum_mutex) {
                SDL_UnlockMutex(engine->spectrum_mutex);
            }
        }
        if (engine->track_meters) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            memmove(&engine->track_meters[track_index],
                    &engine->track_meters[track_index + 1],
                    (size_t)(engine->track_count - track_index - 1) * sizeof(EngineMeterState));
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
        if (engine->track_meter_snapshots) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            for (int b = 0; b < 2; ++b) {
                EngineMeterSnapshot* snaps = engine->track_meter_snapshots +
                                             (size_t)b * (size_t)engine->track_meter_capacity;
                memmove(&snaps[track_index],
                        &snaps[track_index + 1],
                        (size_t)(engine->track_count - track_index - 1) * sizeof(EngineMeterSnapshot));
            }
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
        if (engine->track_fx_meters) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            memmove(&engine->track_fx_meters[track_index],
                    &engine->track_fx_meters[track_index + 1],
                    (size_t)(engine->track_count - track_index - 1) * sizeof(EngineFxMeterBank));
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
        if (engine->track_fx_meter_snapshots) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            for (int b = 0; b < 2; ++b) {
                EngineFxMeterBank* banks = engine->track_fx_meter_snapshots +
                                           (size_t)b * (size_t)engine->track_fx_meter_capacity;
                memmove(&banks[track_index],
                        &banks[track_index + 1],
                        (size_t)(engine->track_count - track_index - 1) * sizeof(EngineFxMeterBank));
            }
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
        if (engine->scope_host.tracks) {
            memmove(&engine->scope_host.tracks[track_index],
                    &engine->scope_host.tracks[track_index + 1],
                    (size_t)(engine->track_count - track_index - 1) * sizeof(EngineFxScopeBank));
        }
    }

    engine->track_count--;
    if (engine->track_count < 0) {
        engine->track_count = 0;
    }
    if (engine->track_count >= 0 && engine->track_count < engine->track_capacity) {
        engine_track_init(&engine->tracks[engine->track_count]);
        if (engine->track_spectra) {
            if (engine->spectrum_mutex) {
                SDL_LockMutex(engine->spectrum_mutex);
            }
            for (int b = 0; b < ENGINE_SPECTRUM_BINS; ++b) {
                engine->track_spectra[engine->track_count * ENGINE_SPECTRUM_BINS + b] = ENGINE_SPECTRUM_DB_FLOOR;
            }
            if (engine->spectrum_mutex) {
                SDL_UnlockMutex(engine->spectrum_mutex);
            }
        }
        if (engine->track_meters) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            engine_meter_reset_state(&engine->track_meters[engine->track_count]);
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
        if (engine->track_meter_snapshots) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            for (int b = 0; b < 2; ++b) {
                size_t idx = (size_t)b * (size_t)engine->track_meter_capacity + (size_t)engine->track_count;
                SDL_zero(engine->track_meter_snapshots[idx]);
            }
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
        if (engine->track_fx_meters) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            SDL_zero(engine->track_fx_meters[engine->track_count]);
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
        if (engine->track_fx_meter_snapshots) {
            if (engine->meter_mutex) {
                SDL_LockMutex(engine->meter_mutex);
            }
            for (int b = 0; b < 2; ++b) {
                size_t idx = (size_t)b * (size_t)engine->track_fx_meter_capacity + (size_t)engine->track_count;
                SDL_zero(engine->track_fx_meter_snapshots[idx]);
            }
            if (engine->meter_mutex) {
                SDL_UnlockMutex(engine->meter_mutex);
            }
        }
    }

    engine_scope_reset_track_bank(engine, engine->track_count);
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_track_set_name(Engine* engine, int track_index, const char* name) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track) {
        return false;
    }
    if (name && name[0] != '\0') {
        strncpy(track->name, name, sizeof(track->name) - 1);
        track->name[sizeof(track->name) - 1] = '\0';
    } else {
        snprintf(track->name, sizeof(track->name), "Track %d", track_index + 1);
    }
    return true;
}

const EngineTrack* engine_get_tracks(const Engine* engine) {
    if (!engine) {
        return NULL;
    }
    return engine->tracks;
}

int engine_get_track_count(const Engine* engine) {
    if (!engine) {
        return 0;
    }
    return engine->track_count;
}

bool engine_track_set_muted(Engine* engine, int track_index, bool muted) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track) {
        return false;
    }
    track->muted = muted;
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_track_set_solo(Engine* engine, int track_index, bool solo) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track) {
        return false;
    }
    track->solo = solo;
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_track_set_gain(Engine* engine, int track_index, float gain) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track) {
        return false;
    }
    track->gain = gain;
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_track_set_pan(Engine* engine, int track_index, float pan) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track) {
        return false;
    }
    track->pan = pan;
    engine_request_rebuild_sources(engine);
    return true;
}

static bool engine_apply_eq_curve(Engine* engine, int target, int track_index, const EngineEqCurve* curve) {
    if (!engine || !curve) {
        return false;
    }
    if (!engine_is_running(engine)) {
        if (target == 0) {
            if (engine->eq_mutex) {
                SDL_LockMutex(engine->eq_mutex);
            }
            engine_eq_set_curve(&engine->master_eq, curve);
            if (engine->eq_mutex) {
                SDL_UnlockMutex(engine->eq_mutex);
            }
            return true;
        }
        if (track_index >= 0 && track_index < engine->track_count) {
            if (engine->eq_mutex) {
                SDL_LockMutex(engine->eq_mutex);
            }
            engine_eq_set_curve(&engine->tracks[track_index].track_eq, curve);
            if (engine->eq_mutex) {
                SDL_UnlockMutex(engine->eq_mutex);
            }
            return true;
        }
        return false;
    }
    EngineCommand cmd = {0};
    cmd.type = ENGINE_CMD_SET_EQ;
    cmd.payload.eq.target = target;
    cmd.payload.eq.track_index = track_index;
    cmd.payload.eq.curve = *curve;
    return engine_post_command(engine, &cmd);
}

bool engine_set_master_eq_curve(Engine* engine, const EngineEqCurve* curve) {
    return engine_apply_eq_curve(engine, 0, -1, curve);
}

bool engine_set_track_eq_curve(Engine* engine, int track_index, const EngineEqCurve* curve) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    return engine_apply_eq_curve(engine, 1, track_index, curve);
}
