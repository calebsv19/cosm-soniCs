#include "app_state.h"
#include "daw/midi_actions.h"
#include "daw/midi_patterns.h"
#include "ui/midi_pattern_panel.h"
#include "ui/font.h"
#include "engine/engine_internal.h"
#include "test_midi_editor_harness.h"
#include "test_assert.h"
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v,m) daw_test_expect("midi_authoring_test", (v), (m))
static bool reject_publish, reject_history, reject_copy;
static int publications;
// Injects an engine publication refusal at the actual transaction boundary.
static bool publish(Engine* engine) { ++publications; return !reject_publish && engine_request_rebuild_sources(engine); }
// Injects final history reservation failure before the project may change.
static void* history_realloc(void* ptr, size_t size) { return reject_history ? NULL : realloc(ptr,size); }
// Injects copied-snapshot preparation failure without changing production allocation paths.
static void* copy_malloc(size_t size) { return reject_copy ? NULL : malloc(size); }
#define engine_request_rebuild_sources publish
#include "../src/engine/engine_clips_midi.c"
#undef engine_request_rebuild_sources
#define realloc history_realloc
#include "../src/undo/undo_manager_stack.c"
#undef realloc
#define malloc copy_malloc
#include "../src/app/midi_actions.c"
#undef malloc
// Creates private stopped project state with one empty four-beat region.
static AppState* fixture(DawActionTarget* target) {
    AppState* state=calloc(1,sizeof(*state));EngineRuntimeConfig cfg;
    daw_midi_editor_test_state_init("midi_authoring_test",state,&cfg);
    CHECK(engine_add_midi_clip_to_track(state->engine,0,0,192000,NULL),"fixture region");
    state->selected_track_index=state->active_track_index=state->selected_clip_index=0;
    state->midi_editor_ui.selected_clip_creation_index=engine_get_tracks(state->engine)[0].clips[0].creation_index;
    CHECK(daw_instrument_target_capture(state,0,0,target)==DAW_ACTION_UNCHANGED,"target capture");publications=0;return state;
}
// Releases only private state and its owned history, avoiding personal project data.
static void destroy(AppState* state) { daw_midi_editor_test_state_destroy(state);free(state); }
// Captures an independent common snapshot for one explicit target.
static DawMidiSnapshot capture(AppState* state,DawActionTarget target) {
    DawMidiSnapshot out={0};CHECK(daw_action_get_midi(state,target,&out).status==DAW_MIDI_UNCHANGED,"copied snapshot");return out;
}
// Verifies atomic publication, one-step history, no-op behavior and independent caller storage.
static void transactions(void) {
    DawActionTarget target;AppState* state=fixture(&target);DawMidiSnapshot empty=capture(state,target);
    DawMidiPatternNotes pattern;CHECK(daw_midi_pattern_build(&empty,(DawMidiPattern){DAW_MIDI_CHORDS,60,0,.65f},&pattern).status==DAW_MIDI_UNCHANGED && pattern.count==12,"chord generation");
    DawMidiRequest request={&empty,pattern.notes,pattern.count,DAW_MIDI_REPLACE};
    CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_APPLIED && publications==1 && state->undo.undo_count==1 && !state->undo.active_drag_valid,"one publication and history");
    DawMidiSnapshot saved=capture(state,target);pattern.notes[0].note=127;
    CHECK(saved.note_count==12 && saved.notes[0].note==60 && state->undo.undo_stack[0].data.midi_note_edit.after_notes[0].note==60,"independent notes/history");
    request=(DawMidiRequest){&saved,saved.notes,saved.note_count,DAW_MIDI_REPLACE};int calls=publications;
    CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_UNCHANGED && publications==calls && state->undo.undo_count==1,"replacement no-op");
    state->midi_editor_ui.qwerty_record_armed=true;CHECK(!undo_manager_undo(&state->undo,state) && state->undo.undo_count==1,"armed replay refusal retains history");state->midi_editor_ui.qwerty_record_armed=false;
    CHECK(undo_manager_undo(&state->undo,state) && engine_get_tracks(state->engine)[0].clips[0].midi_notes.note_count==0,"single undo clears entire phrase");
    CHECK(undo_manager_redo(&state->undo,state) && engine_get_tracks(state->engine)[0].clips[0].midi_notes.note_count==12,"single redo restores entire phrase");
    DawMidiSnapshot latest=capture(state,target);EngineMidiNote appended={120000,10000,55,.5f};
    CHECK(daw_action_edit_midi(state,&(DawMidiRequest){&latest,&appended,1,DAW_MIDI_APPEND}).status==DAW_MIDI_APPLIED && state->undo.undo_count==2,"append preserves phrase");
    CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_STATE_CONFLICT && state->undo.undo_count==2,"stale notes refuse overwrite");
    CHECK(undo_manager_undo(&state->undo,state),"append undo");
    DawMidiSnapshot now=capture(state,target);CHECK(equal_notes(now.notes,now.note_count,saved.notes,saved.note_count),"append undo exact original");
    CHECK(daw_action_edit_midi(state,&(DawMidiRequest){&now,NULL,0,DAW_MIDI_REPLACE}).status==DAW_MIDI_APPLIED,"explicit clear is one transaction");
    CHECK(undo_manager_undo(&state->undo,state),"clear undo");
    CHECK(engine_clip_midi_add_note(state->engine,0,0,appended,NULL),"out-of-band note edit");int history=state->undo.undo_count;
    CHECK(!undo_manager_undo(&state->undo,state) && state->undo.undo_count==history,"replay refuses new notes without consuming history");
    daw_midi_snapshot_free(&now);daw_midi_snapshot_free(&latest);daw_midi_snapshot_free(&saved);daw_midi_snapshot_free(&empty);destroy(state);
}
// Verifies invalid payloads, busy state, history/copy failures and rejected engine publication are side-effect free.
static void refusals(void) {
    DawActionTarget target;AppState* state=fixture(&target);DawMidiSnapshot snapshot=capture(state,target);EngineMidiNote note={0,24000,60,.65f};
    DawMidiRequest request={&snapshot,&note,1,DAW_MIDI_APPEND};
    reject_history=true;CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_HISTORY_UNAVAILABLE && publications==0 && state->undo.undo_count==0,"history refusal before publication");reject_history=false;
    reject_copy=true;CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_ENGINE_REJECTED && publications==0,"copy failure before publication");reject_copy=false;
    reject_publish=true;CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_ENGINE_REJECTED && state->undo.undo_count==0 && !state->undo.active_drag_valid,"publication failure cancels reservation");reject_publish=false;
    CHECK(engine_get_tracks(state->engine)[0].clips[0].midi_notes.note_count==0,"failed publication preserves old notes");
    note.velocity=NAN;CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_INVALID,"nonfinite velocity");note.velocity=.65f;
    note.duration_frames=0;CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_INVALID,"zero duration");note.duration_frames=24000;
    note.start_frame=UINT64_MAX;CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_INVALID,"overflow start");note.start_frame=0;
    EngineMidiNote duplicates[]={note,note};CHECK(daw_action_edit_midi(state,&(DawMidiRequest){&snapshot,duplicates,2,DAW_MIDI_REPLACE}).status==DAW_MIDI_INVALID,"duplicate events");
    CHECK(daw_action_edit_midi(state,&(DawMidiRequest){&snapshot,&note,ENGINE_MIDI_NOTE_CAP+1,DAW_MIDI_APPEND}).status==DAW_MIDI_INVALID,"note count cap before payload read");
    state->midi_editor_ui.marquee_active=true;CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_BUSY,"marquee ownership");state->midi_editor_ui.marquee_active=false;
    state->bounce_active=true;CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_BUSY,"bounce conflict");state->bounce_active=false;
    state->midi_editor_ui.qwerty_record_armed=true;CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_BUSY,"record ownership");state->midi_editor_ui.qwerty_record_armed=false;
    CHECK(tempo_map_upsert_event(&state->tempo_map,1,90),"tempo edit");CHECK(daw_action_edit_midi(state,&request).status==DAW_MIDI_STATE_CONFLICT,"stale tempo");
    daw_midi_snapshot_free(&snapshot);destroy(state);
}
// Carries off-thread common read/write outcomes back to the owner test thread.
typedef struct ThreadCheck { AppState* state; DawMidiRequest request; DawMidiStatus read,write; } ThreadCheck;
// Attempts actions from a non-owner without accessing mutable clip lists.
static int other_thread(void* data) {
    ThreadCheck* check=data;DawMidiSnapshot out={0};check->read=daw_action_get_midi(check->state,check->request.expected->target,&out).status;check->write=daw_action_edit_midi(check->state,&check->request).status;return 0;
}
// Verifies identity survives array sorting and refuses removed targets or replaced projects.
static void identities(void) {
    DawActionTarget target;AppState* state=fixture(&target);DawMidiSnapshot snapshot=capture(state,target);EngineMidiNote note={0,10000,60,.5f};
    ThreadCheck check={state,{&snapshot,&note,1,DAW_MIDI_REPLACE},0,0};SDL_Thread* thread=SDL_CreateThread(other_thread,"midi-owner-test",&check);CHECK(thread!=NULL,"test thread");SDL_WaitThread(thread,NULL);
    CHECK(check.read==DAW_MIDI_WRONG_THREAD && check.write==DAW_MIDI_WRONG_THREAD && publications==0,"owner enforcement");
    CHECK(engine_add_midi_clip_to_track(state->engine,0,200000,48000,NULL),"neighbor");
    CHECK(engine_clip_set_timeline_start(state->engine,0,1,0,NULL),"neighbor sort");
    CHECK(daw_action_edit_midi(state,&check.request).status==DAW_MIDI_APPLIED,"target after sort");
    int t,c;CHECK(daw_action_target_resolve(state,target,&t,&c)==DAW_ACTION_UNCHANGED,"resolve sorted target");
    CHECK(engine_remove_clip(state->engine,t,c),"target removal");CHECK(daw_action_edit_midi(state,&check.request).status==DAW_MIDI_STALE_TARGET,"removed identity");
    int history=state->undo.undo_count;CHECK(!undo_manager_undo(&state->undo,state) && state->undo.undo_count==history,"removed history retained");
    Engine* old=state->engine;EngineRuntimeConfig cfg;config_set_defaults(&cfg);state->engine=engine_create(&cfg);
    CHECK(daw_action_edit_midi(state,&check.request).status==DAW_MIDI_STALE_TARGET && !undo_manager_undo(&state->undo,state),"project generation");engine_destroy(old);
    daw_midi_snapshot_free(&snapshot);destroy(state);
}
// Proves tempo-boundary mapping, major-key pitch membership and rejection of short/trimmed regions.
static void musical_timing(void) {
    DawActionTarget target;AppState* state=fixture(&target);CHECK(tempo_map_upsert_event(&state->tempo_map,2,60),"tempo boundary");DawMidiSnapshot snapshot=capture(state,target);DawMidiPatternNotes melody;
    CHECK(daw_midi_pattern_build(&snapshot,(DawMidiPattern){DAW_MIDI_MELODY,60,0,.65f},&melody).status==DAW_MIDI_UNCHANGED,"mapped phrase");
    CHECK(melody.notes[1].start_frame==12000 && melody.notes[4].start_frame==48000 && melody.notes[5].start_frame==72000,"tempo changes alter beat spacing");
    CHECK(melody.notes[0].duration_frames==10200 && melody.notes[4].duration_frames==20400,"gates map through tempo events");
    for(int i=0;i<melody.count;++i){int pitch=melody.notes[i].note%12;CHECK(pitch==0 || pitch==2 || pitch==4 || pitch==5 || pitch==7 || pitch==9 || pitch==11,"C major membership");}
    snapshot.duration_frames=1000;CHECK(daw_midi_pattern_build(&snapshot,(DawMidiPattern){DAW_MIDI_MELODY,60,0,.65f},&melody).status==DAW_MIDI_INVALID,"short region");snapshot.duration_frames=192000;
    snapshot.offset_frames=1;CHECK(daw_midi_pattern_build(&snapshot,(DawMidiPattern){DAW_MIDI_MELODY,60,0,.65f},&melody).status==DAW_MIDI_INVALID,"trimmed region explicit refusal");snapshot.offset_frames=0;
    CHECK(daw_midi_pattern_build(&snapshot,(DawMidiPattern){DAW_MIDI_CHORDS,120,0,.65f},&melody).status==DAW_MIDI_INVALID,"pitch overflow");
    daw_midi_snapshot_free(&snapshot);destroy(state);
}
// Exercises actual editor entry, explicit replacement, captured selection and append room checks.
static void workflow(void) {
    DawActionTarget target;AppState* state=fixture(&target);DawMidiPatternLayout layout;daw_midi_pattern_layout(state,&layout);
    CHECK(daw_midi_pattern_activate(state,layout.entry.x+1,layout.entry.y+1) && state->midi_pattern_ui.open,"open patterns");daw_midi_pattern_layout(state,&layout);
    CHECK(daw_midi_pattern_activate(state,layout.buttons[0].x+1,layout.buttons[0].y+1) && state->undo.undo_count==1,"append melody one command");
    CHECK(daw_midi_pattern_activate(state,layout.buttons[2].x+1,layout.buttons[2].y+1) && state->undo.undo_count==2,"append arpeggio second phrase");
    CHECK(daw_midi_pattern_activate(state,layout.buttons[1].x+1,layout.buttons[1].y+1) && state->undo.undo_count==2,"out-of-room preserves phrases");
    CHECK(daw_midi_pattern_activate(state,layout.buttons[3].x+1,layout.buttons[3].y+1) && state->midi_pattern_ui.replace,"explicit replace choice");
    CHECK(daw_midi_pattern_activate(state,layout.buttons[1].x+1,layout.buttons[1].y+1) && state->undo.undo_count==3,"replace chords one command");
    CHECK(engine_get_tracks(state->engine)[0].clips[0].midi_notes.note_count==12,"complete chord replacement");
    SDL_Event expression={.type=SDL_MOUSEBUTTONDOWN};expression.button.button=SDL_BUTTON_LEFT;
    expression.button.x=layout.buttons[7].x+1;expression.button.y=layout.buttons[7].y+1;
    CHECK(daw_midi_pattern_event(state,&expression) && state->undo.undo_count==3,"expression waits for matched release");
    SDL_Event cancel={.type=SDL_WINDOWEVENT};cancel.window.event=SDL_WINDOWEVENT_FOCUS_LOST;daw_midi_pattern_event(state,&cancel);
    expression.type=SDL_MOUSEBUTTONUP;CHECK(daw_midi_pattern_event(state,&expression) && state->undo.undo_count==3,"focus cancels expression press");
    expression.type=SDL_MOUSEBUTTONDOWN;CHECK(daw_midi_pattern_event(state,&expression),"expression press");expression.type=SDL_MOUSEBUTTONUP;
    CHECK(daw_midi_pattern_event(state,&expression) && state->undo.undo_count==4,"native expression shared action");
    SDL_Event escape={.type=SDL_KEYDOWN};escape.key.keysym.sym=SDLK_ESCAPE;CHECK(daw_midi_pattern_event(state,&escape) && !state->midi_pattern_ui.open,"close popup");
    CHECK(undo_manager_undo(&state->undo,state) && state->undo.undo_count==3,"expression one-step undo");
    destroy(state);
}
// Renders a three-instrument C-major arrangement and proves full undo/redo restores identical production samples.
static void listening(const char* directory) {
    DawActionTarget target;AppState* state=fixture(&target);state->tempo.bpm=100;CHECK(tempo_map_upsert_event(&state->tempo_map,0,100),"demo tempo");
    CHECK(engine_add_track(state->engine)==1 && engine_add_track(state->engine)==2,"demo tracks");
    const EngineInstrumentPresetId presets[]={ENGINE_INSTRUMENT_PRESET_ROUNDED_PAD,ENGINE_INSTRUMENT_PRESET_ROUNDED_KEYS,ENGINE_INSTRUMENT_PRESET_ROUNDED_PLUCK};
    const DawMidiPatternKind kinds[]={DAW_MIDI_CHORDS,DAW_MIDI_MELODY,DAW_MIDI_ARPEGGIO};const int roots[]={48,72,60};const char* names[]={"chords","melody","arpeggio"};
    for(int t=0;t<3;++t){if(t)CHECK(engine_add_midi_clip_to_track(state->engine,t,0,192000,NULL),"demo region");CHECK(daw_instrument_target_capture(state,t,0,&target)==DAW_ACTION_UNCHANGED,"demo target");
        CHECK(engine_clip_midi_set_instrument_preset(state->engine,t,0,presets[t]),"demo voice");
        DawMidiSnapshot snapshot=capture(state,target);DawMidiPatternNotes notes;CHECK(daw_midi_pattern_build(&snapshot,(DawMidiPattern){kinds[t],roots[t],0,.5f},&notes).status==DAW_MIDI_UNCHANGED,"demo pattern");
        CHECK(daw_action_edit_midi(state,&(DawMidiRequest){&snapshot,notes.notes,notes.count,DAW_MIDI_REPLACE}).status==DAW_MIDI_APPLIED,"demo transaction");daw_midi_snapshot_free(&snapshot);
        for(int other=0;other<3;++other)CHECK(engine_track_set_muted(state->engine,other,other!=t),"solo demo stem");
        EngineBounceBuffer stem={0};CHECK(engine_bounce_range_to_buffer(state->engine,0,192000,NULL,NULL,&stem),"stem production bounce");
        if(directory){char path[2048];snprintf(path,sizeof(path),"%s/%s.wav",directory,names[t]);CHECK(engine_bounce_write_wav(&stem,path,ENGINE_BOUNCE_WAV_PCM16),"stem WAV");}engine_bounce_buffer_free(&stem);
    }
    for(int t=0;t<3;++t){CHECK(engine_track_set_muted(state->engine,t,false),"unmute arrangement");CHECK(engine_track_set_gain(state->engine,t,.4f),"arrangement balance");}
    EngineBounceBuffer before={0},after={0};CHECK(engine_bounce_range_to_buffer(state->engine,0,192000,NULL,NULL,&before),"arrangement bounce");
    for(int i=0;i<3;++i)CHECK(undo_manager_undo(&state->undo,state),"arrangement undo");
    for(int t=0;t<3;++t)CHECK(engine_get_tracks(state->engine)[t].clips[0].midi_notes.note_count==0,"whole pattern undo");
    for(int i=0;i<3;++i)CHECK(undo_manager_redo(&state->undo,state),"arrangement redo");
    CHECK(engine_bounce_range_to_buffer(state->engine,0,192000,NULL,NULL,&after) && before.frame_count==after.frame_count && !memcmp(before.data,after.data,before.frame_count*before.channels*sizeof(float)),"exact rendered undo/redo parity");
    double peak=0,power=0;for(uint64_t i=0;i<before.frame_count*before.channels;++i){double v=before.data[i];CHECK(isfinite(v),"finite arrangement");if(fabs(v)>peak)peak=fabs(v);power+=v*v;}
    CHECK(peak>0 && peak<1 && power>0,"original-level arrangement without clipping");
    if(directory){char path[2048];snprintf(path,sizeof(path),"%s/c-major-arrangement.wav",directory);CHECK(engine_bounce_write_wav(&before,path,ENGINE_BOUNCE_WAV_PCM16),"arrangement WAV");printf("midi arrangement: peak %.8f RMS %.8f, exact undo/redo samples\n",peak,sqrt(power/(before.frame_count*before.channels)));}
    SessionDocument project;session_document_init(&project);CHECK(session_document_capture(state,&project),"arrangement project capture");
    if(directory){char path[2048];snprintf(path,sizeof(path),"%s/c-major-project.json",directory);CHECK(session_document_write_file(&project,path),"arrangement project write");session_document_free(&project);session_document_init(&project);CHECK(session_document_read_file(path,&project),"arrangement project read");}
    state->midi_pattern_ui.open=true;state->midi_pattern_ui.target=target;
    CHECK(session_apply_document(state,&project) && !state->midi_pattern_ui.open,"project reopen clears old popup ownership");session_document_free(&project);
    DawMidiSnapshot stale={0};CHECK(daw_action_get_midi(state,target,&stale).status==DAW_MIDI_STALE_TARGET,"reopen revokes old handle");
    EngineBounceBuffer reopened={0};CHECK(engine_bounce_range_to_buffer(state->engine,0,192000,NULL,NULL,&reopened) && before.frame_count==reopened.frame_count && !memcmp(before.data,reopened.data,before.frame_count*before.channels*sizeof(float)),"exact project reopen sound parity");
    engine_bounce_buffer_free(&reopened);engine_bounce_buffer_free(&before);engine_bounce_buffer_free(&after);destroy(state);
}
// Qualifies native staged arrangement fields, matching releases, stale refusal and explicit refresh.
static void arrangement_form(void) {
    DawActionTarget target; AppState* state=fixture(&target);state->inspector.visible=true;ui_layout_panes(state,1600,1000);
    DawMidiArrangementLayout layout;daw_midi_arrangement_layout(state,&layout);
    CHECK(daw_midi_arrangement_open(state),"open arrangement form");
    DawMidiArrangementUI* ui=&state->midi_arrangement_ui;
    SDL_strlcpy(ui->fields[0],"24000",sizeof(ui->fields[0]));
    SDL_Event event={0};event.type=SDL_MOUSEBUTTONUP;event.button.button=SDL_BUTTON_LEFT;event.button.x=layout.buttons[0].x+1;event.button.y=layout.buttons[0].y+1;
    CHECK(daw_midi_arrangement_event(state,&event) && !state->undo.undo_count,"unmatched release cannot move");
    event.type=SDL_MOUSEBUTTONDOWN;CHECK(daw_midi_arrangement_event(state,&event),"press move");event.type=SDL_MOUSEBUTTONUP;
    CHECK(daw_midi_arrangement_event(state,&event) && state->undo.undo_count==1 && engine_get_tracks(state->engine)[0].clips[0].timeline_start_frames==24000,"matching release moves exact frames");
    SDL_strlcpy(ui->fields[1],"96000",sizeof(ui->fields[1]));
    CHECK(daw_midi_arrangement_activate(state,layout.buttons[2].x+1,layout.buttons[2].y+1) && engine_get_tracks(state->engine)[0].clips[0].duration_frames==96000,"typed right trim");
    CHECK(engine_clip_set_name(state->engine,0,0,"Outside work"),"unrecorded change");SDL_strlcpy(ui->fields[0],"48000",sizeof(ui->fields[0]));
    CHECK(daw_midi_arrangement_activate(state,layout.buttons[0].x+1,layout.buttons[0].y+1) && state->undo.undo_count==2 && engine_get_tracks(state->engine)[0].clips[0].timeline_start_frames==24000,"staged form refuses changed content");
    CHECK(daw_midi_arrangement_activate(state,layout.buttons[4].x+1,layout.buttons[4].y+1),"explicit refresh");
    CHECK(daw_midi_arrangement_activate(state,layout.fields[0].x+1,layout.fields[0].y+1) && ui->focused==0,"shared text field focus");
    event=(SDL_Event){0};event.type=SDL_TEXTINPUT;SDL_strlcpy(event.text.text,"x",sizeof(event.text.text));CHECK(daw_midi_arrangement_event(state,&event),"text consumed");
    CHECK(daw_midi_arrangement_activate(state,layout.buttons[0].x+1,layout.buttons[0].y+1) && state->undo.undo_count==2,"invalid frame text refuses");
    CHECK(daw_midi_arrangement_activate(state,layout.buttons[4].x+1,layout.buttons[4].y+1),"refresh invalid text");
    SDL_strlcpy(ui->fields[0],"192000",sizeof(ui->fields[0]));
    CHECK(daw_midi_arrangement_activate(state,layout.buttons[1].x+1,layout.buttons[1].y+1) && state->undo.undo_count==3 && ui->target.clip_id!=target.clip_id,"copy selects independent target");
    CHECK(daw_midi_arrangement_activate(state,layout.buttons[3].x+1,layout.buttons[3].y+1) && !ui->open && !ui->captured && state->undo.undo_count==4,"delete releases native snapshot");
    CHECK(undo_manager_undo(&state->undo,state),"native arrangement deletion undo");
    CHECK(daw_midi_arrangement_open(state),"reopen restored copy");event=(SDL_Event){0};event.type=SDL_WINDOWEVENT;event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    daw_midi_arrangement_event(state,&event);CHECK(!ui->open && !ui->captured,"focus loss releases guard");
    destroy(state);
}
// Runs private common-action, musical timing, editor and production-listening checks without device output.
int main(int argc,char** argv) {
    CHECK(!SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER) && !TTF_Init(),"runtime");transactions();refusals();identities();musical_timing();workflow();arrangement_form();listening(argc>1?argv[1]:NULL);ui_font_shutdown();TTF_Quit();SDL_Quit();
    puts("midi_authoring_test: success (atomic history, stale state/identity, failures, tempo/key patterns, editor workflow and exact rendered undo/redo)");return 0;
}
