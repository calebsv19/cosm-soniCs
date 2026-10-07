#include "app_state.h"
#include "ui/editor_controls.h"
#include "ui/layout.h"
#include "ui/midi_editor.h"
#include "ui/midi_instrument_panel.h"
#include "ui/effects_panel.h"
#include "input/effects_panel_input.h"
#include "input/project_modal_input.h"
#include "input/timeline_selection.h"
#include "input/timeline/timeline_drop.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Delivers real application pointer edges, including a same-frame down/up pair.
static void pointer(AppState* state, Uint32 type, SDL_Rect r) {
    SDL_Event e={.type=type}; e.button.button=SDL_BUTTON_LEFT; e.button.clicks=1;
    e.button.x=r.x+r.w/2; e.button.y=r.y+r.h/2;
    state->mouse_x=e.button.x; state->mouse_y=e.button.y;
    input_manager_handle_event(&state->input_manager,state,&e);
}
// Sends a key edge through product routing rather than directly to the kit.
static void key(AppState* state, Uint32 type, SDL_Keycode code, bool repeat) {
    SDL_Event e={.type=type}; e.key.keysym.sym=code; e.key.repeat=repeat;
    input_manager_handle_event(&state->input_manager,state,&e);
}
// Finds registered product geometry through stable semantic keys.
static SDL_Rect control(AppState* state, unsigned domain, uint64_t key) {
    daw_editor_controls_sync(state); KitUiSurface* surface=&state->editor_controls.surface;
    for(unsigned i=0;i<surface->count;++i) if(surface->keys[i].domain==domain && surface->keys[i].value==key) {
        KitRenderRect r=surface->controls[i].bounds; return (SDL_Rect){r.x,r.y,r.width,r.height};
    }
    fprintf(stderr,"missing domain=%u key=%llu count=%u status=%d selected=%d/%d mode=%d\n",domain,(unsigned long long)key,surface->count,surface->collection_result.code,state->selected_track_index,state->selected_clip_index,state->midi_editor_ui.panel_mode);
    assert(!"missing visible control"); return (SDL_Rect){0};
}
// Verifies no command on down, outside release, changed geometry, takeover or key repeats.
static void timeline_and_library(AppState* s) {
    SDL_Rect project=control(s,10,1); LibraryPanelMode mode=s->library.panel_mode;
    pointer(s,SDL_MOUSEBUTTONDOWN,project); assert(s->library.panel_mode==mode);
    timeline_drop_handle_library_drag(&s->input_manager,s,false,true);
    assert(s->library.panel_mode==mode); // Sampled state must not replay header commands.
    pointer(s,SDL_MOUSEBUTTONUP,project); assert(s->library.panel_mode==LIBRARY_PANEL_MODE_IN_PROJECT);
    SDL_Rect source=control(s,10,0); pointer(s,SDL_MOUSEBUTTONDOWN,source);
    pointer(s,SDL_MOUSEBUTTONUP,(SDL_Rect){-100,-100,1,1}); assert(s->library.panel_mode==LIBRARY_PANEL_MODE_IN_PROJECT);
    pointer(s,SDL_MOUSEBUTTONDOWN,source); pointer(s,SDL_MOUSEBUTTONUP,source);
    assert(s->library.panel_mode==LIBRARY_PANEL_MODE_SOURCE);
    SDL_Rect snap=control(s,11,4); bool before=s->timeline_snap_enabled;
    pointer(s,SDL_MOUSEBUTTONDOWN,snap); assert(s->timeline_snap_enabled==before);
    pointer(s,SDL_MOUSEBUTTONUP,snap); assert(s->timeline_snap_enabled!=before);
    before=s->timeline_snap_enabled;
    key(s,SDL_KEYDOWN,SDLK_RETURN,false); key(s,SDL_KEYDOWN,SDLK_RETURN,true);
    assert(s->timeline_snap_enabled==before); key(s,SDL_KEYUP,SDLK_RETURN,false);
    assert(s->timeline_snap_enabled!=before);
    before=s->timeline_snap_enabled; pointer(s,SDL_MOUSEBUTTONDOWN,snap);
    s->panes[1].rect.x+=20; pointer(s,SDL_MOUSEBUTTONUP,snap); assert(s->timeline_snap_enabled==before);
    s->panes[1].rect.x-=20; snap=control(s,11,4);
    pointer(s,SDL_MOUSEBUTTONDOWN,snap); project_modal_input_open_save_prompt(s);
    pointer(s,SDL_MOUSEBUTTONUP,snap); assert(s->timeline_snap_enabled==before);
    project_modal_input_close_save_prompt(s);
    pointer(s,SDL_MOUSEBUTTONDOWN,snap);
    SDL_Event lost={.type=SDL_WINDOWEVENT}; lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    input_manager_handle_event(&s->input_manager,s,&lost);
    pointer(s,SDL_MOUSEBUTTONUP,snap); assert(s->timeline_snap_enabled==before);
    int n=engine_get_track_count(s->engine), undo=s->undo.undo_count; SDL_Rect add=control(s,11,0);
    pointer(s,SDL_MOUSEBUTTONDOWN,add); assert(engine_get_track_count(s->engine)==n);
    pointer(s,SDL_MOUSEBUTTONUP,add); assert(engine_get_track_count(s->engine)==n+1 && s->undo.undo_count==undo+1);
    undo_manager_undo(&s->undo,s); assert(engine_get_track_count(s->engine)==n);
    SDL_Rect loop=control(s,11,3); pointer(s,SDL_MOUSEBUTTONDOWN,loop); assert(!s->loop_enabled);
    pointer(s,SDL_MOUSEBUTTONUP,loop); assert(s->loop_enabled);
}
// Qualifies effects presentation/navigation and event-bound modifier behavior.
static void effects(AppState* s) {
    effects_panel_input_init(s); s->inspector.visible=false;
    SDL_Rect view=control(s,14,0); int mode=s->effects_panel.view_mode;
    pointer(s,SDL_MOUSEBUTTONDOWN,view); assert(s->effects_panel.view_mode==mode);
    pointer(s,SDL_MOUSEBUTTONUP,view); assert(s->effects_panel.view_mode!=mode);
    SDL_Rect spec=control(s,14,1); bool before=s->effects_panel.spec_panel_enabled;
    pointer(s,SDL_MOUSEBUTTONDOWN,spec); assert(s->effects_panel.spec_panel_enabled==before);
    pointer(s,SDL_MOUSEBUTTONUP,spec); assert(s->effects_panel.spec_panel_enabled!=before);
    SDL_Rect add=control(s,14,3); pointer(s,SDL_MOUSEBUTTONDOWN,add);
    assert(s->effects_panel.overlay_layer==FX_PANEL_OVERLAY_CLOSED);
    pointer(s,SDL_MOUSEBUTTONUP,add); assert(s->effects_panel.overlay_layer==FX_PANEL_OVERLAY_CATEGORIES);
    // Cancel a category press if its owning layer changes before release.
    EffectsPanelLayout layout; effects_panel_compute_layout(s,&layout);
    if(layout.overlay_item_count) {
        SDL_Rect row=layout.overlay_item_rects[0]; pointer(s,SDL_MOUSEBUTTONDOWN,row);
        s->effects_panel.overlay_layer=FX_PANEL_OVERLAY_CLOSED; pointer(s,SDL_MOUSEBUTTONUP,row);
        assert(s->effects_panel.overlay_layer==FX_PANEL_OVERLAY_CLOSED);
    }
    s->effects_panel.overlay_layer=FX_PANEL_OVERLAY_CLOSED;
    s->effects_panel.view_mode=FX_PANEL_VIEW_STACK;
    FxInstId id=undo_manager_add_effect(s,-1,1); assert(id); effects_panel_sync_from_engine(s);
    int slot=s->effects_panel.chain_count-1; uint64_t slot_key=(uint64_t)id*16+8;
    SDL_Rect toggle=control(s,14,slot_key); bool enabled=s->effects_panel.chain[slot].enabled;
    pointer(s,SDL_MOUSEBUTTONDOWN,toggle); assert(s->effects_panel.chain[slot].enabled==enabled);
    pointer(s,SDL_MOUSEBUTTONUP,toggle); assert(s->effects_panel.chain[slot].enabled!=enabled);
    SDL_Rect remove=control(s,14,slot_key+1); int count=s->effects_panel.chain_count, undo=s->undo.undo_count;
    pointer(s,SDL_MOUSEBUTTONDOWN,remove); assert(s->effects_panel.chain_count==count);
    pointer(s,SDL_MOUSEBUTTONUP,remove); assert(s->effects_panel.chain_count==count-1 && s->undo.undo_count==undo+1);
    undo_manager_undo(&s->undo,s); effects_panel_sync_from_engine(s); assert(s->effects_panel.chain_count==count);

}
// Preserves MIDI command behavior and cancels presses when the selected region changes.
static void midi(AppState* s) {
    int clip=-1; assert(engine_add_midi_clip_to_track(s->engine,0,0,96000,&clip));
    timeline_selection_set_single(s,0,clip); s->active_track_index=0; s->inspector.visible=true;
    MidiEditorLayout layout; midi_editor_compute_layout(s,&layout);
    SDL_Rect up=control(s,12,7); int octave=s->midi_editor_ui.qwerty_octave_offset;
    pointer(s,SDL_MOUSEBUTTONDOWN,up); assert(s->midi_editor_ui.qwerty_octave_offset==octave);
    pointer(s,SDL_MOUSEBUTTONUP,up); assert(s->midi_editor_ui.qwerty_octave_offset==octave+1);
    SDL_Rect velocity=control(s,12,9); float v=s->midi_editor_ui.default_velocity;
    pointer(s,SDL_MOUSEBUTTONDOWN,velocity); pointer(s,SDL_MOUSEBUTTONUP,velocity);
    assert(s->midi_editor_ui.default_velocity>=v);
    SDL_Rect edit=control(s,12,1); pointer(s,SDL_MOUSEBUTTONDOWN,edit);
    assert(s->midi_editor_ui.panel_mode==MIDI_REGION_PANEL_EDITOR);
    pointer(s,SDL_MOUSEBUTTONUP,edit); assert(s->midi_editor_ui.panel_mode==MIDI_REGION_PANEL_INSTRUMENT);
    MidiInstrumentPanelLayout instrument; midi_instrument_panel_compute_layout(s,&instrument);
    SDL_Rect tab=control(s,13,11); int group=s->midi_editor_ui.instrument_active_group;
    pointer(s,SDL_MOUSEBUTTONDOWN,tab); assert(s->midi_editor_ui.instrument_active_group==group);
    pointer(s,SDL_MOUSEBUTTONUP,tab); assert(s->midi_editor_ui.instrument_active_group==1);
    SDL_Rect notes=control(s,13,0); pointer(s,SDL_MOUSEBUTTONDOWN,notes); pointer(s,SDL_MOUSEBUTTONUP,notes);
    assert(s->midi_editor_ui.panel_mode==MIDI_REGION_PANEL_EDITOR);
    up=control(s,12,7); octave=s->midi_editor_ui.qwerty_octave_offset;
    pointer(s,SDL_MOUSEBUTTONDOWN,up); int other=-1; assert(engine_add_midi_clip_to_track(s->engine,0,100000,96000,&other));
    timeline_selection_set_single(s,0,other); pointer(s,SDL_MOUSEBUTTONUP,up);
    assert(s->midi_editor_ui.qwerty_octave_offset==octave);
    SDL_Rect menu=control(s,12,0); pointer(s,SDL_MOUSEBUTTONDOWN,menu); pointer(s,SDL_MOUSEBUTTONUP,menu);
    assert(s->midi_editor_ui.instrument_menu_open);
    midi_editor_compute_layout(s,&layout);
    assert(layout.instrument_browser.row_count>0);
    SDL_Rect row=layout.instrument_browser.rows[0].rect;
    pointer(s,SDL_MOUSEBUTTONDOWN,row); assert(s->midi_editor_ui.instrument_menu_expanded_category==-1);
    pointer(s,SDL_MOUSEBUTTONUP,row); assert(s->midi_editor_ui.instrument_menu_expanded_category>=0);
}
// Runs production-linked owner checks with an offline engine and isolated dummy platform.
int main(void) {
    SDL_setenv("SDL_VIDEODRIVER","dummy",1); SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS));
    AppState* s=calloc(1,sizeof(*s)); assert(s); EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    s->engine=engine_create(&cfg); assert(s->engine); undo_manager_init(&s->undo); input_manager_init(&s->input_manager);
    s->active_track_index=0; s->selected_track_index=-1; s->selected_clip_index=-1; s->pane_count=4;
    ui_init_panes(s);
    ui_layout_panes(s,1600,1000);
    timeline_and_library(s); effects(s); midi(s);
    undo_manager_free(&s->undo); engine_destroy(s->engine); free(s); SDL_Quit();
    puts("shared_editor_controls_test: success (five control groups, matching release, cancellation, focus and original commands)");
}
