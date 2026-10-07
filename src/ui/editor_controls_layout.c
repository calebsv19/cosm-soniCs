#include "ui/editor_controls.h"
#include "app_state.h"
#include "ui/layout.h"
#include "ui/timeline_view_controls.h"
#include "ui/midi_editor.h"
#include "ui/midi_instrument_panel.h"
#include "ui/effects_panel.h"
#include "input/effects_panel_input_helpers.h"
#include "input/project_modal_input.h"
#include "input/library_input.h"
#include "input/inspector_input.h"

// Combines meaningful identities, never borrowed pointer buffers or padded structs.
static uint64_t mix(uint64_t hash, uint64_t value) { return (hash ^ value) * 1099511628211ULL; }

// Registers only visible portions, borrowing geometry from the original product layouts.
static void add(KitUiSurface* surface, unsigned domain, uint64_t key, SDL_Rect rect, SDL_Rect clip, bool enabled) {
    if (rect.w <= 0 || rect.h <= 0 || clip.w <= 0 || clip.h <= 0) return;
    KitRenderRect bounds = {rect.x,rect.y,rect.w,rect.h};
    KitRenderRect visible = {clip.x,clip.y,clip.w,clip.h};
    (void)kit_ui_surface_register(surface,(KitUiSurfaceKey){domain,key},bounds,&visible,enabled,NULL);
}

// Preset/category keys follow product identities rather than scrolled row positions.
static void presets(KitUiSurface* surface, unsigned domain, const MidiPresetBrowserLayout* browser) {
    for (int i = 0; i < browser->row_count; ++i) {
        const MidiPresetBrowserRow* row = &browser->rows[i];
        if (row->type == MIDI_PRESET_BROWSER_ROW_EMPTY) continue;
        uint64_t key = row->type == MIDI_PRESET_BROWSER_ROW_CATEGORY ? 1000u+row->category : 2000u+row->preset;
        add(surface,domain,key,row->rect,browser->menu_rect,true);
    }
}

// Collects each independent discrete group while leaving drags and sliders app-owned.
static void collect(AppState* state, KitUiSurface* surface) {
    const Pane* library = ui_layout_get_pane(state,3);
    if (library && library->visible) {
        SDL_Rect header = library->rect;
        header.h = ui_layout_pane_header_height(library);
        SDL_Rect source, project;
        library_browser_mode_rects(&header,&source,&project);
        add(surface,10,0,source,header,true); add(surface,10,1,project,header,true);
    }
    const Pane* timeline = ui_layout_get_pane(state,1);
    if (timeline && timeline->visible) {
        SDL_Rect content = ui_layout_pane_content_rect(timeline);
        timeline_view_controls_compute_layout(content.x,content.y,content.w,&state->timeline_controls);
        TimelineControlsUI* c = &state->timeline_controls;
        SDL_Rect buttons[] = {c->add_rect,c->remove_rect,c->midi_region_rect,c->loop_toggle_rect,
            c->snap_toggle_rect,c->automation_toggle_rect,c->automation_target_rect,c->tempo_toggle_rect,
            c->automation_label_toggle_rect};
        for (unsigned i = 0; i < sizeof(buttons)/sizeof(buttons[0]); ++i)
            add(surface,11,i,buttons[i],content,(i!=1 && i!=2) || engine_get_track_count(state->engine)>0);
    }
    const Pane* mixer = ui_layout_get_pane(state,2);
    if (!mixer || !mixer->visible) return;
    if (midi_instrument_panel_should_render(state)) {
        MidiInstrumentPanelLayout layout; midi_instrument_panel_compute_layout(state,&layout);
        add(surface,13,0,layout.notes_button_rect,layout.panel_rect,true);
        add(surface,13,1,layout.preset_button_rect,layout.panel_rect,true);
        if (state->midi_editor_ui.instrument_menu_open) presets(surface,13,&layout.preset_browser);
        else for (int i=0;i<layout.group_tab_count;++i) add(surface,13,10+i,layout.group_tab_rects[i],layout.panel_rect,true);
    } else if (midi_editor_should_render(state)) {
        MidiEditorLayout layout; midi_editor_compute_layout(state,&layout);
        add(surface,12,0,layout.instrument_button_rect,layout.panel_rect,true);
        if (state->midi_editor_ui.instrument_menu_open) presets(surface,12,&layout.instrument_browser);
        else {
            SDL_Rect buttons[]={layout.instrument_panel_button_rect,layout.test_button_rect,layout.quantize_button_rect,
                layout.quantize_down_button_rect,layout.quantize_up_button_rect,layout.octave_down_button_rect,
                layout.octave_up_button_rect,layout.velocity_down_button_rect,layout.velocity_up_button_rect};
            for(unsigned i=0;i<sizeof(buttons)/sizeof(buttons[0]);++i) add(surface,12,i+1,buttons[i],layout.panel_rect,true);
        }
    } else if (!state->inspector.visible) {
        EffectsPanelLayout layout; effects_panel_compute_layout(state,&layout);
        EffectsPanelState* panel=&state->effects_panel;
        SDL_Rect buttons[]={layout.view_toggle_rect,layout.spec_toggle_rect,layout.preview_toggle_rect,layout.dropdown_button_rect};
        for(unsigned i=0;i<4;++i) add(surface,14,i,buttons[i],layout.panel_rect,true);
        if (layout.overlay_visible) {
            add(surface,14,4,layout.overlay_back_rect,layout.overlay_rect,true);
            for(int i=0;i<layout.overlay_item_count;++i) add(surface,14,1000+layout.overlay_item_order[i],layout.overlay_item_rects[i],layout.overlay_rect,true);
        } else if (panel->view_mode==FX_PANEL_VIEW_STACK) {
            for(int i=0;i<layout.column_count && i<panel->chain_count;++i) {
                uint64_t key=(uint64_t)panel->chain[i].id*16+8;
                add(surface,14,key,layout.slots[i].toggle_rect,layout.panel_rect,true);
                add(surface,14,key+1,layout.slots[i].remove_rect,layout.panel_rect,true);
                add(surface,14,key+2,layout.slots[i].preview_toggle_rect,layout.panel_rect,true);
            }
        } else {
            for(int row=0;row<layout.list_row_count && row<panel->chain_count;++row)
                add(surface,14,(uint64_t)panel->chain[row].id*16+11,layout.list_toggle_rects[row],layout.list_rect,true);
            int i=panel->list_open_slot_index; EffectsSlotLayout detail;
            if(compute_detail_slot_layout(state,&layout,i,&detail)) {
                uint64_t key=(uint64_t)panel->chain[i].id*16+8;
                add(surface,14,key,detail.toggle_rect,layout.detail_rect,true);
                add(surface,14,key+1,detail.remove_rect,layout.detail_rect,true);
                add(surface,14,key+2,detail.preview_toggle_rect,layout.detail_rect,true);
            }
        }
    }
}

