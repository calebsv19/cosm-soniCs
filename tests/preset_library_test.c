#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "app_state.h"
#include "cJSON.h"
#include "daw/preset_actions.h"
#include "engine/engine_internal.h"
#include "session.h"
#include "ui/preset_panel.h"
#include "ui/font.h"
#include "ui/midi_instrument_panel.h"
#include "test_midi_editor_harness.h"
#include "test_assert.h"
#include <SDL2/SDL_ttf.h>
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(v,m) daw_test_expect("preset_library_test",(v),(m))
// Removes only the fresh fixture directory and its known regular outputs.
static void cleanup(const char* path) {
    DIR* d=opendir(path); if (!d) return; struct dirent* e;
    while ((e=readdir(d))) {
        if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
        char p[2048]; snprintf(p,sizeof(p),"%s/%s",path,e->d_name); struct stat st;
        CHECK(!lstat(p,&st),"fixture stat"); if (S_ISDIR(st.st_mode)) cleanup(p); else CHECK(!unlink(p),"fixture unlink");
    }
    closedir(d); CHECK(!rmdir(path),"fixture rmdir");
}
// Reads bytes for independent deterministic PCM comparison and malformed file fixtures.
static unsigned char* read_bytes(const char* path, size_t* size) {
    FILE* f=fopen(path,"rb"); CHECK(f!=NULL,"read file"); CHECK(!fseek(f,0,SEEK_END),"seek");
    *size=(size_t)ftell(f); rewind(f); unsigned char* p=calloc(*size+1,1); CHECK(p!=NULL,"read allocation");
    CHECK(fread(p,1,*size,f)==*size,"read bytes"); fclose(f); return p;
}
// Writes deliberate malformed documents only inside this test's private root.
static void write_bytes(const char* path,const void* bytes,size_t size) {
    FILE* f=fopen(path,"wb"); CHECK(f!=NULL,"write fixture"); CHECK(fwrite(bytes,1,size,f)==size && !fclose(f),"write bytes");
}
// Drives the browser through real text and discrete control input without a renderer.
static void ui_workflow(AppState* state) {
    state->selected_track_index=state->active_track_index=0; state->selected_clip_index=0;
    state->midi_editor_ui.panel_mode=MIDI_REGION_PANEL_INSTRUMENT;
    MidiInstrumentPanelLayout p; midi_instrument_panel_compute_layout(state,&p);
    DawPresetPanelLayout l; daw_preset_panel_layout(state,p.panel_rect,p.header_rect,&l);
    CHECK(daw_preset_panel_activate(state,l.save_button.x+1,l.save_button.y+1),"open Save As");
    CHECK(state->preset_ui.save_open && state->preset_ui.editing,"save text owner");
    SDL_strlcpy(state->preset_ui.name,"Browser Sound",sizeof(state->preset_ui.name)); state->preset_ui.cursor=13;
    SDL_Event enter={.type=SDL_KEYDOWN}; enter.key.keysym.sym=SDLK_RETURN;
    CHECK(daw_preset_panel_event(state,&enter) && !state->preset_ui.save_open,"save via Enter");
    CHECK(daw_preset_panel_activate(state,l.library_button.x+1,l.library_button.y+1),"open browser");
    SDL_Event text={.type=SDL_TEXTINPUT}; SDL_strlcpy(text.text.text,"Browser",sizeof(text.text.text));
    CHECK(daw_preset_panel_event(state,&text),"browser search input");
    daw_preset_panel_layout(state,p.panel_rect,p.header_rect,&l); CHECK(l.row_count==1,"filtered browser");
    CHECK(daw_preset_panel_event(state,&enter) && !state->preset_ui.open,"load via Enter");
    CHECK(state->preset_ui.has_loaded && !strcmp(state->preset_ui.loaded.name,"Browser Sound"),"loaded custom label");
    DawActionTarget target; CHECK(daw_instrument_target_capture(state,0,0,&target)==DAW_ACTION_UNCHANGED,"label target");
    DawInstrumentSnapshot sound=daw_action_get_instrument(state,target).snapshot; bool modified;
    CHECK(!strcmp(daw_preset_panel_sound_name(state,&sound,&modified),"Browser Sound") && !modified,"saved marker");
    sound.effective_params.tone+=.01f;
    CHECK(!strcmp(daw_preset_panel_sound_name(state,&sound,&modified),"Browser Sound") && modified,"edited marker");
}
// Exercises portable storage, optimistic updates, project isolation, finite audition, recall and UI ownership.
int main(void) {
    CHECK(!SDL_Init(SDL_INIT_EVENTS) && !TTF_Init(),"SDL events and font library");
    char directory[]="/tmp/daw-preset-library-XXXXXX"; CHECK(mkdtemp(directory)!=NULL,"private fixture");
    char root[1024]; snprintf(root,sizeof(root),"%s/library",directory); CHECK(!setenv("DAW_PRESET_LIBRARY",root,1),"library override");
    DawPresetList list; CHECK(daw_preset_library_list(&list)==DAW_PRESET_OK && list.count==0 && access(root,F_OK),"read must not create library");
    EngineInstrumentPresetId base=ENGINE_INSTRUMENT_PRESET_FILTERED_PAD;
    EngineInstrumentParams params=engine_instrument_default_params(base); params.cutoff_hz=1379.75f; params.vibrato_depth=7.25f;
    DawUserPreset a,b,updated,read; bool durable;
    CHECK(daw_preset_library_save("Evening / Glass é",base,params,NULL,NULL,&a,&durable)==DAW_PRESET_OK,"portable name");
    CHECK(daw_preset_library_get(a.id,a.revision,&read)==DAW_PRESET_OK && daw_preset_sound_equal(&read,base,params),"all settings exact roundtrip");
    CHECK(daw_preset_library_save(a.name,base,params,NULL,NULL,&b,&durable)==DAW_PRESET_OK && strcmp(a.id,b.id),"duplicate label stable IDs");
    // Simulates an existing version-one library document with its complete fourteen-parameter sound.
    char legacy_path[2048]; snprintf(legacy_path,sizeof(legacy_path),"%s/%s.sonics-preset.json",root,a.id);
    size_t legacy_size; unsigned char* original=read_bytes(legacy_path,&legacy_size);
    cJSON* legacy=cJSON_Parse((char*)original); CHECK(legacy!=NULL,"legacy JSON");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(legacy,"version"),1);
    cJSON* settings=cJSON_GetObjectItemCaseSensitive(legacy,"parameters");
    for(int id=14;id<ENGINE_INSTRUMENT_PARAM_COUNT;++id){EngineInstrumentParamSpec spec;engine_instrument_param_spec(id,&spec);cJSON_DeleteItemFromObjectCaseSensitive(settings,spec.id);}
    char* encoded=cJSON_PrintUnformatted(legacy); CHECK(encoded!=NULL,"legacy encoding"); write_bytes(legacy_path,encoded,strlen(encoded));
    CHECK(daw_preset_library_get(a.id,NULL,&read)==DAW_PRESET_OK && read.params.filter_env_octaves==0 && read.params.filter_lfo_octaves==0,"old preset recalls without modulation");
    write_bytes(legacy_path,original,legacy_size); free(original); cJSON_free(encoded); cJSON_Delete(legacy);
    params.cutoff_hz=1900;
    CHECK(daw_preset_library_save("Renamed Glass",base,params,a.id,a.revision,&updated,&durable)==DAW_PRESET_OK && !strcmp(a.id,updated.id) && strcmp(a.revision,updated.revision),"explicit revision update");
    CHECK(daw_preset_library_save("Stale",base,params,a.id,a.revision,&read,&durable)==DAW_PRESET_CONFLICT,"stale update refused");
    char path[2048],backup[2048]; snprintf(path,sizeof(path),"%s/%s.sonics-preset.json",root,a.id); snprintf(backup,sizeof(backup),"%s.previous",path);
    size_t n; unsigned char* bytes=read_bytes(backup,&n); CHECK(strstr((char*)bytes,a.revision)!=NULL,"recoverable prior revision"); free(bytes);
    CHECK(daw_preset_library_get("../outside",NULL,&read)==DAW_PRESET_INVALID,"filename isolation");
    params.level=NAN; CHECK(daw_preset_library_save("Invalid",base,params,NULL,NULL,&read,&durable)==DAW_PRESET_INVALID,"NaN refused"); params=updated.params;
    char lockpath[2048]; snprintf(lockpath,sizeof(lockpath),"%s/.library.lock",root); int lock=open(lockpath,O_RDWR); CHECK(lock>=0 && !flock(lock,LOCK_EX|LOCK_NB),"lock fixture");
    CHECK(daw_preset_library_save("Busy",base,params,NULL,NULL,&read,&durable)==DAW_PRESET_BUSY,"cooperating concurrent writer refused"); close(lock);
    bytes=read_bytes(path,&n); char* v=strstr((char*)bytes,"\"synthesis_version\":1"); CHECK(v!=NULL,"version fixture"); v[strlen("\"synthesis_version\":")]='2'; write_bytes(path,bytes,n);
    CHECK(daw_preset_library_get(a.id,NULL,&read)==DAW_PRESET_UNSUPPORTED,"future behavior refused"); v[strlen("\"synthesis_version\":")]='1'; write_bytes(path,bytes,n); free(bytes);
    char linked[2048]; snprintf(linked,sizeof(linked),"%s/00000000000000000000000000000000.sonics-preset.json",root); CHECK(!symlink(path,linked),"linked fixture");
    CHECK(daw_preset_library_get("00000000000000000000000000000000",NULL,&read)==DAW_PRESET_IO,"symlink refused");
    CHECK(daw_preset_library_list(&list)==DAW_PRESET_OK && list.count==2 && list.invalid_count==1,"invalid file reported");
    AppState* state=calloc(1,sizeof(*state)); EngineRuntimeConfig cfg; daw_midi_editor_test_state_init("preset_library_test",state,&cfg);
    CHECK(engine_add_midi_clip_to_track(state->engine,0,0,144000,NULL),"project MIDI region");
    CHECK(engine_clip_midi_add_note(state->engine,0,0,(EngineMidiNote){0,72000,60,.5f},NULL),"project note");
    DawActionTarget target; CHECK(daw_instrument_target_capture(state,0,0,&target)==DAW_ACTION_UNCHANGED,"owned target");
    DawInstrumentSnapshot before=daw_action_get_instrument(state,target).snapshot;
    CHECK(daw_action_apply_preset(state,target,b.id,b.revision).status==DAW_ACTION_APPLIED && state->undo.undo_count==1,"common apply history");
    DawInstrumentSnapshot sound=daw_action_get_instrument(state,target).snapshot;
    CHECK(daw_preset_sound_equal(&b,sound.effective_preset,sound.effective_params),"complete project copy");
    CHECK(undo_manager_undo(&state->undo,state) && daw_action_get_instrument(state,target).snapshot.stored.inherits_track==before.stored.inherits_track,"apply undo restores inheritance");
    CHECK(undo_manager_redo(&state->undo,state),"apply redo");
    CHECK(daw_action_save_preset(state,&before,"Stale capture",NULL,NULL).status==DAW_PRESET_CONFLICT,"captured state conflict");
    int history=state->undo.undo_count;
    DawPresetSaveResult saved=daw_action_save_preset(state,&sound,"Project Glass",NULL,NULL);
    CHECK(saved.status==DAW_PRESET_OK && state->undo.undo_count==history,"library save outside project history");
    DawPresetPreview note,chord;
    CHECK(daw_action_preview_preset(state,b.id,b.revision,false,&note)==DAW_PRESET_OK && note.peak>0 && note.peak<1,"finite original-level note WAV");
    bytes=read_bytes(note.path,&n); CHECK(n==576044 && !memcmp(bytes,"RIFF",4),"PCM WAV structure");
    CHECK(daw_action_preview_preset(state,b.id,b.revision,false,&note)==DAW_PRESET_OK,"repeat note preview"); size_t n2; unsigned char* again=read_bytes(note.path,&n2);
    CHECK(n==n2 && !memcmp(bytes,again,n),"reproducible preview PCM"); free(again);
    CHECK(daw_action_preview_preset(state,b.id,b.revision,true,&chord)==DAW_PRESET_OK && chord.peak>0 && chord.peak<1,"finite original-level chord WAV");
    again=read_bytes(chord.path,&n2); CHECK(n==n2 && memcmp(bytes+44,again+44,n-44),"different chord excitation"); free(bytes); free(again);
    CHECK(state->undo.undo_count==history && engine_clip_midi_note_count(&engine_get_tracks(state->engine)[0].clips[0])==1,"preview preserves project");
    CHECK(daw_action_audition_instrument(state,target,true).status==DAW_ACTION_UNCHANGED,"live finite audition");
    CHECK(state->engine->midi_audition_notes.note_count==4,"four audition voices");
    CHECK(daw_action_audition_instrument(state,target,true).status==DAW_ACTION_UNCHANGED && state->engine->midi_audition_notes.note_count==4,"repeat preview replaces voices without stacking level");
    for (int i=0;i<4;++i) CHECK(state->engine->midi_audition_notes.notes[i].duration_frames==72000,"sample-counted gate survives focus loss");
    engine_midi_audition_all_notes_off(state->engine);
    SessionDocument doc; session_document_init(&doc); CHECK(session_document_capture(state,&doc),"project capture");
    char project[2048]; snprintf(project,sizeof(project),"%s/project.json",directory); CHECK(session_document_write_file(&doc,project),"project write"); session_document_free(&doc);
    snprintf(path,sizeof(path),"%s/%s.sonics-preset.json",root,b.id); CHECK(!unlink(path),"library sound removed");
    CHECK(session_document_read_file(project,&doc),"project read without preset library entry"); CHECK(session_apply_document(state,&doc),"project reopen"); session_document_free(&doc);
    CHECK(daw_instrument_target_capture(state,0,0,&target)==DAW_ACTION_UNCHANGED,"reopened owned target"); sound=daw_action_get_instrument(state,target).snapshot;
    CHECK(daw_preset_sound_equal(&b,sound.effective_preset,sound.effective_params),"library-independent exact project recall");
    ui_workflow(state);
    daw_midi_editor_test_state_destroy(state); free(state); ui_font_shutdown(); TTF_Quit(); SDL_Quit(); cleanup(directory);
    puts("preset_library_test: success (portable settings, updates, guarded actions, finite auditions, reproducible WAVs, independent recall and browser input)"); return 0;
}
