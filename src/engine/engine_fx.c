#include "engine/engine_internal.h"

#include "effects/effects_api.h"

#include <stdlib.h>
#include <math.h>

// Updates control-owned FX parameters and publishes a prepared DSP revision off the worker.
static bool engine_fx_post_param(Engine* engine, bool is_master, int track_index, FxInstId id,
                                 uint32_t param_index, float value, FxParamMode mode, float beat_value) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || !engine->fxm || id == 0) return false;
    if (!isfinite(value) || !isfinite(beat_value) || mode < FX_PARAM_MODE_NATIVE || mode > FX_PARAM_MODE_BEAT_RATE ||
        (mode != FX_PARAM_MODE_NATIVE && beat_value <= 0)) return false;
    SDL_LockMutex(engine->fxm_mutex);
    FxMasterSnapshot before = {0};
    bool ok = is_master ? fxm_master_snapshot(engine->fxm, &before)
                        : fxm_track_snapshot(engine->fxm, track_index, &before);
    const FxMasterInstanceInfo* previous = NULL;
    for (int i = 0; ok && i < before.count; ++i) {
        if (before.items[i].id == id) { previous = &before.items[i]; break; }
    }
    if (!previous || param_index >= previous->param_count || param_index >= FX_MAX_PARAMS) {
        SDL_UnlockMutex(engine->fxm_mutex);
        return false;
    }
    ok = is_master
        ? fxm_master_set_param_with_mode(engine->fxm, id, param_index, value, mode, beat_value)
        : fxm_track_set_param_with_mode(engine->fxm, track_index, id, param_index, value, mode, beat_value);
    if (ok) ok = engine_request_rebuild_sources(engine);
    if (!ok) {
        if (is_master) fxm_master_set_param_with_mode(engine->fxm, id, param_index,
            previous->params[param_index], previous->param_mode[param_index], previous->param_beats[param_index]);
        else fxm_track_set_param_with_mode(engine->fxm, track_index, id, param_index,
            previous->params[param_index], previous->param_mode[param_index], previous->param_beats[param_index]);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

// Identifies structural effect edits that need an independently prepared control candidate.
typedef enum { FX_EDIT_ADD, FX_EDIT_REMOVE, FX_EDIT_REORDER, FX_EDIT_ENABLED } EngineFxEdit;

// Commits an effect-chain edit only after its complete render revision has been prepared.
static FxInstId engine_fx_edit_chain(Engine* engine, int track, bool master, EngineFxEdit edit,
                                     FxInstId id, FxTypeId type, int position, bool enabled,
                                     FxMasterInstanceInfo* output, int* output_position) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || !engine->fxm_mutex) return 0;
    SDL_LockMutex(engine->fxm_mutex);
    EffectsManager* original = engine->fxm;
    EffectsManager* candidate = fxm_clone_for_render(original);
    bool ok = candidate != NULL;
    if (ok) {
        switch (edit) {
            case FX_EDIT_ADD:
                id = master ? fxm_master_add(candidate, type) : fxm_track_add(candidate, track, type);
                ok = id != 0;
                break;
            case FX_EDIT_REMOVE:
                ok = master ? fxm_master_remove(candidate, id) : fxm_track_remove(candidate, track, id);
                break;
            case FX_EDIT_REORDER:
                ok = master ? fxm_master_reorder(candidate, id, position) : fxm_track_reorder(candidate, track, id, position);
                break;
            case FX_EDIT_ENABLED:
                ok = master ? fxm_master_set_enabled(candidate, id, enabled) : fxm_track_set_enabled(candidate, track, id, enabled);
                break;
        }
    }
    FxMasterInstanceInfo captured = {0};
    int captured_position = -1;
    if (ok && output) {
        FxMasterSnapshot snapshot;
        ok = master ? fxm_master_snapshot(candidate, &snapshot) : fxm_track_snapshot(candidate, track, &snapshot);
        if (ok) for (int i = 0; i < snapshot.count; ++i)
            if (snapshot.items[i].id == id) { captured = snapshot.items[i]; captured_position = i; break; }
        ok = ok && captured_position >= 0;
    }
    if (ok) {
        engine->fxm = candidate;
        ok = engine_request_rebuild_sources(engine);
        if (!ok) engine->fxm = original;
    }
    fxm_destroy(ok ? original : candidate);
    SDL_UnlockMutex(engine->fxm_mutex);
    if (ok && output) { *output = captured; if (output_position) *output_position = captured_position; }
    return ok ? id : 0;
}

