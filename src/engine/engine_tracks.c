#include "engine/engine_internal.h"

#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

// Supplies process-local identities for new track lifetimes, independent of array indices.
static atomic_uint_fast64_t next_track_runtime_id = 1;

// Initializes a new editable track lifetime with a fresh identity and default content.
void engine_track_init(EngineTrack* track) {
    if (!track) {
        return;
    }
    track->runtime_id = atomic_fetch_add_explicit(&next_track_runtime_id, 1, memory_order_relaxed);
    track->clips = NULL;
    track->clip_count = 0;
    track->clip_capacity = 0;
    track->gain = 1.0f;
    track->pan = 0.0f;
    track->muted = false;
    track->solo = false;
    track->active = true;
    track->name[0] = '\0';
    track->midi_instrument_enabled = false;
    track->midi_instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    track->midi_instrument_params = engine_instrument_default_params(track->midi_instrument_preset);
    track->midi_instrument_automation_lanes = NULL;
    track->midi_instrument_automation_lane_count = 0;
    track->midi_instrument_automation_lane_capacity = 0;
    memset(&track->track_eq, 0, sizeof(track->track_eq));
}

static void engine_track_midi_clear_instrument_automation(EngineTrack* track) {
    if (!track || !track->midi_instrument_automation_lanes) {
        return;
    }
    for (int i = 0; i < track->midi_instrument_automation_lane_count; ++i) {
        engine_automation_lane_free(&track->midi_instrument_automation_lanes[i]);
    }
    free(track->midi_instrument_automation_lanes);
    track->midi_instrument_automation_lanes = NULL;
    track->midi_instrument_automation_lane_count = 0;
    track->midi_instrument_automation_lane_capacity = 0;
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
    track->midi_instrument_enabled = false;
    track->midi_instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    track->midi_instrument_params = engine_instrument_default_params(track->midi_instrument_preset);
    engine_track_midi_clear_instrument_automation(track);
    engine_eq_free(&track->track_eq);
}

// Prepares all track-indexed storage before committing any pointer or capacity change.
static bool engine_ensure_track_capacity_locked(Engine* engine, int required_tracks) {
    if (!engine || required_tracks < 0) return false;
    if (required_tracks <= engine->track_capacity) return true;
    int capacity = engine->track_capacity > 0 ? engine->track_capacity : 1;
    while (capacity < required_tracks) {
        if (capacity > INT_MAX / 2) return false;
        capacity *= 2;
    }
    size_t count = (size_t)capacity;
    if (count > SIZE_MAX / (2 * sizeof(EngineFxMeterBank))) return false;
    EngineTrack* tracks = calloc(count, sizeof(*tracks));
    float* spectra = calloc(count * ENGINE_SPECTRUM_BINS, sizeof(*spectra));
    EngineMeterState* meters = calloc(count, sizeof(*meters));
    EngineMeterSnapshot* snapshots = calloc(count * 2, sizeof(*snapshots));
    EngineFxMeterBank* fx_meters = calloc(count, sizeof(*fx_meters));
    EngineFxMeterSnapshotBank* fx_snapshots = calloc(count * 2, sizeof(*fx_snapshots));
    if (!tracks || !spectra || !meters || !snapshots || !fx_meters || !fx_snapshots) goto fail;
    if (engine->track_capacity) memcpy(tracks, engine->tracks, (size_t)engine->track_capacity * sizeof(*tracks));
    for (int t = engine->track_capacity; t < capacity; ++t) engine_track_init(&tracks[t]);
    for (size_t i = 0; i < count * ENGINE_SPECTRUM_BINS; ++i) spectra[i] = ENGINE_SPECTRUM_DB_FLOOR;
    if (engine->track_spectra) memcpy(spectra, engine->track_spectra,
        (size_t)engine->track_spectrum_capacity * ENGINE_SPECTRUM_BINS * sizeof(*spectra));
    if (engine->track_meters) memcpy(meters, engine->track_meters,
        (size_t)engine->track_meter_capacity * sizeof(*meters));
    if (engine->track_fx_meters) memcpy(fx_meters, engine->track_fx_meters,
        (size_t)engine->track_fx_meter_capacity * sizeof(*fx_meters));
    // Each published buffer has its own stride; reallocating raw bytes cannot preserve the second row.
    for (int b = 0; b < 2; ++b) {
        if (engine->track_meter_snapshots) memcpy(snapshots + (size_t)b * count,
            engine->track_meter_snapshots + (size_t)b * engine->track_meter_capacity,
            (size_t)engine->track_meter_capacity * sizeof(*snapshots));
        if (engine->track_fx_meter_snapshots) memcpy(fx_snapshots + (size_t)b * count,
            engine->track_fx_meter_snapshots + (size_t)b * engine->track_fx_meter_capacity,
            (size_t)engine->track_fx_meter_capacity * sizeof(*fx_snapshots));
    }
    if (!engine_scope_ensure_track_capacity(engine, capacity)) goto fail;
    free(engine->tracks); engine->tracks = tracks;
    SDL_LockMutex(engine->spectrum_mutex);
    free(engine->track_spectra); engine->track_spectra = spectra;
    engine->track_spectrum_capacity = capacity;
    SDL_UnlockMutex(engine->spectrum_mutex);
    free(engine->track_meters); engine->track_meters = meters;
    free(engine->track_meter_snapshots); engine->track_meter_snapshots = snapshots;
    free(engine->track_fx_meters); engine->track_fx_meters = fx_meters;
    free(engine->track_fx_meter_snapshots); engine->track_fx_meter_snapshots = fx_snapshots;
    engine->track_capacity = engine->track_meter_capacity = engine->track_fx_meter_capacity = capacity;
    return true;
fail:
    // Track resources were only borrowed into the candidate array; the original still owns them.
    free(tracks); free(spectra); free(meters); free(snapshots); free(fx_meters); free(fx_snapshots);
    return false;
}

