#include "app_state.h"
#include "daw/fx_library.h"
#include "daw/midi_actions.h"
// Resolves the exact current authored chain before any library IO or candidate publication.
static DawFxPresetResult guard(AppState* state,const DawFxSnapshot* expected) {
    DawFxPresetResult result={.action=DAW_ACTION_INVALID,.library=DAW_PRESET_OK};
    if(!expected)return result;
    result.action=daw_action_get_fx(state,expected->generation,expected->track_id,&result.snapshot);
    if(result.action!=DAW_ACTION_UNCHANGED)return result;
    if(!daw_fx_snapshot_equal(expected,&result.snapshot))result.action=DAW_ACTION_STALE_TARGET;
    else if(daw_midi_actions_busy(state))result.action=DAW_ACTION_BUSY;
    return result;
}
// Stores complete current settings only after identity, chain and stopped-authoring guards pass.
DawFxPresetResult daw_action_save_fx_preset(AppState* state,const DawFxSnapshot* expected,const char* name,const char* id,const char* revision) {
    DawFxPresetResult result=guard(state,expected);if(result.action!=DAW_ACTION_UNCHANGED)return result;
    result.library=daw_fx_library_save(state->engine,name,&result.snapshot.chain,id,revision,&result.preset,&result.durable);
    result.has_preset=result.library==DAW_PRESET_OK;return result;
}
// Applies an explicit current library revision with independent effect identities and destination-tempo settings.
DawFxPresetResult daw_action_apply_fx_preset(AppState* state,const DawFxSnapshot* expected,const char* id,const char* revision) {
    DawFxPresetResult result=guard(state,expected);if(result.action!=DAW_ACTION_UNCHANGED)return result;
    if(!revision){result.library=DAW_PRESET_INVALID;return result;}
    result.library=daw_fx_library_get(state->engine,id,revision,&result.preset);if(result.library!=DAW_PRESET_OK)return result;
    result.has_preset=true;EngineFxChainRequest request={.op=ENGINE_FX_CHAIN_REPLACE,.restore=&result.preset.chain};
    result.action=daw_action_edit_fx(state,&result.snapshot,&request,&result.snapshot);return result;
}