bool engine_fx_snapshot_all(Engine* engine, EngineFxSnapshot* out_snap) {
    if (!engine || !engine->fxm_mutex || !out_snap) {
        return false;
    }
    SDL_zero(*out_snap);
    SDL_LockMutex(engine->fxm_mutex);
    bool ok = false;
    FxMasterSnapshot master = {0};
    if (engine->fxm && fxm_master_snapshot(engine->fxm, &master)) {
        ok = true;
    }
    int tcount = engine->track_count;
    FxMasterSnapshot* tracks = NULL;
    if (ok && tcount > 0) {
        tracks = (FxMasterSnapshot*)calloc((size_t)tcount, sizeof(FxMasterSnapshot));
        if (!tracks) {
            ok = false;
        } else {
            for (int t = 0; t < tcount; ++t) {
                fxm_track_snapshot(engine->fxm, t, &tracks[t]);
            }
        }
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    if (!ok) {
        free(tracks);
        return false;
    }
    out_snap->master = master;
    out_snap->tracks = tracks;
    out_snap->track_count = tcount;
    return true;
}

void engine_fx_restore_all(Engine* engine, const EngineFxSnapshot* snap) {
    if (!engine || !engine->fxm_mutex || !snap || !engine->fxm) {
        return;
    }
    SDL_LockMutex(engine->fxm_mutex);
    fxm_set_track_count(engine->fxm, snap->track_count);
    for (int i = 0; i < snap->master.count && i < FX_MASTER_MAX; ++i) {
        const FxMasterInstanceInfo* src = &snap->master.items[i];
        FxInstId id = fxm_master_add(engine->fxm, src->type);
        if (!id) continue;
        uint32_t pc = src->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : src->param_count;
        for (uint32_t p = 0; p < pc; ++p) {
            FxParamMode mode = src->param_mode[p];
            float beat_value = src->param_beats[p];
            if (mode == FX_PARAM_MODE_NATIVE) {
                fxm_master_set_param(engine->fxm, id, p, src->params[p]);
            } else {
                fxm_master_set_param_with_mode(engine->fxm, id, p, src->params[p], mode, beat_value);
            }
        }
        if (!src->enabled) {
            fxm_master_set_enabled(engine->fxm, id, false);
        }
    }
    for (int t = 0; t < snap->track_count; ++t) {
        const FxMasterSnapshot* ts = &snap->tracks[t];
        for (int i = 0; i < ts->count && i < FX_MASTER_MAX; ++i) {
            const FxMasterInstanceInfo* src = &ts->items[i];
            FxInstId id = fxm_track_add(engine->fxm, t, src->type);
            if (!id) continue;
            uint32_t pc = src->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : src->param_count;
            for (uint32_t p = 0; p < pc; ++p) {
                FxParamMode mode = src->param_mode[p];
                float beat_value = src->param_beats[p];
                if (mode == FX_PARAM_MODE_NATIVE) {
                    fxm_track_set_param(engine->fxm, t, id, p, src->params[p]);
                } else {
                    fxm_track_set_param_with_mode(engine->fxm, t, id, p, src->params[p], mode, beat_value);
                }
            }
            if (!src->enabled) {
                fxm_track_set_enabled(engine->fxm, t, id, false);
            }
        }
    }
    SDL_UnlockMutex(engine->fxm_mutex);
}

bool engine_fx_get_registry(const Engine* engine, const FxRegistryEntry** out_entries, int* out_count) {
    if (!engine || !engine->fxm_mutex || SDL_ThreadID() != engine->control_thread_id) {
        return false;
    }
    if (out_entries) {
        *out_entries = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    SDL_LockMutex(engine->fxm_mutex);
    const FxRegistryEntry* entries = NULL;
    if (engine->fxm) {
        entries = fxm_get_registry(engine->fxm, out_count);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    if (!entries) {
        return false;
    }
    if (out_entries) {
        *out_entries = entries;
    }
    return true;
}

bool engine_fx_registry_get_desc(const Engine* engine, FxTypeId type, FxDesc* out_desc) {
    if (!engine || !out_desc || !engine->fxm_mutex) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        ok = fxm_registry_get_desc(engine->fxm, type, out_desc);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_registry_get_param_specs(const Engine* engine,
                                        FxTypeId type,
                                        const EffectParamSpec** out_specs,
                                        uint32_t* out_count) {
    if (!engine || !engine->fxm_mutex) {
        return false;
    }
    if (out_specs) {
        *out_specs = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        const EffectParamSpec* specs = fxm_registry_get_param_specs(engine->fxm, type, out_count);
        if (out_specs) {
            *out_specs = specs;
        }
        ok = specs != NULL;
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_master_snapshot(const Engine* engine, FxMasterSnapshot* out_snapshot) {
    if (!engine || !out_snapshot || !engine->fxm_mutex) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        ok = fxm_master_snapshot(engine->fxm, out_snapshot);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

FxInstId engine_fx_master_add(Engine* engine, FxTypeId type) {
    return engine_fx_edit_chain(engine, -1, true, FX_EDIT_ADD, 0, type, 0, false, NULL, NULL);
}

bool engine_fx_master_remove(Engine* engine, FxInstId id) {
    return engine_fx_edit_chain(engine, -1, true, FX_EDIT_REMOVE, id, 0, 0, false, NULL, NULL) != 0;
}

bool engine_fx_master_reorder(Engine* engine, FxInstId id, int new_index) {
    return engine_fx_edit_chain(engine, -1, true, FX_EDIT_REORDER, id, 0, new_index, false, NULL, NULL) != 0;
}

bool engine_fx_master_set_param(Engine* engine, FxInstId id, uint32_t param_index, float value) {
    if (!engine || !engine->fxm_mutex) {
        return false;
    }
    return engine_fx_post_param(engine, true, -1, id, param_index, value, FX_PARAM_MODE_NATIVE, 0.0f);
}

bool engine_fx_master_set_param_with_mode(Engine* engine,
                                          FxInstId id,
                                          uint32_t param_index,
                                          float value,
                                          FxParamMode mode,
                                          float beat_value) {
    if (!engine || !engine->fxm_mutex) {
        return false;
    }
    return engine_fx_post_param(engine, true, -1, id, param_index, value, mode, beat_value);
}

bool engine_fx_master_set_enabled(Engine* engine, FxInstId id, bool enabled) {
    return engine_fx_edit_chain(engine, -1, true, FX_EDIT_ENABLED, id, 0, 0, enabled, NULL, NULL) != 0;
}

FxInstId engine_fx_track_add(Engine* engine, int track_index, FxTypeId type) {
    return engine_fx_edit_chain(engine, track_index, false, FX_EDIT_ADD, 0, type, 0, false, NULL, NULL);
}

bool engine_fx_track_remove(Engine* engine, int track_index, FxInstId id) {
    return engine_fx_edit_chain(engine, track_index, false, FX_EDIT_REMOVE, id, 0, 0, false, NULL, NULL) != 0;
}

bool engine_fx_track_reorder(Engine* engine, int track_index, FxInstId id, int new_index) {
    return engine_fx_edit_chain(engine, track_index, false, FX_EDIT_REORDER, id, 0, new_index, false, NULL, NULL) != 0;
}

bool engine_fx_track_set_param(Engine* engine, int track_index, FxInstId id, uint32_t param_index, float value) {
    if (!engine || !engine->fxm_mutex) return false;
    return engine_fx_post_param(engine, false, track_index, id, param_index, value, FX_PARAM_MODE_NATIVE, 0.0f);
}

bool engine_fx_track_set_param_with_mode(Engine* engine,
                                         int track_index,
                                         FxInstId id,
                                         uint32_t param_index,
                                         float value,
                                         FxParamMode mode,
                                         float beat_value) {
    if (!engine || !engine->fxm_mutex) return false;
    return engine_fx_post_param(engine, false, track_index, id, param_index, value, mode, beat_value);
}

bool engine_fx_track_set_enabled(Engine* engine, int track_index, FxInstId id, bool enabled) {
    return engine_fx_edit_chain(engine, track_index, false, FX_EDIT_ENABLED, id, 0, 0, enabled, NULL, NULL) != 0;
}

bool engine_fx_track_snapshot(const Engine* engine, int track_index, FxMasterSnapshot* out_snapshot) {
    if (!engine || !engine->fxm_mutex || !out_snapshot) return false;
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_track_snapshot(engine->fxm, track_index, out_snapshot);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_set_track_count(Engine* engine, int track_count) {
    if (!engine || !engine->fxm_mutex || track_count < 0) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_set_track_count(engine->fxm, track_count);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

// Publishes a fully prepared restored effect without exposing intermediate defaults or new identities.
bool engine_fx_restore_instance(Engine* engine, int track_index, int position, const FxMasterInstanceInfo* instance) {
    if (!engine || !engine_is_control_thread(engine) || !engine->fxm_mutex ||
        track_index < -1 || track_index >= engine->track_count) return false;
    SDL_LockMutex(engine->fxm_mutex);
    EffectsManager* original = engine->fxm;
    EffectsManager* candidate = fxm_clone_for_render(original);
    bool ok = candidate && fxm_restore_instance(candidate, track_index, position, instance);
    if (ok) {
        engine->fxm = candidate;
        ok = engine_request_rebuild_sources(engine);
        if (!ok) engine->fxm = original;
    }
    fxm_destroy(ok ? original : candidate);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

// Reserves a complete effect readback on the private candidate before publishing the add.
FxInstId engine_fx_add_capture(Engine* engine, int track_index, FxTypeId type, FxMasterInstanceInfo* out, int* position) {
    if (!out || !position || !engine || track_index < -1 || track_index >= engine->track_count) return 0;
    return engine_fx_edit_chain(engine, track_index, track_index < 0, FX_EDIT_ADD, 0, type, 0, false, out, position);
}
