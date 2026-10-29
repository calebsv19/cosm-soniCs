#define _XOPEN_SOURCE 700
#include "test_temp_path.h"
#include "app_state.h"
#include "engine/engine_internal.h"
#include "engine/groups.h"
#include "daw/routing_actions.h"
#include "ui/timeline_group_overview.h"
#include "ui/track_group_panel.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "ui/editor_controls.h"
#include "ui/effects_panel.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline_selection.h"
#include "vk_renderer.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Captures exact common routing guards for deterministic fixture authoring.
static EngineRoutingSnapshot routing(AppState* s) {EngineRoutingSnapshot r;assert(daw_action_get_routing(s,&r)==DAW_ACTION_UNCHANGED);return r;}
// Sends real application press/release edges through stable shared control ownership.
static void click(AppState* s,SDL_Rect rect,unsigned clicks) {
    SDL_Event e={.type=SDL_MOUSEBUTTONDOWN};e.button.button=SDL_BUTTON_LEFT;e.button.clicks=clicks;
    e.button.x=rect.x+rect.w/2;e.button.y=rect.y+rect.h/2;s->mouse_x=e.button.x;s->mouse_y=e.button.y;
    input_manager_handle_event(&s->input_manager,s,&e);e.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&s->input_manager,s,&e);
}
// Finds live control geometry by semantic identity, not array slot or assumed screen coordinates.
static SDL_Rect control(AppState* s,unsigned domain,uint64_t id) {
    daw_editor_controls_sync(s);KitUiSurface* surface=&s->editor_controls.surface;
    for(unsigned i=0;i<surface->count;++i)if(surface->keys[i].domain==domain && surface->keys[i].value==id) {
        KitRenderRect r=surface->controls[i].bounds;return(SDL_Rect){r.x,r.y,r.width,r.height};
    }
    assert(0);return(SDL_Rect){0};
}
// Renders a cold exact production reference, avoiding normalization and user devices.
static EngineBounceBuffer bounce(AppState* s) {EngineBounceOptions o=engine_bounce_options_default();o.normalize_if_clipping=false;EngineBounceBuffer b={0};
    assert(engine_bounce_range_to_buffer_with_options(s->engine,0,144000,&o,NULL,NULL,&b));return b;}
