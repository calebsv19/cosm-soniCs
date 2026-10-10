#include "app_state.h"
#include "engine/engine_internal.h"
#include "daw/midi_regions.h"
#include "test_assert.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v,m) daw_test_expect("midi_regions_test",(v),(m))
static bool reject_publication,reject_history;
static int publications;
// Rejects the actual scalar/addition/removal engine publication boundary without changing production APIs.
static bool publish(Engine* engine){++publications;return !reject_publication && engine_request_rebuild_sources(engine);}
#define engine_request_rebuild_sources publish
#include "../src/engine/engine_clips.c"
#include "../src/engine/engine_tracks.c"
#undef engine_request_rebuild_sources
// Rejects reserved history storage before any structural mutation.
static void* reserve(void* p,size_t n){return reject_history?NULL:realloc(p,n);}
#define realloc reserve
#include "../src/undo/undo_manager_stack.c"
#undef realloc
// Creates an entirely empty private project on its owning control thread.
static AppState* fixture(void){AppState* s=calloc(1,sizeof(*s));EngineRuntimeConfig c;config_set_defaults(&c);s->engine=engine_create(&c);
    CHECK(s->engine!=NULL,"engine");while(engine_get_track_count(s->engine))CHECK(engine_remove_track(s->engine,engine_get_track_count(s->engine)-1),"empty fixture");undo_manager_init(&s->undo);tempo_map_init(&s->tempo_map,c.sample_rate);time_signature_map_init(&s->time_signature_map);s->tempo=tempo_state_default(c.sample_rate);return s;}
