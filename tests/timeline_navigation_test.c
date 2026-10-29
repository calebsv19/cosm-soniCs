#include "app_state.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "ui/editor_controls.h"
#include "ui/effects_panel.h"
#include "input/effects_panel_input.h"
#include "vk_renderer.h"
#include "input/timeline/timeline_geometry.h"
#include "input/timeline/timeline_input_mouse_scroll.h"
#include "input/timeline_selection.h"
#include "input/transport_input.h"
#include "input/timeline/timeline_input_mouse_click.h"
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>

// Obtains the production geometry for the currently laid out timeline.
static TimelineGeometry geometry(AppState* s) {
    TimelineGeometry g; assert(timeline_compute_geometry(s, ui_layout_get_pane(s, 1), &g)); return g;
}
// Sends a precise wheel event through the product timeline owner.
static void wheel(AppState* s, float x, float y, SDL_Keymod mods, Uint32 direction) {
    TimelineGeometry g = geometry(s);
    SDL_Event e = {.type = SDL_MOUSEWHEEL}; e.wheel.x = (int)x; e.wheel.y = (int)y;
    e.wheel.preciseX = x; e.wheel.preciseY = y; e.wheel.direction = direction;
    e.wheel.mouseX = g.content_left + 100; e.wheel.mouseY = g.viewport.y + 100;
    SDL_SetModState(mods); assert(timeline_input_mouse_handle_scroll(&s->input_manager, s, &e)); SDL_SetModState(KMOD_NONE);
}
// Checks every row remains reachable and spacing/toolbar pixels are not tracks.
static void reachable(AppState* s) {
    for (int i = 0; i < engine_get_track_count(s->engine); ++i) {
        timeline_reveal_track(s, i); TimelineGeometry g = geometry(s);
        int top = g.track_top + i * (g.track_height + g.track_spacing);
        assert(top >= g.viewport.y && top + g.track_height <= g.viewport.y + g.viewport.h);
        assert(timeline_track_at_position(s, top + g.track_height / 2, g.track_height, g.track_spacing) == i);
        assert(timeline_track_at_position(s, g.viewport.y - 1, g.track_height, g.track_spacing) == -1);
        if (top + g.track_height < g.viewport.y + g.viewport.h)
            assert(timeline_track_at_position(s, top + g.track_height, g.track_height, g.track_spacing) == -1);
    }
}
// Captures the actual timeline painter and requires validation-clean native frames.
static void capture(VkRenderer* renderer, AppState* s, const char* prefix, const char* name) {
    char path[4096]; snprintf(path,sizeof(path),"%s-%s.bmp",prefix,name);
    assert(vk_renderer_request_capture(renderer,path)==VK_SUCCESS);
    VkCommandBuffer cmd; VkFramebuffer fb; VkExtent2D extent;
    assert(vk_renderer_begin_frame(renderer,&cmd,&fb,&extent)==VK_SUCCESS);
    vk_renderer_set_logical_size(renderer,1600,1000);
    ui_render_panes((SDL_Renderer*)renderer,s); ui_render_controls((SDL_Renderer*)renderer,s);
    assert(vk_renderer_end_frame(renderer,cmd)==VK_SUCCESS); vk_renderer_wait_idle(renderer);
    const VkRuntimeCapabilityReport* report=vk_runtime_get_capability_report(&renderer->context.device->runtime);
    assert(report && report->validation_enabled && !report->validation_warning_count && !report->validation_error_count);
}
// Exercises bounds, row anchoring, two-axis navigation, capture and layout changes.
int main(int argc, char** argv) {
    assert(argc == 1 || argc == 2);
    assert(SDL_Init(SDL_INIT_EVENTS | (argc == 2 ? SDL_INIT_VIDEO : 0)) == 0); assert(TTF_Init() == 0); assert(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf",14));
    AppState* s = calloc(1, sizeof(*s)); assert(s); config_set_defaults(&s->runtime_cfg);
    s->engine = engine_create(&s->runtime_cfg); assert(s->engine); input_manager_init(&s->input_manager);
    tempo_map_init(&s->tempo_map,s->runtime_cfg.sample_rate); time_signature_map_init(&s->time_signature_map);
    s->tempo=tempo_state_default(s->runtime_cfg.sample_rate);
    ui_init_panes(s); s->timeline_visible_seconds = 10.0f;
    effects_panel_input_init(s);
    while (engine_get_track_count(s->engine) < 32) assert(engine_add_track(s->engine) >= 0);
    const int sizes[][2] = {{1600,1000},{1100,800},{1920,1200}};
    for (unsigned k = 0; k < sizeof(sizes)/sizeof(sizes[0]); ++k) {
        ui_layout_panes(s, sizes[k][0], sizes[k][1]);
        for (int scale = 0; scale < 3; ++scale) {
            s->timeline_vertical_scale = (float[]){0.5f,1.0f,2.5f}[scale]; reachable(s);
            timeline_set_vertical_scroll(s, 1e9f); TimelineGeometry g = geometry(s);
            assert(s->timeline_vertical_scroll == g.vertical_max);
            timeline_set_vertical_scroll(s, -100); assert(s->timeline_vertical_scroll == 0);
        }
        s->panes[2].visible = !s->panes[2].visible; ui_layout_panes(s, sizes[k][0], sizes[k][1]); reachable(s);
    }
    ui_layout_panes(s,1600,1000); s->timeline_vertical_scale=1; timeline_set_vertical_scroll(s,500);
    daw_editor_controls_sync(s); SDL_Rect height=s->timeline_controls.height_up_rect;
    float scale=s->timeline_vertical_scale; SDL_Event button={.type=SDL_MOUSEBUTTONDOWN};
    button.button.button=SDL_BUTTON_LEFT; button.button.x=height.x+height.w/2; button.button.y=height.y+height.h/2;
    input_manager_handle_event(&s->input_manager,s,&button); assert(s->timeline_vertical_scale==scale);
    button.type=SDL_MOUSEBUTTONUP; input_manager_handle_event(&s->input_manager,s,&button);
    assert(s->timeline_vertical_scale>scale); s->timeline_vertical_scale=1; timeline_set_vertical_scroll(s,500);
    float start=s->timeline_window_start_seconds; wheel(s,0,-0.25f,KMOD_NONE,SDL_MOUSEWHEEL_NORMAL);
    assert(fabsf(s->timeline_vertical_scroll-515)<0.01f && s->timeline_window_start_seconds==start);
    wheel(s,0,-0.25f,KMOD_NONE,SDL_MOUSEWHEEL_FLIPPED); assert(fabsf(s->timeline_vertical_scroll-500)<0.01f);
    wheel(s,-0.5f,-0.5f,KMOD_NONE,SDL_MOUSEWHEEL_NORMAL); assert(s->timeline_vertical_scroll>500 && s->timeline_window_start_seconds>start);
    float vertical=s->timeline_vertical_scroll; wheel(s,0,-1,KMOD_SHIFT,SDL_MOUSEWHEEL_NORMAL);
    assert(s->timeline_vertical_scroll==vertical);
    TimelineGeometry before=geometry(s); float row=(before.vertical_scroll+100)/(before.track_height+before.track_spacing);
    wheel(s,0,2,KMOD_CTRL,SDL_MOUSEWHEEL_NORMAL); TimelineGeometry after=geometry(s);
    assert(fabsf((after.vertical_scroll+100)/(after.track_height+after.track_spacing)-row)<0.01f);
    before=geometry(s); int center=before.viewport.h/2;
    row=(before.vertical_scroll+center)/(before.track_height+before.track_spacing);
    SDL_Event slider={.type=SDL_MOUSEBUTTONDOWN}; slider.button.button=SDL_BUTTON_LEFT;
    slider.button.x=s->transport_ui.vert_track_rect.x+s->transport_ui.vert_track_rect.w*3/4;
    slider.button.y=s->transport_ui.vert_track_rect.y+s->transport_ui.vert_track_rect.h/2;
    transport_input_handle_event(&s->input_manager,s,&slider); assert(s->input_manager.prev_vert_slider_down);
    after=geometry(s); assert(fabsf((after.vertical_scroll+center)/(after.track_height+after.track_spacing)-row)<0.01f);
    slider.type=SDL_MOUSEBUTTONUP; transport_input_handle_event(&s->input_manager,s,&slider);
    assert(!s->input_manager.prev_vert_slider_down);
    float visible=s->timeline_visible_seconds; wheel(s,0,1,KMOD_ALT,SDL_MOUSEWHEEL_NORMAL); assert(s->timeline_visible_seconds<visible);
    timeline_set_vertical_scroll(s,0); TimelineGeometry g=geometry(s);
    SDL_Event e={.type=SDL_MOUSEBUTTONDOWN}; e.button.button=SDL_BUTTON_LEFT;
    e.button.x=g.scroll_thumb.x+2; e.button.y=g.scroll_thumb.y+3;
    assert(timeline_scrollbar_handle_event(s,&e) && s->timeline_scrollbar_dragging);
    e.type=SDL_MOUSEMOTION; e.motion.x=0; e.motion.y=g.scrollbar.y+g.scrollbar.h+200;
    assert(timeline_scrollbar_handle_event(s,&e)); g=geometry(s); assert(s->timeline_vertical_scroll==g.vertical_max);
    e.type=SDL_MOUSEBUTTONUP; e.button.button=SDL_BUTTON_LEFT; e.button.y=g.scrollbar.y+g.scrollbar.h+200;
    assert(timeline_scrollbar_handle_event(s,&e) && !s->timeline_scrollbar_dragging);
    timeline_selection_set_single(s,0,-1); assert(s->timeline_vertical_scroll==0);
    timeline_selection_set_single(s,31,-1); g=geometry(s);
    assert(g.track_top+31*(g.track_height+g.track_spacing)+g.track_height<=g.viewport.y+g.viewport.h);
    ui_layout_panes(s,1100,800); g=geometry(s);
    int selected_top=g.track_top+31*(g.track_height+g.track_spacing);
    assert(selected_top>=g.viewport.y && selected_top+g.track_height<=g.viewport.y+g.viewport.h);
    ui_layout_panes(s,1600,1000); timeline_reveal_track(s,31);
    daw_editor_controls_sync(s); const EngineTrack* tracks=engine_get_tracks(s->engine);
    bool last=false,first=false; KitUiSurface* surface=&s->editor_controls.surface;
    for(unsigned i=0;i<surface->count;++i) if(surface->keys[i].domain==15) {
        if(surface->keys[i].value==tracks[31].runtime_id*2) last=true;
        if(surface->keys[i].value==tracks[0].runtime_id*2) first=true;
    }
    assert(last && !first);
    if (argc == 2) {
        SDL_Window* window=SDL_CreateWindow("soniCs timeline navigation proof",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
            1600,1000,SDL_WINDOW_VULKAN|SDL_WINDOW_ALLOW_HIGHDPI); assert(window);
        VkRenderer renderer={0}; VkRendererConfig config; vk_renderer_config_set_defaults(&config); config.enable_validation=SDL_TRUE;
        assert(vk_renderer_init(&renderer,window,&config)==VK_SUCCESS);
        s->timeline_vertical_scale=2.5f; timeline_selection_set_single(s,0,-1); capture(&renderer,s,argv[1],"top-large");
        timeline_selection_set_single(s,31,-1); capture(&renderer,s,argv[1],"bottom-large");
        s->panes[2].visible=true; s->layout_runtime.mixer_ratio=0.45f; ui_layout_panes(s,1100,800);
        timeline_reveal_track(s,31); capture(&renderer,s,argv[1],"bottom-resized-panel");
        ui_font_invalidate_cache((SDL_Renderer*)&renderer); vk_renderer_shutdown(&renderer); SDL_DestroyWindow(window);
    }
    while(engine_get_track_count(s->engine)>1) assert(engine_remove_track(s->engine,engine_get_track_count(s->engine)-1));
    timeline_set_vertical_scroll(s,s->timeline_vertical_scroll); assert(s->timeline_vertical_scroll==0 && geometry(s).vertical_max==0);
    tempo_map_free(&s->tempo_map); time_signature_map_free(&s->time_signature_map);
    engine_destroy(s->engine); free(s); ui_font_shutdown(); TTF_Quit(); SDL_Quit();
    puts("timeline navigation: all 32 rows reachable at three scales/window sizes; wheel, zoom, capture, selection and bounds passed");
}