// Changes all UI meter capacities atomically relative to opportunistic render publication.
bool engine_ensure_track_capacity(Engine* engine, int required_tracks) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id) return false;
    SDL_LockMutex(engine->meter_mutex);
    bool ok = engine_ensure_track_capacity_locked(engine, required_tracks);
    SDL_UnlockMutex(engine->meter_mutex);
    return ok;
}

EngineTrack* engine_get_track_mutable(Engine* engine, int track_index) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track_index < 0 || track_index == INT_MAX) {
        return NULL;
    }
    if (!engine_ensure_track_capacity(engine, track_index + 1)) {
        return NULL;
    }
    while (engine->track_count <= track_index) {
        engine_eq_free(&engine->tracks[engine->track_count].track_eq);
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

// Clears UI caches after track indices change while render-owned histories retain stable identities.
// Invalidates track-indexed meter and scope readback after accepted topology changes.
void engine_track_reset_published_caches(Engine* engine) {
    SDL_LockMutex(engine->meter_mutex);
    for (int t = 0; t < engine->track_capacity; ++t) {
        engine_scope_reset_track_bank(engine, t);
        if (engine->track_meters) engine_meter_reset_state(&engine->track_meters[t]);
        if (engine->track_fx_meters) SDL_zero(engine->track_fx_meters[t]);
    }
    if (engine->track_meter_snapshots) memset(engine->track_meter_snapshots, 0,
        2 * (size_t)engine->track_meter_capacity * sizeof(*engine->track_meter_snapshots));
    if (engine->track_fx_meter_snapshots) memset(engine->track_fx_meter_snapshots, 0,
        2 * (size_t)engine->track_fx_meter_capacity * sizeof(*engine->track_fx_meter_snapshots));
    SDL_UnlockMutex(engine->meter_mutex);
}

// Appends a track through the same transactional publication path as insertion.
int engine_add_track(Engine* engine) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id) return -1;
    int index = engine->track_count;
    return engine_insert_track(engine, index) ? index : -1;
}

