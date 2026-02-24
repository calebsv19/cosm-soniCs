#include "engine/engine_internal.h"

#include "effects/effects_api.h"

#include <stdlib.h>

// Posts an FX parameter change to the worker or applies immediately when offline.
static bool engine_fx_post_param(Engine* engine,
                                 bool is_master,
                                 int track_index,
                                 FxInstId id,
                                 uint32_t param_index,
                                 float value,
                                 FxParamMode mode,
                                 float beat_value) {
    if (!engine || id == 0) {
        return false;
    }
    if (!engine->device_started || !engine->worker_thread) {
        bool ok = false;
        SDL_LockMutex(engine->fxm_mutex);
        if (engine->fxm) {
            if (is_master) {
                ok = (mode == FX_PARAM_MODE_NATIVE)
                         ? fxm_master_set_param(engine->fxm, id, param_index, value)
                         : fxm_master_set_param_with_mode(engine->fxm, id, param_index, value, mode, beat_value);
            } else {
                ok = (mode == FX_PARAM_MODE_NATIVE)
                         ? fxm_track_set_param(engine->fxm, track_index, id, param_index, value)
                         : fxm_track_set_param_with_mode(engine->fxm, track_index, id, param_index, value, mode, beat_value);
            }
        }
        SDL_UnlockMutex(engine->fxm_mutex);
        return ok;
    }
    EngineCommand cmd = {
        .type = ENGINE_CMD_SET_FX_PARAM,
    };
    cmd.payload.fx_param.is_master = is_master;
    cmd.payload.fx_param.track_index = track_index;
    cmd.payload.fx_param.id = id;
    cmd.payload.fx_param.param_index = param_index;
    cmd.payload.fx_param.value = value;
    cmd.payload.fx_param.mode = mode;
    cmd.payload.fx_param.beat_value = beat_value;
    if (!engine_post_command(engine, &cmd)) {
        SDL_Log("engine_fx_post_param: command queue full");
        return false;
    }
    return true;
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
    if (!engine || !engine->fxm_mutex) {
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
    if (!engine || !engine->fxm_mutex) {
        return 0;
    }
    FxInstId id = 0;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        id = fxm_master_add(engine->fxm, type);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return id;
}

bool engine_fx_master_remove(Engine* engine, FxInstId id) {
    if (!engine || !engine->fxm_mutex || id == 0) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        ok = fxm_master_remove(engine->fxm, id);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_master_reorder(Engine* engine, FxInstId id, int new_index) {
    if (!engine || !engine->fxm_mutex || id == 0) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        ok = fxm_master_reorder(engine->fxm, id, new_index);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
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
    if (!engine || !engine->fxm_mutex || id == 0) {
        return false;
    }
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        ok = fxm_master_set_enabled(engine->fxm, id, enabled);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

FxInstId engine_fx_track_add(Engine* engine, int track_index, FxTypeId type) {
    if (!engine || !engine->fxm_mutex) return 0;
    FxInstId id = 0;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) {
        id = fxm_track_add(engine->fxm, track_index, type);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    return id;
}

bool engine_fx_track_remove(Engine* engine, int track_index, FxInstId id) {
    if (!engine || !engine->fxm_mutex || id == 0) return false;
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_track_remove(engine->fxm, track_index, id);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
}

bool engine_fx_track_reorder(Engine* engine, int track_index, FxInstId id, int new_index) {
    if (!engine || !engine->fxm_mutex || id == 0) return false;
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_track_reorder(engine->fxm, track_index, id, new_index);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
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
    if (!engine || !engine->fxm_mutex || id == 0) return false;
    bool ok = false;
    SDL_LockMutex(engine->fxm_mutex);
    if (engine->fxm) ok = fxm_track_set_enabled(engine->fxm, track_index, id, enabled);
    SDL_UnlockMutex(engine->fxm_mutex);
    return ok;
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