void daw_editor_controls_sync(AppState* state) {
    if (!state) return;
    DawEditorControls* ui=&state->editor_controls;
    bool unavailable=!state->engine || state->bounce_active || project_modal_input_active(state) ||
        state->tempo_ui.editing || state->track_name_editor.editing || library_input_is_editing(state) ||
        inspector_input_has_text_focus(state) || state->undo.active_drag_valid || state->layout_runtime.drag.active ||
        daw_workspace_authoring_host_active(&state->workspace_authoring);
    uint64_t hash=mix(1469598103934665603ULL,(uintptr_t)state->engine);
    hash=mix(hash,unavailable); hash=mix(hash,state->window_width); hash=mix(hash,state->window_height);
    hash=mix(hash,state->active_track_index); hash=mix(hash,state->selected_track_index); hash=mix(hash,state->selected_clip_index);
    hash=mix(hash,state->midi_editor_ui.selected_clip_creation_index); hash=mix(hash,state->midi_editor_ui.panel_mode);
    hash=mix(hash,state->midi_editor_ui.instrument_menu_open); hash=mix(hash,state->midi_editor_ui.instrument_active_group);
    hash=mix(hash,state->effects_panel.target_track_index); hash=mix(hash,state->effects_panel.view_mode);
    hash=mix(hash,state->effects_panel.overlay_layer); hash=mix(hash,state->effects_panel.active_category_index);
    hash=mix(hash,state->effects_panel.list_open_slot_index);
    if(state->engine) {
        int n=engine_get_track_count(state->engine); hash=mix(hash,n);
        const EngineTrack* tracks=engine_get_tracks(state->engine);
        for(int i=0;i<n;++i) hash=mix(hash,tracks[i].runtime_id);
        if(state->selected_track_index>=0 && state->selected_track_index<n) {
            const EngineTrack* track=&tracks[state->selected_track_index];
            if(state->selected_clip_index>=0 && state->selected_clip_index<track->clip_count)
                hash=mix(hash,track->clips[state->selected_clip_index].creation_index);
        }
    }
    for(int i=0;i<state->effects_panel.chain_count;++i) hash=mix(hash,state->effects_panel.chain[i].id);
    if(ui->context!=hash) { ui->context=hash; ++ui->generation; if(unavailable) ui->keyboard_focus=false; }
    kit_ui_surface_begin(&ui->surface,ui->generation);
    if(!unavailable) collect(state,&ui->surface);
    // Geometry changes cancel both captured pointer and armed key before the new frame is accepted.
    for(unsigned i=0;i<ui->surface.count;++i) {
        KitUiInteractionControl old=ui->surface.controls[i];
        if(old.id!=ui->surface.interaction.captured_id && old.id!=ui->surface.interaction.key_owner_id) continue;
        for(unsigned j=0;j<ui->surface.next_count;++j) if(ui->surface.next_controls[j].id==old.id) {
            KitRenderRect next=ui->surface.next_controls[j].bounds;
            if(old.bounds.x!=next.x || old.bounds.y!=next.y || old.bounds.width!=next.width || old.bounds.height!=next.height) {
                KitUiInteractionResult ignored; KitUiInteractionEvent cancel={.type=KIT_UI_INTERACTION_CANCEL};
                (void)kit_ui_interaction_route(&ui->surface.interaction,ui->surface.controls,ui->surface.count,&cancel,&ignored);
            }
        }
    }
    (void)kit_ui_surface_end(&ui->surface);
}