// Publishes an inserted track and remapped effects together, restoring all editable ownership on failure.
bool engine_insert_track(Engine* engine, int track_index) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id) return false;
    if (track_index < 0) track_index = 0;
    if (track_index > engine->track_count) track_index = engine->track_count;
    if (!engine_ensure_track_capacity(engine, engine->track_count + 1)) return false;
    SDL_LockMutex(engine->fxm_mutex);
    EffectsManager* previous_fx = engine->fxm;
    EffectsManager* candidate_fx = previous_fx ? fxm_clone_for_render(previous_fx) : NULL;
    if (previous_fx && (!candidate_fx || !fxm_set_track_count(candidate_fx, engine->track_count) ||
                        !fxm_insert_track(candidate_fx, track_index))) {
        fxm_destroy(candidate_fx);
        SDL_UnlockMutex(engine->fxm_mutex);
        return false;
    }
    int remaining = engine->track_count - track_index;
    EngineTrack spare = engine->tracks[engine->track_count];
    memmove(&engine->tracks[track_index + 1], &engine->tracks[track_index], (size_t)remaining * sizeof(EngineTrack));
    EngineTrack* added = &engine->tracks[track_index];
    engine_track_init(added);
    engine_eq_init(&added->track_eq, (float)engine->config.sample_rate, engine_graph_get_channels(engine->graph));
    added->active = false;
    snprintf(added->name, sizeof(added->name), "Track %d", track_index + 1);
    ++engine->track_count;
    engine->fxm = candidate_fx;
    int armed = atomic_load(&engine->record_armed_track_index);
    if (armed >= track_index) atomic_store(&engine->record_armed_track_index, armed + 1);
    if (!engine_request_rebuild_sources(engine)) {
        engine_track_clear(engine, added);
        memmove(&engine->tracks[track_index], &engine->tracks[track_index + 1], (size_t)remaining * sizeof(EngineTrack));
        engine->tracks[--engine->track_count] = spare;
        engine->fxm = previous_fx;
        atomic_store(&engine->record_armed_track_index, armed);
        fxm_destroy(candidate_fx);
        SDL_UnlockMutex(engine->fxm_mutex);
        return false;
    }
    fxm_destroy(previous_fx);
    SDL_UnlockMutex(engine->fxm_mutex);
    engine_track_clear(engine, &spare);
    engine_track_reset_published_caches(engine);
    return true;
}

// Keeps detached track and effect ownership until the replacement revision is ready, then reclaims it.
bool engine_remove_track(Engine* engine, int track_index) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track_index < 0 || track_index >= engine->track_count) return false;
    SDL_LockMutex(engine->fxm_mutex);
    EffectsManager* previous_fx = engine->fxm;
    EffectsManager* candidate_fx = previous_fx ? fxm_clone_for_render(previous_fx) : NULL;
    if (previous_fx && (!candidate_fx || !fxm_set_track_count(candidate_fx, engine->track_count) ||
                        !fxm_remove_track(candidate_fx, track_index))) {
        fxm_destroy(candidate_fx);
        SDL_UnlockMutex(engine->fxm_mutex);
        return false;
    }
    EngineTrack removed = engine->tracks[track_index];
    int remaining = engine->track_count - track_index - 1;
    int armed = atomic_load(&engine->record_armed_track_index);
    memmove(&engine->tracks[track_index], &engine->tracks[track_index + 1], (size_t)remaining * sizeof(EngineTrack));
    --engine->track_count;
    engine->fxm = candidate_fx;
    atomic_store(&engine->record_armed_track_index, armed == track_index ? -1 : armed > track_index ? armed - 1 : armed);
    if (!engine_request_rebuild_sources(engine)) {
        memmove(&engine->tracks[track_index + 1], &engine->tracks[track_index], (size_t)remaining * sizeof(EngineTrack));
        engine->tracks[track_index] = removed;
        ++engine->track_count;
        engine->fxm = previous_fx;
        atomic_store(&engine->record_armed_track_index, armed);
        fxm_destroy(candidate_fx);
        SDL_UnlockMutex(engine->fxm_mutex);
        return false;
    }
    fxm_destroy(previous_fx);
    SDL_UnlockMutex(engine->fxm_mutex);
    engine_track_init(&engine->tracks[engine->track_count]);
    engine_track_clear(engine, &removed);
    engine_track_reset_published_caches(engine);
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

bool engine_track_midi_get_instrument_automation_lanes(const Engine* engine,
                                                       int track_index,
                                                       const EngineAutomationLane** out_lanes,
                                                       int* out_lane_count) {
    if (out_lanes) {
        *out_lanes = NULL;
    }
    if (out_lane_count) {
        *out_lane_count = 0;
    }
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    const EngineTrack* track = &engine->tracks[track_index];
    if (out_lanes) {
        *out_lanes = track->midi_instrument_automation_lanes;
    }
    if (out_lane_count) {
        *out_lane_count = track->midi_instrument_automation_lane_count;
    }
    return true;
}

