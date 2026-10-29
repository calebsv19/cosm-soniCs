#include "app_state.h"
#include "daw/fx_actions.h"
#include "daw/midi_actions.h"
#include "effects/param_utils.h"
#include <math.h>
#include <stdlib.h>

// Resolves a stable chain identity while preserving the master sentinel.
static int chain_track(const AppState* state, uint64_t id) {
    if (!id) return -1;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    for (int i = 0; i < engine_get_track_count(state->engine); ++i) if (tracks[i].runtime_id == id) return i;
    return -2;
}

// Copies authored chain state only on the application owner thread.
DawActionStatus daw_action_get_fx(const AppState* state, uint64_t generation, uint64_t track_id, DawFxSnapshot* out) {
    if (!state || !state->engine || !out) return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(state->engine)) return DAW_ACTION_WRONG_THREAD;
    if (generation != engine_instance_id(state->engine)) return DAW_ACTION_STALE_TARGET;
    int track = chain_track(state, track_id);
    if (track == -2) return DAW_ACTION_STALE_TARGET;
    *out = (DawFxSnapshot){.generation = generation, .track_id = track_id};
    bool ok = track < 0 ? engine_fx_master_snapshot(state->engine, &out->chain) : engine_fx_track_snapshot(state->engine, track, &out->chain);
    return ok ? DAW_ACTION_UNCHANGED : DAW_ACTION_ENGINE_REJECTED;
}

// Compares complete order, identities, bypass state and all active physical parameter targets.
bool daw_fx_snapshot_equal(const DawFxSnapshot* a, const DawFxSnapshot* b) {
    if (!a || !b || a->generation != b->generation || a->track_id != b->track_id || a->chain.count != b->chain.count) return false;
    for (int i = 0; i < a->chain.count; ++i) {
        const FxMasterInstanceInfo* x = &a->chain.items[i];
        const FxMasterInstanceInfo* y = &b->chain.items[i];
        if(!engine_mixer_automation_equal(&x->automation,&y->automation))return false;
        if (x->id != y->id || x->type != y->type || x->enabled != y->enabled || x->param_count != y->param_count) return false;
        for (uint32_t p = 0; p < x->param_count; ++p)
            if (x->params[p] != y->params[p] || x->param_mode[p] != y->param_mode[p] || x->param_beats[p] != y->param_beats[p]) return false;
    }
    return true;
}

// Rejects invalid or unsupported physical parameter values before candidate preparation.
static bool valid_parameter(const AppState* state, const FxMasterInstanceInfo* effect, const EngineFxChainRequest* request) {
    uint32_t count = 0;
    const EffectParamSpec* specs = NULL;
    if (!engine_fx_registry_get_param_specs(state->engine, effect->type, &specs, &count) || request->parameter >= count || request->parameter >= effect->param_count) return false;
    const EffectParamSpec* spec = &specs[request->parameter];
    if (!isfinite(request->value) || request->value < spec->min_value || request->value > spec->max_value ||
        (spec->flags & FX_PARAM_FLAG_READ_ONLY_METER) || !isfinite(request->beats) || request->mode < FX_PARAM_MODE_NATIVE || request->mode > FX_PARAM_MODE_BEAT_RATE) return false;
    if (request->mode != FX_PARAM_MODE_NATIVE && (!(spec->flags & FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE) || request->beats <= 0)) return false;
    if (request->mode == FX_PARAM_MODE_NATIVE && request->beats != 0) return false;
    if (request->mode == FX_PARAM_MODE_BEAT_RATE && spec->unit != FX_PARAM_UNIT_HZ) return false;
    if (request->mode == FX_PARAM_MODE_BEATS && spec->unit != FX_PARAM_UNIT_MS && spec->unit != FX_PARAM_UNIT_SECONDS) return false;
    if ((spec->type == FX_PARAM_TYPE_BOOL || spec->type == FX_PARAM_TYPE_ENUM || spec->type == FX_PARAM_TYPE_INT) && request->value != floorf(request->value)) return false;
    return true;
}

