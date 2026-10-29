#define _XOPEN_SOURCE 700
#include "test_temp_path.h"
#include "app_state.h"
#include "session/project_collections.h"
#include "input/project_modal_input.h"
#include "ui/project_modal_controls.h"
#include "daw/save_file.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

// Creates only fixture directories under a task-owned temporary working directory.
static void mkdir_ok(const char* p) {assert(!mkdir(p,0700));}
// Reads a tiny marker without replacing or redirecting its bytes.
static void read_marker(char* p) {FILE* f=fopen("config/projects/last_project.txt","rb");assert(f);size_t n=fread(p,1,SESSION_PATH_MAX-1,f);p[n]=0;assert(!fclose(f));}
// Chooses a named collection using the same scope identity exposed by the UI.
static void choose(AppState* s,const char* label) {for(int i=0;i<s->project_collections.count;++i)if(!strcmp(label,s->project_collections.items[i].label)){assert(project_collections_select(s,i));return;}assert(0);}
// Sends press/release to production modal input, including stale/mismatched activation cases.
static void click(AppState* s,int button) {DawProjectModalLayout l;daw_project_modal_layout(s,&l);SDL_Event e={.type=SDL_MOUSEBUTTONDOWN};e.button.button=SDL_BUTTON_LEFT;e.button.x=l.buttons[button].x+5;e.button.y=l.buttons[button].y+5;project_modal_input_handle_event(s,&e);e.type=SDL_MOUSEBUTTONUP;project_modal_input_handle_event(s,&e);}
// Qualifies scoped lists, safe browsing, monotonic checkpoints, no-replace races and exact current saves.
int main(void) {
    char oldcwd[4096];assert(getcwd(oldcwd,sizeof(oldcwd)));DAW_TEST_TEMP_PATH(root, "sonics-collection-test-XXXXXX");assert(mkdtemp(root));assert(!chdir(root));
    mkdir_ok("config");mkdir_ok("config/projects");mkdir_ok("out");mkdir_ok("out/projects");mkdir_ok("music_projects");
    mkdir_ok("music_projects/classical_conversations");mkdir_ok("music_projects/classical_conversations/projects");
    mkdir_ok("music_projects/night_pocket");mkdir_ok("music_projects/night_pocket/projects");
    AppState* s=calloc(1,sizeof(*s));assert(s);config_set_defaults(&s->runtime_cfg);s->tempo=tempo_state_default(s->runtime_cfg.sample_rate);
    s->engine=engine_create(&s->runtime_cfg);assert(s->engine);tempo_map_init(&s->tempo_map,s->runtime_cfg.sample_rate);time_signature_map_init(&s->time_signature_map);
    undo_manager_init(&s->undo);s->timeline_visible_seconds=10;s->timeline_vertical_scale=1;s->selected_track_index=s->selected_clip_index=-1;
    strcpy(s->data_paths.output_root,"out");s->window_width=800;s->window_height=600;
    int clip=-1;assert(engine_add_midi_clip_to_track(s->engine,0,0,24000,&clip));
    EngineMidiNote note={.note=60,.velocity=.7f,.start_frame=0,.duration_frames=20000};
    assert(engine_clip_midi_add_note(s->engine,0,clip,note,NULL));
    EngineBounceBuffer before_audio={0};EngineBounceOptions options=engine_bounce_options_default();options.normalize_if_clipping=false;
    assert(engine_bounce_range_to_buffer_with_options(s->engine,0,24000,&options,NULL,NULL,&before_audio));
    assert(project_manager_save(s,"Night_Pocket",true));char original[SESSION_PATH_MAX];strcpy(original,s->project.path);
    char marker[SESSION_PATH_MAX];read_marker(marker);Engine* engine=s->engine;int tracks=engine_get_track_count(engine);
    assert(project_collections_refresh(s,"music_projects"));choose(s,"classical_conversations");
    assert(s->engine==engine && tracks==engine_get_track_count(engine) && !strcmp(s->project.path,original));
    char after[SESSION_PATH_MAX];read_marker(after);assert(!strcmp(marker,after));
    ProjectInfo items[64];int count=-1;assert(project_collections_list(s,items,64,&count) && count==0);
    assert(!project_collections_refresh(s,"missing") && !strcmp(s->project.path,original));
    char next[SESSION_PATH_MAX];assert(project_collections_next_path(s,"Classical_Conversations_v001_ThemeStudy",next,sizeof(next)));assert(strstr(next,"_v001_ThemeStudy.json"));
    assert(project_collections_save_version(s,"Classical_Conversations_v001_ThemeStudy"));char v1[SESSION_PATH_MAX];strcpy(v1,s->project.path);
    s->tempo.bpm=108;TempoEvent tempo={.beat=0,.bpm=108};assert(tempo_map_set_events(&s->tempo_map,&tempo,1));assert(project_collections_save_version(s,s->project.name));assert(strstr(s->project.path,"_v002_ThemeStudy.json"));
    SessionDocument doc;session_document_init(&doc);assert(session_document_read_file(v1,&doc) && doc.tempo.bpm==120);session_document_free(&doc);
    assert(project_collections_list(s,items,64,&count) && count==2 && strstr(items[0].name,"v001"));
    FILE* f=fopen("music_projects/classical_conversations/brief.json","wb");assert(f);fputs("{}",f);fclose(f);
    f=fopen("music_projects/classical_conversations/projects/broken.json","wb");assert(f);fputs("{broken",f);fclose(f);
    assert(!symlink(v1,"music_projects/classical_conversations/projects/alias.json"));
    assert(project_collections_list(s,items,64,&count) && count==2);
    assert(project_collections_next_path(s,s->project.name,next,sizeof(next)));session_document_init(&doc);assert(session_document_capture(s,&doc));
    assert(session_document_write_file_new(&doc,next));assert(!session_document_write_file_new(&doc,next));session_document_free(&doc);
    assert(project_collections_next_path(s,s->project.name,next,sizeof(next)) && strstr(next,"v004"));
    choose(s,"night_pocket");assert(project_collections_list(s,items,64,&count) && !count);
    char current[SESSION_PATH_MAX];strcpy(current,s->project.path);assert(project_manager_save(s,s->project.name,true));assert(!strcmp(current,s->project.path));
    assert(project_modal_input_open_load_modal(s) && s->project_load.active && !s->project_load.count);
    int selected=s->project_collections.selected;DawProjectModalLayout l;daw_project_modal_layout(s,&l);
    SDL_Event e={.type=SDL_MOUSEBUTTONDOWN};e.button.button=SDL_BUTTON_LEFT;e.button.x=l.buttons[2].x+4;e.button.y=l.buttons[2].y+4;project_modal_input_handle_event(s,&e);
    e.type=SDL_MOUSEBUTTONUP;e.button.x=l.buttons[3].x+4;project_modal_input_handle_event(s,&e);assert(s->project_collections.selected==selected);
    click(s,2);assert(s->project_collections.selected!=selected && !strcmp(current,s->project.path));
    click(s,4);assert(s->project_prompt.folder_mode && !s->project_load.active);
    strcpy(s->project_prompt.buffer,"music_projects");e=(SDL_Event){.type=SDL_KEYDOWN};e.key.keysym.sym=SDLK_RETURN;project_modal_input_handle_event(s,&e);assert(s->project_load.active);
    project_modal_input_close_load_modal(s);project_modal_input_open_save_prompt(s);choose(s,"classical_conversations");
    strcpy(s->project_prompt.buffer,"Classical_Conversations_v001_ThemeStudy");click(s,0);assert(!s->project_prompt.active && strstr(s->project.path,"v004"));
    // A valid offline reload preserves the published tempo and complete engine structure.
    assert(project_manager_load(s,s->project.path));assert(s->tempo.bpm==108 && engine_get_track_count(s->engine)==tracks);
    EngineBounceBuffer after_audio={0};assert(engine_bounce_range_to_buffer_with_options(s->engine,0,24000,&options,NULL,NULL,&after_audio));
    assert(before_audio.frame_count==after_audio.frame_count && before_audio.channels==after_audio.channels);
    assert(!memcmp(before_audio.data,after_audio.data,(size_t)before_audio.frame_count*before_audio.channels*sizeof(float)));
    engine_bounce_buffer_free(&before_audio);engine_bounce_buffer_free(&after_audio);
    engine_destroy(s->engine);undo_manager_free(&s->undo);tempo_map_free(&s->tempo_map);time_signature_map_free(&s->time_signature_map);free(s);
    assert(!chdir(oldcwd));
    printf("project_collections_test: passed (isolated lists, browsing preserves song/markers, version suffixes, atomic no-replace, exact current save, modal releases and reload); fixture=%s\n",root);
    return 0;
}