// Publishes complete track-instrument automation while retaining old lane ownership on failure.
bool engine_track_midi_set_instrument_automation_lanes(Engine* engine, int track_index,
                                                       const EngineAutomationLane* lanes, int lane_count) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track_index < 0 || track_index >= engine->track_count ||
        lane_count < 0 || (lane_count > 0 && !lanes)) return false;
    for (int i = 0; i < lane_count; ++i) {
        if (!engine_automation_target_is_instrument_param(lanes[i].target) || lanes[i].point_count < 0 ||
            (lanes[i].point_count > 0 && !lanes[i].points)) return false;
        for (int j = 0; j < lanes[i].point_count; ++j) if (!isfinite(lanes[i].points[j].value)) return false;
    }
    EngineTrack candidate = {0};
    if (lane_count > 0) {
        candidate.midi_instrument_automation_lanes = calloc((size_t)lane_count, sizeof(*lanes));
        if (!candidate.midi_instrument_automation_lanes) return false;
        candidate.midi_instrument_automation_lane_count = candidate.midi_instrument_automation_lane_capacity = lane_count;
        for (int i = 0; i < lane_count; ++i) {
            if (!engine_automation_lane_copy(&lanes[i], &candidate.midi_instrument_automation_lanes[i])) {
                engine_track_midi_clear_instrument_automation(&candidate);
                return false;
            }
        }
    }
    EngineTrack* track = &engine->tracks[track_index];
    EngineTrack previous = *track;
    track->midi_instrument_automation_lanes = candidate.midi_instrument_automation_lanes;
    track->midi_instrument_automation_lane_count = track->midi_instrument_automation_lane_capacity = lane_count;
    if (!engine_request_rebuild_sources(engine)) {
        track->midi_instrument_automation_lanes = previous.midi_instrument_automation_lanes;
        track->midi_instrument_automation_lane_count = previous.midi_instrument_automation_lane_count;
        track->midi_instrument_automation_lane_capacity = previous.midi_instrument_automation_lane_capacity;
        engine_track_midi_clear_instrument_automation(&candidate);
        return false;
    }
    engine_track_midi_clear_instrument_automation(&previous);
    return true;
}

// Stages a lane replacement in borrowed descriptors and lets the whole-lane transaction own the copy.
bool engine_track_midi_set_instrument_automation_lane_points(Engine* engine, int track_index,
                                                             EngineAutomationTarget target,
                                                             const EngineAutomationPoint* points, int count) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track_index < 0 || track_index >= engine->track_count ||
        !engine_automation_target_is_instrument_param(target) || count < 0 || (count > 0 && !points)) return false;
    const EngineTrack* track = &engine->tracks[track_index];
    int old_count = track->midi_instrument_automation_lane_count;
    if (old_count == INT_MAX) return false;
    int index = old_count;
    for (int i = 0; i < old_count; ++i)
        if (track->midi_instrument_automation_lanes[i].target == target) { index = i; break; }
    int lane_count = old_count + (index == old_count);
    EngineAutomationLane* lanes = calloc((size_t)lane_count, sizeof(*lanes));
    if (!lanes) return false;
    for (int i = 0; i < old_count; ++i) lanes[i] = track->midi_instrument_automation_lanes[i];
    lanes[index] = (EngineAutomationLane){.target = target, .points = (EngineAutomationPoint*)points,
                                         .point_count = count, .point_capacity = count};
    bool accepted = engine_track_midi_set_instrument_automation_lanes(engine, track_index, lanes, lane_count);
    free(lanes);
    return accepted;
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

// Publishes muted atomically with respect to preparation failure and preserves the previous value on rejection.
bool engine_track_set_muted(Engine* engine, int track_index, bool muted) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id ||
        track_index < 0 || track_index >= engine->track_count) return false;
    EngineTrack* track = &engine->tracks[track_index];
    bool previous = track->muted;
    track->muted = muted;
    if (engine_request_mixer_update(engine)) return true;
    track->muted = previous;
    return false;
}

// Publishes solo atomically with respect to preparation failure and preserves the previous value on rejection.
bool engine_track_set_solo(Engine* engine, int track_index, bool solo) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id ||
        track_index < 0 || track_index >= engine->track_count) return false;
    EngineTrack* track = &engine->tracks[track_index];
    bool previous = track->solo;
    track->solo = solo;
    if (engine_request_mixer_update(engine)) return true;
    track->solo = previous;
    return false;
}

// Commits recording isolation only when a matching source revision is ready for adoption.
bool engine_set_record_armed_track(Engine* engine, int track_index) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track_index >= engine->track_count) return false;
    if (track_index < 0) track_index = -1;
    int previous = atomic_load_explicit(&engine->record_armed_track_index, memory_order_acquire);
    atomic_store_explicit(&engine->record_armed_track_index, track_index, memory_order_release);
    if (engine_request_rebuild_sources(engine)) return true;
    atomic_store_explicit(&engine->record_armed_track_index, previous, memory_order_release);
    return false;
}