// Releases private history, engine and tempo ownership after each case.
static void destroy(AppState* s){undo_manager_free(&s->undo);engine_destroy(s->engine);tempo_map_free(&s->tempo_map);time_signature_map_free(&s->time_signature_map);free(s);}
// Captures a fresh independently owned bounded placement precondition.
static DawMidiLayout* layout(AppState* s){DawMidiLayout* l=malloc(sizeof(*l));CHECK(daw_action_get_midi_layout(s,l).status==DAW_MIDI_UNCHANGED,"layout");return l;}
// Captures current MIDI state for exact note and right-edge edits.
static DawMidiSnapshot snapshot(AppState* s,DawActionTarget t){DawMidiSnapshot m={0};CHECK(daw_action_get_midi(s,t,&m).status==DAW_MIDI_UNCHANGED,"notes snapshot");return m;}
// Qualifies empty creation, complete history ordering and retained IDs across structural replay.
static void chain(void){AppState* s=fixture();DawMidiLayout* l=layout(s);DawActionTarget t={0};int before=publications;
    CHECK(!l->track_count,"zero-track project");CHECK(daw_action_create_midi_region(s,l,NULL,0,96000,&t).status==DAW_MIDI_APPLIED,"create");
    CHECK(publications==before+1 && s->undo.undo_count==1 && engine_get_track_count(s->engine)==1,"one publication/history track+clip");
    DawMidiSnapshot m=snapshot(s,t);EngineMidiNote n={0,24000,60,.5f};DawMidiRequest edit={&m,&n,1,DAW_MIDI_APPEND};CHECK(daw_action_edit_midi(s,&edit).status==DAW_MIDI_APPLIED,"notes");daw_midi_snapshot_free(&m);
    m=snapshot(s,t);CHECK(daw_action_extend_midi_region(s,&m,192000).status==DAW_MIDI_APPLIED,"extend");CHECK(daw_action_extend_midi_region(s,&m,192000).status==DAW_MIDI_STATE_CONFLICT,"stale extension");daw_midi_snapshot_free(&m);
    CHECK(undo_manager_undo(&s->undo,s),"undo extension");CHECK(engine_get_tracks(s->engine)[0].clips[0].duration_frames==96000,"old bound");
    CHECK(undo_manager_undo(&s->undo,s),"undo notes");CHECK(undo_manager_undo(&s->undo,s),"undo creation");CHECK(!engine_get_track_count(s->engine),"empty again");
    CHECK(undo_manager_redo(&s->undo,s),"redo creation");m=snapshot(s,t);CHECK(!m.note_count,"same identity restored");daw_midi_snapshot_free(&m);
    CHECK(undo_manager_redo(&s->undo,s) && undo_manager_redo(&s->undo,s),"redo notes/extension");m=snapshot(s,t);CHECK(m.note_count==1 && m.duration_frames==192000,"complete replay");
    CHECK(daw_action_extend_midi_region(s,&m,192000).status==DAW_MIDI_UNCHANGED && s->undo.undo_count==3,"no-op history");
    daw_midi_snapshot_free(&m);free(l);destroy(s);
}
// Exercises prepublication failures, placement/busy refusal and conservative removal of creations.
static void refusals(void){AppState* s=fixture();DawMidiLayout* l=layout(s);DawActionTarget t={0};
    reject_history=true;CHECK(daw_action_create_midi_region(s,l,NULL,0,96000,&t).status==DAW_MIDI_HISTORY_UNAVAILABLE,"history failure");reject_history=false;
    reject_publication=true;CHECK(daw_action_create_midi_region(s,l,NULL,0,96000,&t).status==DAW_MIDI_ENGINE_REJECTED,"creation publication failure");reject_publication=false;
    CHECK(!engine_get_track_count(s->engine) && !s->undo.undo_count,"failed creation unchanged");
    CHECK(daw_action_create_midi_region(s,l,NULL,UINT64_MAX,2,&t).status==DAW_MIDI_INVALID,"overflow");
    s->bounce_active=true;CHECK(daw_action_create_midi_region(s,l,NULL,0,96000,&t).status==DAW_MIDI_BUSY,"busy");s->bounce_active=false;
    CHECK(daw_action_create_midi_region(s,l,NULL,0,96000,&t).status==DAW_MIDI_APPLIED,"create");
    CHECK(daw_action_create_midi_region(s,l,NULL,0,96000,&t).status==DAW_MIDI_STATE_CONFLICT,"stale layout");free(l);
    DawActionTarget track;CHECK(daw_instrument_target_capture(s,0,-1,&track)==DAW_ACTION_UNCHANGED,"track handle");l=layout(s);DawActionTarget other;
    CHECK(daw_action_create_midi_region(s,l,&track,48000,96000,&other).status==DAW_MIDI_INVALID,"overlap");
    DawMidiSnapshot m=snapshot(s,t);reject_publication=true;CHECK(daw_action_extend_midi_region(s,&m,192000).status==DAW_MIDI_ENGINE_REJECTED,"extend publication failure");reject_publication=false;
    CHECK(engine_get_tracks(s->engine)[0].clips[0].duration_frames==96000 && s->undo.undo_count==1,"extension failure retained");
    CHECK(daw_action_create_midi_region(s,l,&track,192000,96000,&other).status==DAW_MIDI_APPLIED,"neighbor");
    CHECK(daw_action_extend_midi_region(s,&m,192000).status==DAW_MIDI_APPLIED,"touch boundary");daw_midi_snapshot_free(&m);m=snapshot(s,t);
    CHECK(daw_action_extend_midi_region(s,&m,192001).status==DAW_MIDI_INVALID,"cross neighbor");CHECK(daw_action_extend_midi_region(s,&m,1).status==DAW_MIDI_INVALID,"no shortening");
    CHECK(undo_manager_undo(&s->undo,s) && undo_manager_undo(&s->undo,s),"undo extension/neighbor");
    char original_name[sizeof(engine_get_tracks(s->engine)[0].name)];memcpy(original_name,engine_get_tracks(s->engine)[0].name,sizeof(original_name));
    CHECK(engine_track_set_name(s->engine,0,"User work"),"rename track");CHECK(!undo_manager_undo(&s->undo,s) && s->undo.undo_count==1,"retain unrelated track work");
    memcpy(engine_get_track_mutable(s->engine,0)->name,original_name,sizeof(original_name));reject_publication=true;CHECK(!undo_manager_undo(&s->undo,s),"remove publication failure");reject_publication=false;
    CHECK(engine_get_track_count(s->engine)==1 && s->undo.undo_count==1,"history retained");CHECK(undo_manager_undo(&s->undo,s),"remove creation");
    reject_publication=true;CHECK(!undo_manager_redo(&s->undo,s),"redo publication failure");reject_publication=false;CHECK(!engine_get_track_count(s->engine) && s->undo.redo_count==3,"redo state retained");
    CHECK(undo_manager_redo(&s->undo,s),"retry same command");daw_midi_snapshot_free(&m);free(l);destroy(s);
}
// Verifies first-region undo preserves an existing track and restores its disabled instrument state.
static void existing(void){AppState* s=fixture();CHECK(engine_add_track(s->engine)==0,"existing empty track");DawMidiLayout* l=layout(s);DawActionTarget track,t;
    daw_instrument_target_capture(s,0,-1,&track);CHECK(!engine_get_tracks(s->engine)[0].midi_instrument_enabled,"before disabled");
    CHECK(daw_action_create_midi_region(s,l,&track,0,96000,&t).status==DAW_MIDI_APPLIED,"existing creation");CHECK(undo_manager_undo(&s->undo,s),"existing undo");
    CHECK(engine_get_track_count(s->engine)==1 && !engine_get_tracks(s->engine)[0].clip_count && !engine_get_tracks(s->engine)[0].midi_instrument_enabled,"restore metadata");
    CHECK(undo_manager_redo(&s->undo,s),"existing redo");DawMidiSnapshot m=snapshot(s,t);daw_midi_snapshot_free(&m);free(l);destroy(s);
}
// Carries private expectations for a wrong-thread read/write refusal test.
typedef struct ThreadCase{AppState* state;DawMidiLayout* expected;} ThreadCase;
// Confirms mutable project inspection remains on the creating control thread.
static int wrong_thread(void* p){ThreadCase* c=p;DawMidiLayout* l=malloc(sizeof(*l));DawActionTarget t;
    CHECK(daw_action_get_midi_layout(c->state,l).status==DAW_MIDI_WRONG_THREAD,"off-thread read");
    CHECK(daw_action_create_midi_region(c->state,c->expected,NULL,0,96000,&t).status==DAW_MIDI_WRONG_THREAD,"off-thread creation");free(l);return 0;}
// Runs structural qualification without user projects or an audio device.
int main(void){CHECK(SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER)==0,"SDL");chain();refusals();existing();AppState* s=fixture();DawMidiLayout* l=layout(s);ThreadCase c={s,l};SDL_Thread* t=SDL_CreateThread(wrong_thread,"region ownership",&c);CHECK(t!=NULL,"thread");SDL_WaitThread(t,NULL);free(l);destroy(s);SDL_Quit();puts("midi_regions_test: success (empty creation, one publication/history, exact identity replay, failures, overlap/busy, extension, existing metadata and wrong-thread refusal)");return 0;}