// Renders actual application panes with compact headers and mixed overview bands.
static void frame(VkRenderer* r,AppState* s,const char* prefix,const char* name) {
    char p[4096];snprintf(p,sizeof(p),"%s-%s.bmp",prefix,name);assert(vk_renderer_request_capture(r,p)==VK_SUCCESS);
    VkCommandBuffer cmd;VkFramebuffer fb;VkExtent2D size;assert(vk_renderer_begin_frame(r,&cmd,&fb,&size)==VK_SUCCESS);
    vk_renderer_set_logical_size(r,s->window_width,s->window_height);ui_render_panes((SDL_Renderer*)r,s);ui_render_controls((SDL_Renderer*)r,s);ui_render_overlays((SDL_Renderer*)r,s);
    assert(vk_renderer_end_frame(r,cmd)==VK_SUCCESS);vk_renderer_wait_idle(r);
    const VkRuntimeCapabilityReport* report=vk_runtime_get_capability_report(&r->context.device->runtime);
    assert(report && report->validation_enabled && !report->validation_warning_count && !report->validation_error_count);
}
// Proves derived source views, live folding/history, stable selection, release guards and native compact layouts.
int main(int argc,char** argv) {
    assert(argc==1 || argc==2);bool native=argc==2;
    if(!native)SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS) && !TTF_Init());assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",9));
    VkRenderer renderer={0};SDL_Window* window=NULL;
    if(native){window=SDL_CreateWindow("soniCs compact track proof",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1600,1000,SDL_WINDOW_VULKAN|SDL_WINDOW_ALLOW_HIGHDPI);assert(window);
        VkRendererConfig c;vk_renderer_config_set_defaults(&c);c.enable_validation=SDL_TRUE;assert(vk_renderer_init(&renderer,window,&c)==VK_SUCCESS);}
    AppState* s=calloc(1,sizeof(*s));assert(s);config_set_defaults(&s->runtime_cfg);s->engine=engine_create(&s->runtime_cfg);assert(s->engine);
    undo_manager_init(&s->undo);input_manager_init(&s->input_manager);ui_init_panes(s);ui_layout_panes(s,1600,1000);
    tempo_map_init(&s->tempo_map,s->runtime_cfg.sample_rate);time_signature_map_init(&s->time_signature_map);s->tempo=tempo_state_default(48000);
    s->timeline_visible_seconds=5;s->timeline_vertical_scale=1;s->selected_track_index=s->selected_clip_index=-1;
    while(engine_get_track_count(s->engine)<2)assert(engine_add_track(s->engine)>=0);
    assert(engine_track_set_name(s->engine,0,"Violin theme") && engine_track_set_name(s->engine,1,"Recorded room"));
    int midi=-1;assert(engine_add_midi_clip_to_track(s->engine,0,0,48000,&midi));
    for(int i=0;i<6;++i){EngineMidiNote n={.note=60+i*2,.velocity=.7f,.start_frame=i*6000,.duration_frames=4800};assert(engine_clip_midi_add_note(s->engine,0,midi,n,NULL));}
    int other;assert(engine_add_midi_clip_to_track(s->engine,0,96000,48000,&other));EngineMidiNote n={.note=48,.velocity=.6f,.duration_frames=36000};assert(engine_clip_midi_add_note(s->engine,0,other,n,NULL));
    DAW_TEST_TEMP_PATH(dir, "sonics-track-ui-fixture-XXXXXX");assert(mkdtemp(dir));char wav[1024];snprintf(wav,sizeof(wav),"%s/room.wav",dir);
    float* samples=calloc(48000*2,sizeof(float));assert(samples);for(int i=0;i<48000;++i)samples[i*2]=samples[i*2+1]=.15f*sinf((float)i*.031f);
    EngineBounceBuffer source={.data=samples,.frame_count=48000,.channels=2,.sample_rate=48000};assert(engine_bounce_write_wav(&source,wav,ENGINE_BOUNCE_WAV_PCM16));free(samples);
    assert(engine_add_clip_to_track(s->engine,1,wav,24000,NULL));
    EngineRoutingSnapshot before=routing(s),out;assert(daw_action_create_group(s,&before,"Strings",&out)==DAW_ACTION_APPLIED);before=out;
    assert(daw_action_create_group(s,&before,"Major Theme",&out)==DAW_ACTION_APPLIED);before=out;
    uint64_t strings=out.tracks[2].track_id,theme=out.tracks[3].track_id;
    out.tracks[0].parent_id=theme;out.tracks[1].parent_id=strings;out.tracks[3].parent_id=strings;
    assert(daw_action_set_routing(s,&before,&out,&before)==DAW_ACTION_APPLIED);
    TimelineGeometry g;assert(timeline_compute_geometry(s,&s->panes[1],&g));assert(g.track_height==48 && g.track_spacing==2 && g.header_width==200);
    TimelineGroupOverviewItem a,b;assert(timeline_group_overview_item(s,2,0,0,&a) && a.midi);assert(timeline_group_overview_item(s,2,1,0,&b) && !b.midi);
    assert(b.rect.x>a.rect.x && !timeline_group_overview_item(s,3,1,0,&b));
    s->timeline_window_start_seconds=.5f;assert(timeline_group_overview_item(s,2,0,0,&a) && a.source_start==24000);s->timeline_window_start_seconds=0;
    TimelineTrackHeaderLayout h;timeline_track_header_layout(s,0,&s->panes[1].rect,g.track_top,g.track_height,g.header_width,&h);
    assert(h.mute_rect.y==h.solo_rect.y && h.effects_rect.y==h.mute_rect.y && h.text_max_x>h.text_x && h.meter_rect.w>0);
    EngineBounceBuffer baseline=bounce(s);timeline_selection_set_single(s,0,midi);if(native)frame(&renderer,s,argv[1],"expanded-mixed");
    assert(engine_start(s->engine));assert(engine_transport_play(s->engine));SDL_Delay(40);
    uint64_t accepted=atomic_load(&s->engine->commands_accepted);
    click(s,control(s,19,strings),1);assert(engine_get_tracks(s->engine)[2].folded && s->selected_track_index==2);
    assert(engine_transport_requested_playing(s->engine) && atomic_load(&s->engine->commands_accepted)==accepted);
    assert(engine_group_visible_row(engine_get_tracks(s->engine),4,0)<0);
    assert(timeline_group_overview_item(s,2,0,0,&a) && timeline_group_overview_item(s,2,1,0,&b));
    if(native)frame(&renderer,s,argv[1],"collapsed-playing");
    before=routing(s);out=before;out.tracks[0].parent_id=0;assert(daw_action_set_routing(s,&before,&out,&out)==DAW_ACTION_BUSY);
    assert(daw_action_routing_history(s,&before,false,&out)==DAW_ACTION_APPLIED && engine_transport_requested_playing(s->engine));
    assert(daw_action_routing_history(s,&out,true,&before)==DAW_ACTION_APPLIED);
    click(s,control(s,20,strings),2);assert(!engine_get_tracks(s->engine)[2].folded && s->selected_track_index==0 && s->selected_clip_index==midi);
    assert(engine_transport_requested_playing(s->engine) && atomic_load(&s->engine->commands_accepted)==accepted);
    click(s,control(s,21,theme),1);assert(s->selected_track_index==3 && s->effects_panel.target_track_index==3);
    if(native){frame(&renderer,s,argv[1],"group-selected");s->timeline_vertical_scale=.5f;ui_layout_panes(s,850,550);frame(&renderer,s,argv[1],"compact-playing");}
    engine_transport_stop(s->engine);engine_stop(s->engine);EngineBounceBuffer after=bounce(s);
    assert(baseline.frame_count==after.frame_count && !memcmp(baseline.data,after.data,(size_t)after.frame_count*after.channels*sizeof(float)));
    engine_bounce_buffer_free(&baseline);engine_bounce_buffer_free(&after);
    waveform_cache_shutdown(&s->waveform_cache);undo_manager_free(&s->undo);engine_destroy(s->engine);tempo_map_free(&s->tempo_map);time_signature_map_free(&s->time_signature_map);free(s);
    if(native){ui_font_invalidate_cache((SDL_Renderer*)&renderer);vk_renderer_shutdown(&renderer);SDL_DestroyWindow(window);}ui_font_shutdown();TTF_Quit();SDL_Quit();assert(!unlink(wav) && !rmdir(dir));
    puts("track_ui_test: passed (compact shared geometry, nested mixed previews/trim, live fold/history without audio commands, stable selection/FX ownership and byte-identical production audio)");return 0;
}
