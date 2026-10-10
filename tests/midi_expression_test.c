#include "daw/midi_expression.h"
#include "test_midi_editor_harness.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <SDL2/SDL_ttf.h>
#include "ui/font.h"
#define CHECK(v,m) daw_test_expect("midi_expression_test",(v),(m))
// Captures an independent production snapshot for exact transformation/history checks.
static DawMidiSnapshot capture(AppState* state,DawActionTarget target) {
    DawMidiSnapshot value={0};CHECK(daw_action_get_midi(state,target,&value).status==DAW_MIDI_UNCHANGED,"snapshot");return value;
}
// Exercises musical timing across tempo changes, exact history and deterministic expression refusals.
int main(void) {
    CHECK(!SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER) && !TTF_Init(),"runtime");
    AppState* state=calloc(1,sizeof(*state));EngineRuntimeConfig cfg;daw_midi_editor_test_state_init("midi_expression_test",state,&cfg);
    CHECK(tempo_map_upsert_event(&state->tempo_map,2,60),"tempo change");
    CHECK(engine_add_midi_clip_to_track(state->engine,0,24000,200000,NULL),"offset region");
    EngineMidiNote source[]={{14000,3000,60,.8f},{39000,4000,64,.4f},{88000,5000,67,.6f}};
    CHECK(engine_clip_midi_set_notes(state->engine,0,0,source,3),"source notes");DawActionTarget target;CHECK(daw_instrument_target_capture(state,0,0,&target)==DAW_ACTION_UNCHANGED,"target");
    DawMidiSnapshot before=capture(state,target);int history=state->undo.undo_count;
    DawMidiExpression quantize={.kind=DAW_MIDI_QUANTIZE,.grid_beats=.5,.strength=1,.swing=.5};
    CHECK(daw_action_transform_midi(state,&before,quantize).status==DAW_MIDI_APPLIED,"swung quantize");DawMidiSnapshot quantized=capture(state,target);
    CHECK(quantized.notes[0].start_frame==18000 && quantized.notes[1].start_frame==24000 && quantized.notes[2].start_frame==72000,"tempo-aware nearest swung grid anchored at region start");
    for(int i=0;i<3;++i)CHECK(quantized.notes[i].duration_frames==source[i].duration_frames && quantized.notes[i].velocity==source[i].velocity,"length and velocity preserved");
    CHECK(state->undo.undo_count==history+1 && daw_action_transform_midi(state,&before,quantize).status==DAW_MIDI_STATE_CONFLICT,"one history and stale refusal");
    CHECK(undo_manager_undo(&state->undo,state),"undo");DawMidiSnapshot undone=capture(state,target);CHECK(daw_midi_snapshot_equal(&before,&undone),"exact undo");daw_midi_snapshot_free(&undone);
    CHECK(undo_manager_redo(&state->undo,state),"redo");DawMidiSnapshot redone=capture(state,target);CHECK(daw_midi_snapshot_equal(&quantized,&redone),"exact redo");
    history=state->undo.undo_count;CHECK(daw_action_transform_midi(state,&redone,quantize).status==DAW_MIDI_UNCHANGED && state->undo.undo_count==history,"quantized no-op");
    DawMidiExpression humanize={.kind=DAW_MIDI_HUMANIZE,.timing_ms=8,.velocity_amount=.1,.seed=42};CHECK(daw_action_transform_midi(state,&redone,humanize).status==DAW_MIDI_APPLIED,"humanize");DawMidiSnapshot first=capture(state,target);
    for(int i=0;i<3;++i)CHECK(llabs((long long)first.notes[i].start_frame-(long long)redone.notes[i].start_frame)<=384 && fabs(first.notes[i].velocity-redone.notes[i].velocity)<=.100001,"bounded variation");
    CHECK(undo_manager_undo(&state->undo,state),"variation undo");CHECK(daw_action_transform_midi(state,&redone,humanize).status==DAW_MIDI_APPLIED,"same seed repeat");DawMidiSnapshot second=capture(state,target);CHECK(daw_midi_snapshot_equal(&first,&second),"deterministic seed");
    DawMidiExpression scale={.kind=DAW_MIDI_VELOCITY_SCALE,.velocity_scale=.5};CHECK(daw_action_transform_midi(state,&second,scale).status==DAW_MIDI_APPLIED,"velocity shape");DawMidiSnapshot scaled=capture(state,target);
    for(int i=0;i<3;++i)CHECK(scaled.notes[i].start_frame==second.notes[i].start_frame && scaled.notes[i].velocity==second.notes[i].velocity*.5f,"velocity only");
    history=state->undo.undo_count;state->midi_editor_ui.drag_active=true;CHECK(daw_action_transform_midi(state,&scaled,scale).status==DAW_MIDI_BUSY && state->undo.undo_count==history,"gesture refusal");state->midi_editor_ui.drag_active=false;
    quantize.strength=NAN;CHECK(daw_action_transform_midi(state,&scaled,quantize).status==DAW_MIDI_INVALID,"nonfinite refusal");
    scale.timing_ms=1;CHECK(daw_action_transform_midi(state,&scaled,scale).status==DAW_MIDI_INVALID,"irrelevant settings refusal");
    CHECK(tempo_map_upsert_event(&state->tempo_map,3,90),"outside tempo edit");CHECK(daw_action_transform_midi(state,&scaled,humanize).status==DAW_MIDI_STATE_CONFLICT,"tempo precondition");
    EngineMidiNote collisions[]={{100,1000,60,.4f},{200,1000,60,.5f}};
    CHECK(engine_clip_midi_set_notes(state->engine,0,0,collisions,2),"collision fixture");DawMidiSnapshot collide=capture(state,target);
    quantize=(DawMidiExpression){.kind=DAW_MIDI_QUANTIZE,.grid_beats=.5,.strength=1};
    CHECK(daw_action_transform_midi(state,&collide,quantize).status==DAW_MIDI_INVALID && state->undo.undo_count==history,"duplicate quantized notes reject atomically");
    quantize.strength=0;CHECK(daw_action_transform_midi(state,&collide,quantize).status==DAW_MIDI_UNCHANGED,"zero strength exact no-op");
    EngineMidiNote edge={0,1000,60,.5f};CHECK(engine_clip_midi_set_notes(state->engine,0,0,&edge,1),"edge fixture");DawMidiSnapshot boundary=capture(state,target);
    CHECK(daw_action_transform_midi(state,&boundary,humanize).status==DAW_MIDI_INVALID && state->undo.undo_count==history,"negative humanized timing refuses whole phrase");
    DawMidiSnapshot unchanged=capture(state,target);CHECK(daw_midi_snapshot_equal(&boundary,&unchanged),"boundary refusal preserves notes");
    daw_midi_snapshot_free(&unchanged);daw_midi_snapshot_free(&boundary);daw_midi_snapshot_free(&collide);
    daw_midi_snapshot_free(&scaled);daw_midi_snapshot_free(&second);daw_midi_snapshot_free(&first);daw_midi_snapshot_free(&redone);daw_midi_snapshot_free(&quantized);daw_midi_snapshot_free(&before);
    daw_midi_editor_test_state_destroy(state);free(state);ui_font_shutdown();TTF_Quit();SDL_Quit();puts("midi_expression_test: success (swung tempo-aware grid, bounded deterministic variation, velocity shape, exact one-step history and stale/busy/invalid refusals)");return 0;
}
