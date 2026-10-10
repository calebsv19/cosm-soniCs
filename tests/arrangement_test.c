#include "app_state.h"
#include "daw/arrangement.h"
#include "engine/engine_internal.h"
#include "input/timeline_selection.h"
#include "test_assert.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v,m) daw_test_expect("arrangement_test",(v),(m))
static bool reject_publication,reject_history;
static int publications;
// Injects refusal at the actual complete-content render publication boundary.
static bool publish(Engine* engine){++publications;return !reject_publication && engine_request_rebuild_sources(engine);}
#define engine_request_rebuild_sources publish
#include "../src/engine/engine_clip_history.c"
#undef engine_request_rebuild_sources
// Injects history reservation failure before any region content can change.
static void* reserve(void* p,size_t n){return reject_history?NULL:realloc(p,n);}
#define realloc reserve
#include "../src/undo/undo_manager_stack.c"
#undef realloc
// Builds private stopped lanes with inherited sound, notes and authored region automation.
static AppState* fixture(DawActionTarget targets[2]) {
    AppState* state=calloc(1,sizeof(*state));EngineRuntimeConfig cfg;config_set_defaults(&cfg);cfg.sample_rate=48000;
    state->engine=engine_create(&cfg);CHECK(state->engine != NULL,"engine");state->runtime_cfg=cfg;undo_manager_init(&state->undo);
    state->tempo=tempo_state_default(cfg.sample_rate);tempo_map_init(&state->tempo_map,cfg.sample_rate);time_signature_map_init(&state->time_signature_map);
    state->selected_track_index=state->selected_clip_index=-1;
    CHECK(engine_add_track(state->engine)==1,"destination track");
    EngineInstrumentParams params=engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS);params.level=.45f;
    CHECK(engine_track_midi_set_instrument_state(state->engine,0,ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS,params,true),"source sound");
    for(int i=0;i<2;++i){int c;CHECK(engine_add_midi_clip_to_track(state->engine,0,(uint64_t)i*96000,48000,&c),"source regions");
        EngineMidiNote note={0,12000,(uint8_t)(60+i*4),.6f};CHECK(engine_clip_midi_add_note(state->engine,0,c,note,NULL),"note");
        CHECK(engine_clip_add_automation_point(state->engine,0,c,ENGINE_AUTOMATION_TARGET_VOLUME,0,.8f,NULL),"automation");
        CHECK(daw_instrument_target_capture(state,0,c,&targets[i])==DAW_ACTION_UNCHANGED,"source identity");}
    publications=0;return state;
}
// Releases immutable history before private engine, timing and application ownership.
static void destroy(AppState* state){undo_manager_free(&state->undo);engine_destroy(state->engine);tempo_map_free(&state->tempo_map);time_signature_map_free(&state->time_signature_map);free(state);}
// Captures complete independent authored arrangement content.
static DawArrangementSnapshot* capture(AppState* state){DawArrangementSnapshot* snapshot=calloc(1,sizeof(*snapshot));CHECK(snapshot && daw_action_get_arrangement(state,snapshot).status==DAW_MIDI_UNCHANGED,"arrangement snapshot");return snapshot;}
// Retires one complete copied precondition safely.
static void release(DawArrangementSnapshot* snapshot){daw_arrangement_snapshot_free(snapshot);free(snapshot);}
// Qualifies copied sound/automation, independent identities, atomic swaps, trims/deletes and guarded history.
static void changes(void){DawActionTarget source[2],output[2];AppState* state=fixture(source);DawArrangementSnapshot* before=capture(state);
    DawArrangementEdit duplicate={ENGINE_ARRANGE_DUPLICATE,source[0],engine_get_tracks(state->engine)[1].runtime_id,192000,0};
    EngineBounceBuffer original={0},copied={0};CHECK(engine_bounce_range_to_buffer(state->engine,0,48000,NULL,NULL,&original),"source render");
    CHECK(daw_action_arrange(state,before,&duplicate,1,output).status==DAW_MIDI_APPLIED && publications==1 && state->undo.undo_count==1,"duplicate one publication/history");
    CHECK(output[0].clip_id!=source[0].clip_id && output[0].track_id!=source[0].track_id,"independent identity/lane");
    int t,c;CHECK(daw_action_target_resolve(state,output[0],&t,&c)==DAW_ACTION_UNCHANGED,"copied target");const EngineClip* clip=&engine_get_tracks(state->engine)[t].clips[c];
    CHECK(!clip->instrument_inherits_track && clip->instrument_preset==ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS && clip->instrument_params.level==.45f && clip->midi_notes.note_count==1 && clip->automation_lanes[0].points[0].value==.8f,"complete sound and automation");
    CHECK(engine_bounce_range_to_buffer(state->engine,192000,240000,NULL,NULL,&copied) && original.frame_count==copied.frame_count && !memcmp(original.data,copied.data,(size_t)original.frame_count*original.channels*sizeof(float)),"cross-lane inherited sound remains exact");engine_bounce_buffer_free(&original);engine_bounce_buffer_free(&copied);
    CHECK(!daw_arrangement_snapshot_current(state,before),"old content stale");CHECK(undo_manager_undo(&state->undo,state) && daw_arrangement_snapshot_current(state,before),"complete duplicate undo");
    CHECK(undo_manager_redo(&state->undo,state) && daw_action_target_resolve(state,output[0],&t,&c)==DAW_ACTION_UNCHANGED,"redo retains accepted identity");
    DawArrangementSnapshot* after=capture(state);DawArrangementEdit trim={ENGINE_ARRANGE_TRIM,output[0],0,0,24000};
    CHECK(daw_action_arrange(state,after,&trim,1,output).status==DAW_MIDI_APPLIED && engine_get_tracks(state->engine)[1].clips[0].duration_frames==24000,"right trim preserves notes");release(after);after=capture(state);trim.duration_frames=100;
    CHECK(daw_action_arrange(state,after,&trim,1,output).status==DAW_MIDI_INVALID,"trim cannot silently discard notes");
    DawArrangementEdit swaps[]={{ENGINE_ARRANGE_MOVE,source[0],0,96000,0},{ENGINE_ARRANGE_MOVE,source[1],0,0,0}};int calls=publications;
    CHECK(daw_action_arrange(state,after,swaps,2,output).status==DAW_MIDI_APPLIED && publications==calls+1 && state->undo.undo_count==3,"atomic simultaneous swap");release(after);after=capture(state);
    DawArrangementEdit remove={ENGINE_ARRANGE_DELETE,source[0],0,0,0};CHECK(daw_action_arrange(state,after,&remove,1,output).status==DAW_MIDI_APPLIED && !output[0].clip_id,"delete one step");
    CHECK(undo_manager_undo(&state->undo,state) && daw_arrangement_snapshot_current(state,after),"deletion restores complete content");
    release(after);release(before);destroy(state);
}
// Qualifies reservation/publication failure, content and history drift, no-op, busy and overlap refusal.
static void refusals(void){DawActionTarget source[2],output;AppState* state=fixture(source);DawArrangementSnapshot* before=capture(state);
    DawArrangementEdit move={ENGINE_ARRANGE_MOVE,source[0],0,48000,0};
    state->undo.undo_capacity=state->undo.undo_count;reject_history=true;CHECK(daw_action_arrange(state,before,&move,1,&output).status==DAW_MIDI_HISTORY_UNAVAILABLE && !publications && daw_arrangement_snapshot_current(state,before),"reservation failure preserves project");reject_history=false;
    reject_publication=true;CHECK(daw_action_arrange(state,before,&move,1,&output).status==DAW_MIDI_ENGINE_REJECTED && !state->undo.undo_count && !state->undo.active_drag_valid && daw_arrangement_snapshot_current(state,before),"publication rollback");reject_publication=false;
    move.start_frame=96000;CHECK(daw_action_arrange(state,before,&move,1,&output).status==DAW_MIDI_INVALID,"overlap refuses");move.start_frame=0;
    CHECK(daw_action_arrange(state,before,&move,1,&output).status==DAW_MIDI_UNCHANGED && !state->undo.undo_count,"exact no-op");
    state->midi_editor_ui.drag_active=true;CHECK(daw_action_arrange(state,before,&move,1,&output).status==DAW_MIDI_BUSY,"gesture ownership");state->midi_editor_ui.drag_active=false;
    CHECK(engine_clip_set_name(state->engine,0,0,"User work"),"outside rename");CHECK(daw_action_arrange(state,before,&move,1,&output).status==DAW_MIDI_STATE_CONFLICT,"guard includes region name");release(before);before=capture(state);
    move.start_frame=48000;CHECK(daw_action_arrange(state,before,&move,1,&output).status==DAW_MIDI_APPLIED,"move");
    CHECK(engine_clip_midi_set_instrument_preset(state->engine,0,0,ENGINE_INSTRUMENT_PRESET_FILTERED_PAD),"outside sound change");
    CHECK(!undo_manager_undo(&state->undo,state) && state->undo.undo_count==1,"history refuses unrecorded sound edits");
    release(before);destroy(state);
}
// Exercises the native selection entry point and identity selection across arrangement history.
static void native_selection(void) {
    DawActionTarget sources[2]; AppState* state=fixture(sources);
    CHECK(engine_remove_clip(state->engine,0,1),"native fixture clear copy destination");
    timeline_selection_set_single(state,0,0);
    CHECK(timeline_selection_duplicate(state),"native MIDI duplicate");
    CHECK(state->undo.undo_stack[0].type==UNDO_CMD_ARRANGEMENT && state->selection_count==1,"native common action adoption");
    uint64_t copied=engine_get_tracks(state->engine)[state->selected_track_index].clips[state->selected_clip_index].creation_index;
    CHECK(copied!=sources[0].clip_id,"native copy selected");
    CHECK(undo_manager_undo(&state->undo,state) && state->selection_count==1 &&
        engine_get_tracks(state->engine)[state->selected_track_index].clips[state->selected_clip_index].creation_index==sources[0].clip_id,"undo selects source");
    CHECK(undo_manager_redo(&state->undo,state) && state->selection_count==1 &&
        engine_get_tracks(state->engine)[state->selected_track_index].clips[state->selected_clip_index].creation_index==copied,"redo selects copy");
    timeline_selection_delete(state);
    CHECK(state->selection_count==0 && state->undo.undo_count==2,"native delete uses common history");
    CHECK(undo_manager_undo(&state->undo,state) && state->selection_count==1,"native deletion undo selects restored copy");
    destroy(state);
}
// Executes contained owner-thread arrangement qualification without user projects or audio devices.
int main(void){CHECK(!SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER),"SDL");changes();refusals();native_selection();SDL_Quit();puts("arrangement_test: success (atomic copy/move/swap/trim/delete, inherited sound/audio, independent IDs/automation, exact guarded history and failure rollback)");return 0;}
