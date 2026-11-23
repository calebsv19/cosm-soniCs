#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#define _XOPEN_SOURCE 700
#include "app_state.h"
#include "audio/wav_writer.h"
#include "engine/engine_internal.h"
#include "input/inspector_input.h"
#include "input/inspector_input_numeric_edit.h"
#include "input/timeline_selection.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/layout_modal_overlays.h"
#include "ui/project_modal_controls.h"
#include "input/project_modal_input.h"
#include "vk_renderer.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int refuse_directory_sync;
// Injects only directory acknowledgement failure while retaining actual candidate writes.
static int proof_fsync(int fd) { struct stat st;if(refuse_directory_sync && !fstat(fd,&st)&&S_ISDIR(st.st_mode)){errno=EIO;return -1;}return fsync(fd); }
#define fsync proof_fsync
#include "../src/session/save_file.c"
#undef fsync

// Captures the actual native modal painters and requires clean Vulkan validation.
static void frame(VkRenderer* r,AppState* s,const char* prefix,const char* name) {
    char path[4096];snprintf(path,sizeof(path),"%s-%s.bmp",prefix,name);assert(vk_renderer_request_capture(r,path)==VK_SUCCESS);
    VkCommandBuffer cmd;VkFramebuffer fb;VkExtent2D extent;assert(vk_renderer_begin_frame(r,&cmd,&fb,&extent)==VK_SUCCESS);
    vk_renderer_set_logical_size(r,s->window_width,s->window_height);
    if(s->project_load.active)ui_render_project_load_overlay((SDL_Renderer*)r,s);else ui_render_project_prompt_overlay((SDL_Renderer*)r,s);
    assert(vk_renderer_end_frame(r,cmd)==VK_SUCCESS);vk_renderer_wait_idle(r);
    const VkRuntimeCapabilityReport* report=vk_runtime_get_capability_report(&r->context.device->runtime);
    assert(report && report->validation_enabled && !report->validation_warning_count && !report->validation_error_count);
}
// Activates a native button through its real modal release owner.
static void click(AppState* s,int index) {
    DawProjectModalLayout l;daw_project_modal_layout(s,&l);SDL_Event e={.type=SDL_MOUSEBUTTONDOWN};e.button.button=SDL_BUTTON_LEFT;
    e.button.x=l.buttons[index].x+5;e.button.y=l.buttons[index].y+5;project_modal_input_handle_event(s,&e);e.type=SDL_MOUSEBUTTONUP;project_modal_input_handle_event(s,&e);
}
// Delivers the same key edges that the native window passes to the application router.
static void key(AppState* s, Uint32 type, SDL_Keycode symbol, SDL_Keymod mod) {
    SDL_Event e={.type=type};e.key.keysym.sym=symbol;e.key.keysym.scancode=SDL_GetScancodeFromKey(symbol);e.key.keysym.mod=mod;
    input_manager_handle_event(&s->input_manager,s,&e);
}
// Replaces the current native text selection through the ordinary SDL text route.
static void type_text(AppState* s,const char* text) {
    key(s,SDL_KEYDOWN,SDLK_a,KMOD_GUI);
    SDL_Event e={.type=SDL_TEXTINPUT};SDL_strlcpy(e.text.text,text,sizeof(e.text.text));
    input_manager_handle_event(&s->input_manager,s,&e);
}
// Simulates an owned SDL file drop using a generated WAV in the fixture workspace.
static void drop(AppState* s,const char* file) {
    SDL_Event e={.type=SDL_DROPFILE};e.drop.file=SDL_strdup(file);assert(e.drop.file);
    input_manager_handle_event(&s->input_manager,s,&e);
}
// Proves the modal boundary across an inspector draft, MIDI key ownership and audio import.
static void modal_ownership(VkRenderer* renderer,AppState* s,const char* prefix) {
    input_manager_init(&s->input_manager);inspector_input_init(s);
    ui_init_panes(s);ui_layout_panes(s,1000,700);
    int sample_rate=s->runtime_cfg.sample_rate,clip_index=-1;
    float pcm[1024]={0};assert(wav_write_f32("l5-tone.wav",pcm,1024,1,sample_rate));
    assert(engine_add_clip_to_track(s->engine,0,"l5-tone.wav",0,&clip_index));
    const EngineClip* clip=&engine_get_tracks(s->engine)[0].clips[clip_index];
    timeline_selection_set_single(s,0,clip_index);
    inspector_input_show(s,0,clip_index,clip);
    inspector_numeric_begin_edit(s,clip,&s->inspector.edit.editing_timeline_start);
    type_text(s,"invalid");key(s,SDL_KEYDOWN,SDLK_RETURN,KMOD_NONE);
    int undo=s->undo.undo_count;
    assert(s->inspector.edit.editing_timeline_start && !strcmp(s->inspector.edit.timeline_start,"invalid"));
    project_modal_input_open_save_prompt(s);
    type_text(s,"Modal draft");
    key(s,SDL_KEYDOWN,SDLK_SPACE,KMOD_NONE);
    assert(!strcmp(s->project_prompt.buffer,"Modal draft"));
    assert(!strcmp(s->inspector.edit.timeline_start,"invalid") && s->undo.undo_count==undo);
    assert(!engine_transport_requested_playing(s->engine));
    frame(renderer,s,prefix,"inspector-takeover");
    key(s,SDL_KEYDOWN,SDLK_ESCAPE,KMOD_NONE);
    assert(!project_modal_input_active(s) && SDL_IsTextInputActive());
    type_text(s,"0.5");key(s,SDL_KEYDOWN,SDLK_RETURN,KMOD_NONE);
    assert(!inspector_input_has_text_focus(s) && s->undo.undo_count==undo+1);
    assert(engine_get_tracks(s->engine)[0].clips[0].timeline_start_frames==(uint64_t)(sample_rate/2));

    int midi=-1;assert(engine_add_midi_clip_to_track(s->engine,0,2*sample_rate,sample_rate,&midi));
    timeline_selection_set_single(s,0,midi);
    s->midi_editor_ui.qwerty_test_enabled=true;
    key(s,SDL_KEYDOWN,SDLK_SEMICOLON,KMOD_NONE);key(s,SDL_KEYDOWN,SDLK_z,KMOD_NONE);
    assert(s->engine->midi_audition_pedal_down && s->midi_editor_ui.qwerty_active_notes[0].active);
    assert(project_modal_input_open_load_modal(s));
    key(s,SDL_KEYUP,SDLK_z,KMOD_NONE);key(s,SDL_KEYUP,SDLK_SEMICOLON,KMOD_NONE);
    bool keys_released=!s->engine->midi_audition_pedal_down && !s->midi_editor_ui.qwerty_active_notes[0].active;
    assert(!mkdir("l5-library",0700));
    assert(realpath("l5-library",s->data_paths.library_copy_root));
    const Pane* library=ui_layout_get_pane(s,3);assert(library && library->visible);
    s->mouse_x=library->rect.x+3;s->mouse_y=library->rect.y+3;
    drop(s,"l5-tone.wav");
    bool drop_blocked=access("l5-library/l5-tone.wav",F_OK)!=0;
    key(s,SDL_KEYDOWN,SDLK_ESCAPE,KMOD_NONE);
    if(!drop_blocked)assert(!unlink("l5-library/l5-tone.wav"));
    drop(s,"l5-tone.wav");assert(!access("l5-library/l5-tone.wav",F_OK));
    if(keys_released) {
        assert(s->midi_editor_ui.qwerty_test_enabled);
        key(s,SDL_KEYDOWN,SDLK_z,KMOD_NONE);
        assert(s->midi_editor_ui.qwerty_active_notes[0].active);
        key(s,SDL_KEYUP,SDLK_z,KMOD_NONE);
        assert(!s->midi_editor_ui.qwerty_active_notes[0].active);
    }
    engine_midi_audition_all_notes_off(s->engine);
    memset(s->midi_editor_ui.qwerty_active_notes,0,sizeof(s->midi_editor_ui.qwerty_active_notes));
    s->midi_editor_ui.qwerty_test_enabled=false;
    s->midi_editor_ui.panel_mode=MIDI_REGION_PANEL_INSTRUMENT;
    s->sound_ui.open=true;s->sound_ui.pressed=-1;s->sound_ui.listening=14;
    assert(engine_start(s->engine));
    int channels=s->engine->device.spec.channels;assert(channels>0);
    float* preview=calloc((size_t)sample_rate*8*channels,sizeof(*preview));assert(preview);
    assert(engine_preview_play(s->engine,preview,8*sample_rate,channels,sample_rate));
    free(preview);
    DawSoundPanelLayout sound;daw_sound_panel_layout(s,&sound);
    SDL_Event pointer={.type=SDL_MOUSEBUTTONDOWN};
    pointer.button.button=SDL_BUTTON_LEFT;pointer.button.x=sound.buttons[16].x+3;pointer.button.y=sound.buttons[16].y+3;
    input_manager_handle_event(&s->input_manager,s,&pointer);
    assert(s->sound_ui.pressed==16);
    project_modal_input_open_save_prompt(s);
    key(s,SDL_KEYDOWN,SDLK_ESCAPE,KMOD_NONE);assert(!project_modal_input_active(s));
    pointer.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&s->input_manager,s,&pointer);
    bool stale_preview_blocked=engine_preview_active(s->engine);
    fprintf(stderr,"L5 soniCs modal releases audition=%d blocks drop=%d stale preview release blocked=%d\n",keys_released,drop_blocked,stale_preview_blocked);
    assert(keys_released && drop_blocked && stale_preview_blocked);
    pointer.type=SDL_MOUSEBUTTONDOWN;input_manager_handle_event(&s->input_manager,s,&pointer);
    pointer.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&s->input_manager,s,&pointer);
    assert(!engine_preview_active(s->engine));
    assert(engine_transport_play(s->engine));
    project_modal_input_open_save_prompt(s);
    key(s,SDL_KEYDOWN,SDLK_SPACE,KMOD_NONE);
    key(s,SDL_KEYDOWN,SDLK_ESCAPE,KMOD_NONE);
    assert(!project_modal_input_active(s) && engine_transport_requested_playing(s->engine));
    assert(engine_transport_stop(s->engine));
    puts("PASS L5 soniCs: inspector rejection and resume, modal audition and drop isolation, fresh preview control and retained transport");
}
// Proves actual scoped browsing, version saves and empty-folder UI without touching user songs.
int main(int argc,char** argv) {
    assert(argc==3);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_AUDIO) && !TTF_Init());
    SDL_Window* window=SDL_CreateWindow("soniCs collection proof",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1000,700,SDL_WINDOW_VULKAN|SDL_WINDOW_ALLOW_HIGHDPI);assert(window);
    VkRenderer renderer={0};VkRendererConfig cfg;vk_renderer_config_set_defaults(&cfg);cfg.enable_validation=SDL_TRUE;assert(vk_renderer_init(&renderer,window,&cfg)==VK_SUCCESS);
    char font[4096],cwd[4096];assert(getcwd(cwd,sizeof(cwd)));snprintf(font,sizeof(font),"%s/include/fonts/Montserrat/Montserrat-Regular.ttf",cwd);
    assert(ui_font_set(font,9));AppState* s=calloc(1,sizeof(*s));assert(s);
    config_set_defaults(&s->runtime_cfg);s->engine=engine_create(&s->runtime_cfg);assert(s->engine);s->tempo=tempo_state_default(s->runtime_cfg.sample_rate);
    tempo_map_init(&s->tempo_map,s->runtime_cfg.sample_rate);time_signature_map_init(&s->time_signature_map);undo_manager_init(&s->undo);
    s->timeline_visible_seconds=10;s->timeline_vertical_scale=1;s->selected_track_index=s->selected_clip_index=-1;s->window_width=1000;s->window_height=700;
    assert(!chdir(argv[2]));assert(!mkdir("config",0700));assert(!mkdir("config/projects",0700));assert(!mkdir("music_projects",0700));
    const char* folders[]={"music_projects/classical_conversations","music_projects/classical_conversations/projects","music_projects/night_pocket","music_projects/night_pocket/projects"};
    for(int i=0;i<4;++i)assert(!mkdir(folders[i],0700));
    assert(project_collections_refresh(s,"music_projects"));assert(project_collections_select(s,1));
    project_modal_input_open_save_prompt(s);strcpy(s->project_prompt.buffer,"Classical_Conversations_v001_ThemeStudy");frame(&renderer,s,argv[1],"save-version");click(s,0);
    assert(strstr(s->project.path,"v001"));project_modal_input_open_save_prompt(s);click(s,0);assert(strstr(s->project.path,"v002"));
    project_modal_input_open_save_prompt(s);refuse_directory_sync=1;click(s,0);refuse_directory_sync=0;
    assert(s->project_prompt.active && s->project_prompt.publication_warning && s->project.last_save_result==DAW_SAVE_PUBLISHED);
    assert(strstr(s->project_prompt.error,"sync unconfirmed"));
    char published[SESSION_PATH_MAX];strcpy(published,s->project.path);click(s,0);assert(!strcmp(published,s->project.path));
    frame(&renderer,s,argv[1],"published-warning");click(s,1);assert(!s->project_prompt.active);
    char path[SESSION_PATH_MAX];strcpy(path,s->project.path);assert(project_modal_input_open_load_modal(s) && s->project_load.count==3);
    frame(&renderer,s,argv[1],"classical-list");click(s,3);assert(!s->project_load.count && !strcmp(path,s->project.path));frame(&renderer,s,argv[1],"empty-collection");
    click(s,4);assert(s->project_prompt.folder_mode);frame(&renderer,s,argv[1],"choose-folder");click(s,1);assert(s->project_load.active);
    click(s,2);assert(s->project_load.count==3);s->window_width=600;s->window_height=420;frame(&renderer,s,argv[1],"compact-list");
    project_modal_input_close_load_modal(s);
    assert(project_manager_save(s,s->project.name,true));
    FILE* corrupt=fopen(s->project.path,"wb");assert(corrupt);fputs("{broken",corrupt);assert(!fclose(corrupt));
    assert(project_manager_load(s,s->project.path));
    assert(strstr(s->project.persistence_notice,"Recovered previous save"));
    frame(&renderer,s,argv[1],"recovered-backup");
    modal_ownership(&renderer,s,argv[1]);
    ui_font_invalidate_cache((SDL_Renderer*)&renderer);ui_font_shutdown();engine_destroy(s->engine);undo_manager_free(&s->undo);
    tempo_map_free(&s->tempo_map);time_signature_map_free(&s->time_signature_map);free(s);vk_renderer_shutdown(&renderer);SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();
    puts("project_collections_native_test: passed (actual collection/save controls, v001/v002, scoped and empty lists, folder cancel, compact geometry and clean Vulkan frames)");return 0;
}
