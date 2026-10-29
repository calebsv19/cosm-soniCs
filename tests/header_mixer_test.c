#define _XOPEN_SOURCE 700
#include "app_state.h"
#include "engine/engine_internal.h"
#include "engine/groups.h"
#include "daw/track_mix.h"
#include "daw/routing_actions.h"
#include "input/track_header_mixer.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline_selection.h"
#include "ui/timeline_track_header.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "ui/effects_panel.h"
#include "ui/midi_editor.h"
#include "vk_renderer.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Gets current shared field geometry so native events never rely on guessed coordinates.
static TimelineTrackHeaderLayout header(AppState* s,int t) {TimelineGeometry g;assert(timeline_compute_geometry(s,&s->panes[1],&g));TimelineTrackHeaderLayout h;
    timeline_track_header_layout(s,t,&s->panes[1].rect,timeline_track_lane_y(s,t,g.track_top,g.track_height,g.track_spacing),g.track_height,g.header_width,&h);return h;}
// Delivers native press/release edges through production routing and preserves their click count.
static void pointer(AppState* s,Uint32 type,SDL_Rect r,unsigned clicks) {SDL_Event e={.type=type};e.button.button=SDL_BUTTON_LEFT;e.button.clicks=clicks;
    e.button.x=r.x+r.w/2;e.button.y=r.y+r.h/2;s->mouse_x=e.button.x;s->mouse_y=e.button.y;input_manager_handle_event(&s->input_manager,s,&e);}
// Drives one complete relative gesture through its actual event owner.
static void drag(AppState* s,int t,bool pan,int dy) {SDL_Rect r=pan ? header(s,t).pan_rect : header(s,t).gain_rect;
    pointer(s,SDL_MOUSEBUTTONDOWN,r,1);assert(s->header_mixer.dragging);SDL_Event e={.type=SDL_MOUSEMOTION};e.motion.x=r.x+r.w/2;e.motion.y=r.y+r.h/2+dy;
    input_manager_handle_event(&s->input_manager,s,&e);pointer(s,SDL_MOUSEBUTTONUP,r,1);assert(!s->header_mixer.dragging && !s->undo.active_drag_valid);}
// Sends a keyboard edge through the same focused scalar/history path used by the native app.
static void key(AppState* s,SDL_Keycode code,SDL_Keymod mod,bool repeat) {SDL_Event e={.type=SDL_KEYDOWN};e.key.keysym.sym=code;e.key.keysym.mod=mod;e.key.repeat=repeat;
    input_manager_handle_event(&s->input_manager,s,&e);e.type=SDL_KEYUP;input_manager_handle_event(&s->input_manager,s,&e);}
// Captures exact original-level production audio with deterministic cold state.
static EngineBounceBuffer bounce(AppState* s) {EngineBounceOptions o=engine_bounce_options_default();o.normalize_if_clipping=false;EngineBounceBuffer b={0};
    assert(engine_bounce_range_to_buffer_with_options(s->engine,0,24000,&o,NULL,NULL,&b));return b;}