// Reserves independent complete history before committing one private candidate.
DawActionStatus daw_action_edit_fx(AppState* state, const DawFxSnapshot* expected, const EngineFxChainRequest* request, DawFxSnapshot* out) {
    if (!expected || !request || !out || request->op < ENGINE_FX_CHAIN_ADD || request->op >= ENGINE_FX_CHAIN_RESTORE) return DAW_ACTION_INVALID;
    EngineFxChainRequest normalized = *request;
    request = &normalized;
    DawFxSnapshot current;
    DawActionStatus status = daw_action_get_fx(state, expected->generation, expected->track_id, &current);
    if (status != DAW_ACTION_UNCHANGED) return status;
    if (!daw_fx_snapshot_equal(expected, &current)) return DAW_ACTION_STALE_TARGET;
    if (daw_midi_actions_busy(state)) return DAW_ACTION_BUSY;
    FxMasterSnapshot portable={0};
    int position = -1;
    for (int i = 0; i < current.chain.count; ++i) if (current.chain.items[i].id == request->id) position = i;
    if (request->op == ENGINE_FX_CHAIN_ADD) {
        FxDesc desc;
        if (current.chain.count >= FX_MASTER_MAX || !engine_fx_registry_get_desc(state->engine, request->type, &desc)) return DAW_ACTION_INVALID;
    } else if(request->op==ENGINE_FX_CHAIN_REPLACE) {
        if(!request->restore || request->restore->count<0 || request->restore->count>FX_MASTER_MAX)return DAW_ACTION_INVALID;
        portable=*request->restore;
        for(int i=0;i<portable.count;++i){
            FxMasterInstanceInfo* value=&portable.items[i];FxDesc desc;uint32_t count=0;const EffectParamSpec* specs=NULL;
            if(!engine_fx_registry_get_desc(state->engine,value->type,&desc) || !engine_fx_registry_get_param_specs(state->engine,value->type,&specs,&count) || count!=value->param_count || count>FX_MAX_PARAMS)return DAW_ACTION_INVALID;
            if(!fx_parameter_automation_valid(value->type,&value->automation))return DAW_ACTION_INVALID;
            value->id=i<current.chain.count?current.chain.items[i].id:0;
            for(uint32_t p=0;p<count;++p){
                if(specs[p].flags & FX_PARAM_FLAG_READ_ONLY_METER){
                    if(value->param_mode[p]!=FX_PARAM_MODE_NATIVE || value->param_beats[p]!=0 || !isfinite(value->params[p]) || value->params[p]!=specs[p].default_value)return DAW_ACTION_INVALID;
                    continue;
                }
                EngineFxChainRequest parameter={.parameter=p,.value=value->params[p],.mode=value->param_mode[p],.beats=value->param_beats[p]};
                if(parameter.mode!=FX_PARAM_MODE_NATIVE)parameter.value=fx_param_spec_beats_to_native(&specs[p],parameter.beats,&state->tempo);
                if(!valid_parameter(state,value,&parameter))return DAW_ACTION_INVALID;
                value->params[p]=parameter.value;
            }
        }
        DawFxSnapshot comparison=current;comparison.chain=portable;
        if(daw_fx_snapshot_equal(&current,&comparison)){*out=current;return DAW_ACTION_UNCHANGED;}
        normalized.restore=&portable;
    } else {
        if (position < 0) return DAW_ACTION_INVALID;
        const FxMasterInstanceInfo* item = &current.chain.items[position];
        if (request->op == ENGINE_FX_CHAIN_REORDER && (request->position < 0 || request->position >= current.chain.count)) return DAW_ACTION_INVALID;
        if (request->op == ENGINE_FX_CHAIN_PARAM) {
            if (!isfinite(normalized.value) || !isfinite(normalized.beats)) return DAW_ACTION_INVALID;
            if (normalized.mode != FX_PARAM_MODE_NATIVE) {
                uint32_t count = 0; const EffectParamSpec* specs = NULL;
                if (!engine_fx_registry_get_param_specs(state->engine, item->type, &specs, &count) || normalized.parameter >= count) return DAW_ACTION_INVALID;
                normalized.value = fx_param_spec_beats_to_native(&specs[normalized.parameter], normalized.beats, &state->tempo);
            }
            if (!valid_parameter(state, item, request)) return DAW_ACTION_INVALID;
        }
        if(request->op==ENGINE_FX_CHAIN_AUTOMATION && (request->parameter!=0 || (item->type!=1 && item->type!=3) || !fx_parameter_automation_valid(item->type,&request->automation)))return DAW_ACTION_INVALID;
        bool same = (request->op==ENGINE_FX_CHAIN_AUTOMATION && engine_mixer_automation_equal(&item->automation,&request->automation)) || (request->op == ENGINE_FX_CHAIN_REORDER && position == request->position) ||
                    (request->op == ENGINE_FX_CHAIN_ENABLE && item->enabled == request->enabled) ||
                    (request->op == ENGINE_FX_CHAIN_PARAM && item->params[request->parameter] == request->value && item->param_mode[request->parameter] == request->mode && item->param_beats[request->parameter] == request->beats);
        if (same) { *out = current; return DAW_ACTION_UNCHANGED; }
    }
    DawFxHistory history = {.before = current, .after = current};
    UndoCommand command = {.type = UNDO_CMD_FX_CHAIN};
    command.data.fx_chain = &history;
    if (!undo_manager_begin_drag(&state->undo, &command)) return DAW_ACTION_HISTORY_UNAVAILABLE;
    DawFxHistory* reserved = state->undo.active_drag.data.fx_chain;
    if (!engine_fx_chain_transaction(state->engine, chain_track(state, expected->track_id), request, &reserved->after.chain)) {
        undo_manager_cancel_drag(&state->undo);
        return DAW_ACTION_ENGINE_REJECTED;
    }
    *out = reserved->after;
    return undo_manager_commit_drag(&state->undo, &state->undo.active_drag) ? DAW_ACTION_APPLIED : DAW_ACTION_HISTORY_UNAVAILABLE;
}

// Refuses stale or busy history and restores stable instance identities in one publication.
bool daw_fx_history_restore(AppState* state, const DawFxHistory* history, bool after) {
    if (!history) return false;
    const DawFxSnapshot* expected = after ? &history->before : &history->after;
    const DawFxSnapshot* value = after ? &history->after : &history->before;
    DawFxSnapshot current;
    if (daw_action_get_fx(state, expected->generation, expected->track_id, &current) != DAW_ACTION_UNCHANGED ||
        !daw_fx_snapshot_equal(expected, &current) || daw_midi_actions_busy(state)) return false;
    EngineFxChainRequest request = {.op = ENGINE_FX_CHAIN_RESTORE, .restore = &value->chain};
    return engine_fx_chain_transaction(state->engine, chain_track(state, value->track_id), &request, &current.chain);
}