// Publishes a validated mixer/instrument settings snapshot as one indivisible control edit.
bool engine_track_set_settings(Engine* engine, int track_index, const EngineTrackSettings* settings) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || !settings ||
        track_index < 0 || track_index >= engine->track_count || !isfinite(settings->gain) || settings->gain < 0 ||
        !isfinite(settings->pan) || settings->pan < -1 || settings->pan > 1) return false;
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i)
        if (!isfinite(engine_instrument_params_get(settings->instrument_params, (EngineInstrumentParamId)i))) return false;
    EngineTrack* track = &engine->tracks[track_index];
    EngineTrack previous = *track;
    track->gain = settings->gain;
    track->pan = settings->pan;
    track->muted = settings->muted;
    track->solo = settings->solo;
    track->midi_instrument_enabled = settings->instrument_enabled;
    track->midi_instrument_preset = engine_instrument_preset_clamp(settings->instrument_preset);
    track->midi_instrument_params = engine_instrument_params_sanitize(track->midi_instrument_preset, settings->instrument_params);
    if (engine_request_rebuild_sources(engine)) return true;
    track->gain = previous.gain;
    track->pan = previous.pan;
    track->muted = previous.muted;
    track->solo = previous.solo;
    track->midi_instrument_enabled = previous.midi_instrument_enabled;
    track->midi_instrument_preset = previous.midi_instrument_preset;
    track->midi_instrument_params = previous.midi_instrument_params;
    return false;
}

// Publishes gain atomically with respect to preparation failure and preserves the previous value on rejection.
bool engine_track_set_gain(Engine* engine, int track_index, float gain) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id ||
        track_index < 0 || track_index >= engine->track_count || !isfinite(gain) || gain < 0.0f) return false;
    EngineTrack* track = &engine->tracks[track_index];
    float previous = track->gain;
    track->gain = gain;
    if (engine_request_mixer_update(engine)) return true;
    track->gain = previous;
    return false;
}

// Publishes pan atomically with respect to preparation failure and preserves the previous value on rejection.
bool engine_track_set_pan(Engine* engine, int track_index, float pan) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id ||
        track_index < 0 || track_index >= engine->track_count || !isfinite(pan) || pan < -1.0f || pan > 1.0f) return false;
    EngineTrack* track = &engine->tracks[track_index];
    float previous = track->pan;
    track->pan = pan;
    if (engine_request_mixer_update(engine)) return true;
    track->pan = previous;
    return false;
}

// Updates control-owned EQ coefficients and publishes an independently prepared render revision.
static bool engine_apply_eq_curve(Engine* engine, int target, int track_index, const EngineEqCurve* curve) {
    if (!engine || !curve || SDL_ThreadID() != engine->control_thread_id) return false;
    if (target != 0 && (track_index < 0 || track_index >= engine->track_count)) return false;
    if ((curve->low_cut.enabled && (!isfinite(curve->low_cut.freq_hz) || curve->low_cut.freq_hz <= 0)) ||
        (curve->high_cut.enabled && (!isfinite(curve->high_cut.freq_hz) || curve->high_cut.freq_hz <= 0))) return false;
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i) {
        const EngineEqBand* band = &curve->bands[i];
        if (band->enabled && (!isfinite(band->freq_hz) || band->freq_hz <= 0 ||
            !isfinite(band->gain_db) || !isfinite(band->q_width))) return false;
    }
    EngineEqState* eq = target == 0 ? &engine->master_eq : &engine->tracks[track_index].track_eq;
    // Curve application changes coefficients only; history pointers remain owned by this same state.
    EngineEqState previous = *eq;
    engine_eq_set_curve(eq, curve);
    if (engine_request_rebuild_sources(engine)) return true;
    *eq = previous;
    return false;
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

// Checks control ownership before project capture or replacement begins.
bool engine_is_control_thread(const Engine* engine) {
    return engine && SDL_ThreadID() == engine->control_thread_id;
}

// Captures accepted EQ parameters without consulting a potentially stale UI draft.
bool engine_get_eq_curve(const Engine* engine, int track_index, EngineEqCurve* out_curve) {
    if (!engine_is_control_thread(engine) || !out_curve || track_index >= engine->track_count) return false;
    *out_curve = track_index < 0 ? engine->master_eq.curve : engine->tracks[track_index].track_eq.curve;
    return true;
}
