#include "engine/engine_internal.h"
#include "engine/fx_chain.h"

// Mutates only a private candidate and captures all accepted settings before publication.
bool engine_fx_chain_transaction(Engine* engine, int track, const EngineFxChainRequest* request, FxMasterSnapshot* out) {
    if (!engine || !request || !out || !engine_is_control_thread(engine) || track < -1 || track >= engine->track_count) return false;
    SDL_LockMutex(engine->fxm_mutex);
    EffectsManager* original = engine->fxm;
    EffectsManager* candidate = fxm_clone_for_render(original);
    bool ok = candidate != NULL;
    bool master = track < 0;
    if (ok) switch (request->op) {
        case ENGINE_FX_CHAIN_ADD:
            ok = (master ? fxm_master_add(candidate, request->type) : fxm_track_add(candidate, track, request->type)) != 0;
            break;
        case ENGINE_FX_CHAIN_REMOVE:
            ok = master ? fxm_master_remove(candidate, request->id) : fxm_track_remove(candidate, track, request->id);
            break;
        case ENGINE_FX_CHAIN_REORDER:
            ok = master ? fxm_master_reorder(candidate, request->id, request->position) : fxm_track_reorder(candidate, track, request->id, request->position);
            break;
        case ENGINE_FX_CHAIN_ENABLE:
            ok = master ? fxm_master_set_enabled(candidate, request->id, request->enabled) : fxm_track_set_enabled(candidate, track, request->id, request->enabled);
            break;
        case ENGINE_FX_CHAIN_PARAM:
            ok = master ? fxm_master_set_param_with_mode(candidate, request->id, request->parameter, request->value, request->mode, request->beats)
                        : fxm_track_set_param_with_mode(candidate, track, request->id, request->parameter, request->value, request->mode, request->beats);
            break;
        case ENGINE_FX_CHAIN_REPLACE:
        case ENGINE_FX_CHAIN_RESTORE: {
            FxMasterSnapshot current = {0};
            ok = request->restore && request->restore->count >= 0 && request->restore->count <= FX_MASTER_MAX &&
                 (master ? fxm_master_snapshot(candidate, &current) : fxm_track_snapshot(candidate, track, &current));
            for (int i = current.count - 1; ok && i >= 0; --i)
                ok = master ? fxm_master_remove(candidate, current.items[i].id) : fxm_track_remove(candidate, track, current.items[i].id);
            for (int i = 0; ok && i < request->restore->count; ++i) {
                const FxMasterInstanceInfo* value=&request->restore->items[i];
                if(request->op==ENGINE_FX_CHAIN_RESTORE)ok=fxm_restore_instance(candidate,track,i,value);
                else {
                    FxInstId id=master?fxm_master_add(candidate,value->type):fxm_track_add(candidate,track,value->type);
                    ok=id!=0;
                    uint32_t count=0;const EffectParamSpec* specs=NULL;
                    specs=fxm_registry_get_param_specs(candidate,value->type,&count);
                    ok=ok && specs && count==value->param_count;
                    for(uint32_t p=0;ok && p<count;++p)if(!(specs[p].flags & FX_PARAM_FLAG_READ_ONLY_METER))
                        ok=master?fxm_master_set_param_with_mode(candidate,id,p,value->params[p],value->param_mode[p],value->param_beats[p]):fxm_track_set_param_with_mode(candidate,track,id,p,value->params[p],value->param_mode[p],value->param_beats[p]);
                    if(ok)ok=master?fxm_master_set_enabled(candidate,id,value->enabled):fxm_track_set_enabled(candidate,track,id,value->enabled);
                }
            }
            break;
        }
        default: ok = false;
    }
    FxMasterSnapshot captured = {0};
    if (ok) ok = master ? fxm_master_snapshot(candidate, &captured) : fxm_track_snapshot(candidate, track, &captured);
    if (ok) {
        engine->fxm = candidate;
        ok = engine_request_rebuild_sources(engine);
        if (!ok) engine->fxm = original;
    }
    fxm_destroy(ok ? original : candidate);
    SDL_UnlockMutex(engine->fxm_mutex);
    if (ok) *out = captured;
    return ok;
}