// Checks independent channel factors for child gain followed by the parent bus gain/pan.
static void verify_signal(EngineBounceBuffer* base,EngineBounceBuffer* got,float left,float right) {
    assert(base->frame_count==got->frame_count && got->channels==2);
    for(size_t i=0;i<got->frame_count;++i){assert(fabsf(got->data[i*2]-base->data[i*2]*left)<1e-6f);assert(fabsf(got->data[i*2+1]-base->data[i*2+1]*right)<1e-6f);}
}
// Captures complete actual UI panes with live mixer values and validation-clean renderer state.
static void frame(VkRenderer* r,AppState* s,const char* prefix,const char* name) {char path[4096];snprintf(path,sizeof(path),"%s-%s.bmp",prefix,name);
    assert(vk_renderer_request_capture(r,path)==VK_SUCCESS);VkCommandBuffer cmd;VkFramebuffer fb;VkExtent2D size;
    assert(vk_renderer_begin_frame(r,&cmd,&fb,&size)==VK_SUCCESS);vk_renderer_set_logical_size(r,s->window_width,s->window_height);
    ui_render_panes((SDL_Renderer*)r,s);ui_render_controls((SDL_Renderer*)r,s);ui_render_overlays((SDL_Renderer*)r,s);assert(vk_renderer_end_frame(r,cmd)==VK_SUCCESS);vk_renderer_wait_idle(r);
    const VkRuntimeCapabilityReport* report=vk_runtime_get_capability_report(&r->context.device->runtime);assert(report && report->validation_enabled && !report->validation_warning_count && !report->validation_error_count);
}
// Qualifies scalar mixing/history, view density, gesture cancellation, playback and save/reopen without user data.
int main(int argc,char** argv) {assert(argc==1 || argc==2);bool native=argc==2;
    if(!native)SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS) && !TTF_Init());assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));
    VkRenderer renderer={0};SDL_Window* window=NULL;
    if(native){window=SDL_CreateWindow("soniCs header mixer proof",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1600,1000,SDL_WINDOW_VULKAN|SDL_WINDOW_ALLOW_HIGHDPI);assert(window);
        VkRendererConfig cfg;vk_renderer_config_set_defaults(&cfg);cfg.enable_validation=SDL_TRUE;assert(vk_renderer_init(&renderer,window,&cfg)==VK_SUCCESS);}
    AppState* s=calloc(1,sizeof(*s));assert(s);config_set_defaults(&s->runtime_cfg);s->engine=engine_create(&s->runtime_cfg);assert(s->engine);
    undo_manager_init(&s->undo);input_manager_init(&s->input_manager);ui_init_panes(s);ui_layout_panes(s,1600,1000);s->timeline_visible_seconds=5;s->timeline_vertical_scale=1;
    tempo_map_init(&s->tempo_map,48000);time_signature_map_init(&s->time_signature_map);s->tempo=tempo_state_default(48000);
    int c;assert(engine_add_midi_clip_to_track(s->engine,0,0,48000,&c));assert(engine_track_set_name(s->engine,0,"Violin"));
    EngineMidiNote note={.note=60,.velocity=.7f,.duration_frames=20000};assert(engine_clip_midi_add_note(s->engine,0,c,note,NULL));
    EngineRoutingSnapshot before,out;assert(daw_action_get_routing(s,&before)==DAW_ACTION_UNCHANGED);assert(daw_action_create_group(s,&before,"Strings",&out)==DAW_ACTION_APPLIED);
    before=out;out.tracks[0].parent_id=out.tracks[1].track_id;assert(daw_action_set_routing(s,&before,&out,&before)==DAW_ACTION_APPLIED);
    timeline_selection_set_single(s,0,c);effects_panel_sync_from_engine(s);EngineBounceBuffer base=bounce(s);int history=s->undo.undo_count;
    TimelineGeometry g;assert(timeline_compute_geometry(s,&s->panes[1],&g) && g.content_left==s->panes[1].rect.x+g.header_width+2 && g.window_start_seconds==0);
    s->timeline_window_start_seconds=-.5f;assert(timeline_compute_geometry(s,&s->panes[1],&g) && g.window_start_seconds==-.5f);s->timeline_window_start_seconds=0;
    SDL_Rect field=header(s,0).gain_rect;pointer(s,SDL_MOUSEBUTTONDOWN,field,1);assert(engine_get_tracks(s->engine)[0].gain==1);pointer(s,SDL_MOUSEBUTTONUP,field,1);
    assert(s->undo.undo_count==history && s->selected_track_index==0 && s->selected_clip_index==c);
    drag(s,0,false,75);float half=powf(10,-6.0f/20);assert(fabsf(engine_get_tracks(s->engine)[0].gain-half)<1e-6f && s->undo.undo_count==history+1);
    assert(fabsf(s->effects_panel.track_snapshot.gain-half)<1e-6f);
    assert(engine_track_set_pan(s->engine,0,.25f));key(s,SDLK_z,KMOD_CTRL,false);assert(engine_get_tracks(s->engine)[0].gain==1 && engine_get_tracks(s->engine)[0].pan==.25f);
    key(s,SDLK_z,KMOD_CTRL|KMOD_SHIFT,false);assert(fabsf(engine_get_tracks(s->engine)[0].gain-half)<1e-6f && engine_get_tracks(s->engine)[0].pan==.25f);
    assert(engine_track_set_pan(s->engine,0,0));
    drag(s,1,false,75);drag(s,1,true,-250);assert(fabsf(engine_get_tracks(s->engine)[1].gain-half)<1e-6f && fabsf(engine_get_tracks(s->engine)[1].pan-.5f)<1e-6f);
    EngineBounceBuffer mixed=bounce(s);verify_signal(&base,&mixed,half*half*.5f,half*half);engine_bounce_buffer_free(&mixed);
    history=s->undo.undo_count;field=header(s,1).pan_rect;pointer(s,SDL_MOUSEBUTTONDOWN,field,1);pointer(s,SDL_MOUSEBUTTONUP,field,1);
    float p=engine_get_tracks(s->engine)[1].pan;key(s,SDLK_RIGHT,KMOD_SHIFT,false);assert(fabsf(engine_get_tracks(s->engine)[1].pan-p-.005f)<1e-6f);
    key(s,SDLK_RIGHT,KMOD_SHIFT,true);assert(s->undo.undo_count==history+1);assert(undo_manager_undo(&s->undo,s));
    // Escape restores only the captured scalar and preserves the redo cursor for a canceled edit.
    int redo=s->undo.redo_count;field=header(s,0).gain_rect;pointer(s,SDL_MOUSEBUTTONDOWN,field,1);SDL_Event move={.type=SDL_MOUSEMOTION};move.motion.y=field.y+field.h/2-50;
    input_manager_handle_event(&s->input_manager,s,&move);key(s,SDLK_ESCAPE,KMOD_NONE,false);assert(!s->header_mixer.dragging && s->undo.redo_count==redo && fabsf(engine_get_tracks(s->engine)[0].gain-half)<1e-6f);
    // Double-reset retains pointer ownership until release rather than selecting an underlying header.
    history=s->undo.undo_count;field=header(s,1).pan_rect;pointer(s,SDL_MOUSEBUTTONDOWN,field,2);assert(s->header_mixer.wait_release && engine_get_tracks(s->engine)[1].pan==0);
    input_manager_update(&s->input_manager,s);assert(s->selected_track_index==0);pointer(s,SDL_MOUSEBUTTONUP,field,2);assert(!s->header_mixer.wait_release && s->undo.undo_count==history+1);
    assert(undo_manager_undo(&s->undo,s) && fabsf(engine_get_tracks(s->engine)[1].pan-.5f)<1e-6f);
    // Current-value guards protect later writes to the same scalar without disturbing unrelated state.
    assert(s->undo.undo_stack[s->undo.undo_count-1].type==UNDO_CMD_TRACK_MIX);
    DawTrackMixEdit edit=s->undo.undo_stack[s->undo.undo_count-1].data.track_mix;
    assert(!daw_track_mix_write(s,&edit,edit.after,NAN));
    history=s->undo.undo_count;assert(engine_track_set_pan(s->engine,1,.6f));
    assert(!undo_manager_undo(&s->undo,s) && s->undo.undo_count==history && engine_get_tracks(s->engine)[1].pan==.6f);
    assert(engine_get_tracks(s->engine)[0].clips[c].midi_notes.note_count==1);
    assert(engine_track_set_pan(s->engine,1,.5f));
    float original=engine_get_tracks(s->engine)[0].gain;field=header(s,0).gain_rect;pointer(s,SDL_MOUSEBUTTONDOWN,field,1);move.motion.y=field.y+field.h/2-20;input_manager_handle_event(&s->input_manager,s,&move);
    SDL_Event lost={.type=SDL_WINDOWEVENT};lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;input_manager_handle_event(&s->input_manager,s,&lost);
    assert(!s->header_mixer.dragging && !s->undo.active_drag_valid && engine_get_tracks(s->engine)[0].gain!=original);assert(undo_manager_undo(&s->undo,s));
    assert(engine_start(s->engine) && engine_transport_play(s->engine));SDL_Delay(40);assert(s->engine->scalar_mix_supported);
    drag(s,0,true,-100);assert(engine_transport_requested_playing(s->engine) && s->engine->scalar_mix_supported && !atomic_load(&s->engine->rebuild_sources_pending));
    assert(undo_manager_undo(&s->undo,s) && engine_transport_requested_playing(s->engine));
    if(native){timeline_selection_set_single(s,1,-1);effects_panel_sync_from_engine(s);frame(&renderer,s,argv[1],"live-levels");}
    // Geometry changes commit the last accepted value and cannot apply stale motion to a newly placed row.
    field=header(s,1).gain_rect;pointer(s,SDL_MOUSEBUTTONDOWN,field,1);move.motion.y=field.y+field.h/2-20;input_manager_handle_event(&s->input_manager,s,&move);
    float accepted=engine_get_tracks(s->engine)[1].gain;ui_layout_panes(s,850,550);move.motion.y-=100;input_manager_handle_event(&s->input_manager,s,&move);
    assert(!s->header_mixer.dragging && engine_get_tracks(s->engine)[1].gain==accepted);assert(undo_manager_undo(&s->undo,s));
    s->timeline_vertical_scale=.5f;assert(timeline_compute_geometry(s,&s->panes[1],&g) && g.track_height==36);
    TimelineTrackHeaderLayout h=header(s,1);assert(h.gain_rect.x+h.gain_rect.w<=h.pan_rect.x && h.pan_rect.x+h.pan_rect.w<=h.header_rect.x+h.header_rect.w);
    assert(h.pan_rect.y+h.pan_rect.h<=h.header_rect.y+h.header_rect.h);if(native)frame(&renderer,s,argv[1],"compact-levels");
    engine_transport_stop(s->engine);engine_stop(s->engine);char dir[]="/tmp/sonics-header-mixer-test-XXXXXX";assert(mkdtemp(dir));char path[1024];snprintf(path,sizeof(path),"%s/project.json",dir);
    assert(session_save_to_file(s,path) && session_load_from_file(s,path));assert(fabsf(engine_get_tracks(s->engine)[0].gain-half)<1e-6f && fabsf(engine_get_tracks(s->engine)[1].pan-.5f)<1e-6f);
    EngineBounceBuffer reopened=bounce(s);verify_signal(&base,&reopened,half*half*.5f,half*half);engine_bounce_buffer_free(&reopened);engine_bounce_buffer_free(&base);
    // Releasing an already-held audition key must reach its owner while a header drag is active.
    timeline_selection_set_single(s,0,c);s->midi_editor_ui.qwerty_test_enabled=true;MidiEditorLayout midi_layout;midi_editor_compute_layout(s,&midi_layout);
    SDL_Event audition={.type=SDL_KEYDOWN};audition.key.keysym.sym=SDLK_a;input_manager_handle_event(&s->input_manager,s,&audition);
    assert(s->midi_editor_ui.qwerty_active_notes[0].active);field=header(s,0).gain_rect;pointer(s,SDL_MOUSEBUTTONDOWN,field,1);
    audition.type=SDL_KEYUP;input_manager_handle_event(&s->input_manager,s,&audition);assert(!s->midi_editor_ui.qwerty_active_notes[0].active);
    pointer(s,SDL_MOUSEBUTTONUP,field,1);s->midi_editor_ui.qwerty_test_enabled=false;assert(engine_get_tracks(s->engine)[0].clips[c].midi_notes.note_count==1);
    // Project replacement invalidates an in-flight gesture before the replacement engine can be edited.
    field=header(s,0).gain_rect;pointer(s,SDL_MOUSEBUTTONDOWN,field,1);Engine* old=s->engine;s->engine=engine_create(&s->runtime_cfg);assert(s->engine);undo_manager_clear(&s->undo);
    move.motion.y=field.y-200;input_manager_handle_event(&s->input_manager,s,&move);assert(!s->header_mixer.dragging && engine_get_tracks(s->engine)[0].gain==1);engine_destroy(old);
    drag(s,0,false,1000);assert(engine_get_tracks(s->engine)[0].gain==0);history=s->undo.undo_count;
    key(s,SDLK_DOWN,KMOD_NONE,false);assert(engine_get_tracks(s->engine)[0].gain==0 && s->undo.undo_count==history);
    key(s,SDLK_UP,KMOD_NONE,false);assert(engine_get_tracks(s->engine)[0].gain>0 && s->undo.undo_count==history+1);
    engine_destroy(s->engine);undo_manager_free(&s->undo);tempo_map_free(&s->tempo_map);time_signature_map_free(&s->time_signature_map);free(s);
    if(native){ui_font_invalidate_cache((SDL_Renderer*)&renderer);vk_renderer_shutdown(&renderer);SDL_DestroyWindow(window);}ui_font_shutdown();TTF_Quit();SDL_Quit();assert(!unlink(path) && !rmdir(dir));
    puts("header_mixer_test: passed (child/group signal factors, scalar history/keyboard, no-op/cancel/reset isolation, focus/resize/project guards, live publication and exact save/reopen)");return 0;
}
