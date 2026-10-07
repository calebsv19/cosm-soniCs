#include "ui/editor_controls_internal.h"
#include "app_state.h"
#include "ui/layout.h"
#include "ui/timeline_view.h"
#include "input/timeline/timeline_geometry.h"
#include "input/effects_panel_input_helpers.h"
#include "ui/effects_panel_eq_detail.h"
#include "ui/effects_panel_meter_detail.h"

// Uses runtime track identities so reordered or replaced tracks cannot inherit a press.
void daw_editor_track_controls_collect(AppState* state, KitUiSurface* surface) {
    const Pane* pane = ui_layout_get_pane(state, 1);
    TimelineGeometry geometry;
    if (!pane || !pane->visible || !timeline_compute_geometry(state, pane, &geometry)) return;
    SDL_Rect clip = ui_layout_pane_content_rect(pane);
    // The ruler/toolbar are painted over this area; only lane content is interactive.
    int bottom = clip.y + clip.h;
    clip.y = geometry.track_top;
    clip.h = bottom - clip.y;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    for (int i = 0; tracks && i < engine_get_track_count(state->engine); ++i) {
        TimelineTrackHeaderLayout header;
        timeline_view_compute_track_header_layout(&pane->rect,
            geometry.track_top + i * (geometry.track_height + geometry.track_spacing),
            geometry.track_height, geometry.header_width, &header);
        uint64_t key = (uint64_t)tracks[i].runtime_id * 2;
        daw_editor_control_add(surface, 15, key, header.mute_rect, clip, true);
        daw_editor_control_add(surface, 15, key + 1, header.solo_rect, clip, true);
    }
}

// Preset menus own their visible rows and exclude controls beneath their overlay.
static void snapshot(AppState* state, KitUiSurface* surface, const EffectsPanelLayout* layout) {
    const EffectsPanelTrackSnapshotLayout* snap = &layout->track_snapshot;
    bool track = state->effects_panel.target == FX_PANEL_TARGET_TRACK &&
        state->effects_panel.target_track_index >= 0 &&
        state->effects_panel.target_track_index < engine_get_track_count(state->engine);
    daw_editor_control_add(surface, 16, 0, snap->instrument_button_rect, layout->list_rect, track);
    if (state->effects_panel.track_snapshot.instrument_menu_open) {
        for (int i = 0; i < snap->instrument_browser.row_count; ++i) {
            const MidiPresetBrowserRow* row = &snap->instrument_browser.rows[i];
            if (row->type == MIDI_PRESET_BROWSER_ROW_EMPTY) continue;
            uint64_t key = row->type == MIDI_PRESET_BROWSER_ROW_CATEGORY ? 1000u + row->category : 2000u + row->preset;
            daw_editor_control_add(surface, 16, key, row->rect, snap->instrument_menu_rect, track);
        }
        return;
    }
    daw_editor_control_add(surface, 16, 1, snap->mute_rect, layout->list_rect, track);
    daw_editor_control_add(surface, 16, 2, snap->solo_rect, layout->list_rect, track);
}

// Reuses the EQ selector/toggle geometry; curve handles and double-click reset remain local.
static void equalizer(AppState* state, KitUiSurface* surface, SDL_Rect clip) {
    SDL_Rect master, track, low, mids[4], high;
    effects_panel_eq_detail_compute_selector_rects(&clip, &master, &track);
    effects_panel_eq_detail_compute_toggle_rects(&clip, &low, mids, &high);
    daw_editor_control_add(surface, 17, 0, master, clip, true);
    daw_editor_control_add(surface, 17, 1, track, clip,
        state->effects_panel.target == FX_PANEL_TARGET_TRACK && state->effects_panel.target_track_index >= 0);
    daw_editor_control_add(surface, 17, 2, low, clip, true);
    for (int i = 0; i < 4; ++i) daw_editor_control_add(surface, 17, 3 + i, mids[i], clip, true);
    daw_editor_control_add(surface, 17, 7, high, clip, true);
}

// Registers only spec toggles/dropdowns and time-mode buttons, never slider/knob hit regions.
static void parameters(AppState* state, KitUiSurface* surface, int index, const EffectsSlotLayout* layout) {
    EffectsPanelState* panel = &state->effects_panel;
    const FxSlotUIState* slot = &panel->chain[index];
    uint64_t base = (uint64_t)slot->id * 128;
    if (panel_uses_spec_ui(panel, slot)) {
        EffectsSpecPanelLayout spec;
        effects_panel_spec_compute_layout(state, panel, slot, &layout->body_rect,
            panel->slot_runtime[index].scroll, &spec);
        SDL_Rect clip = effects_panel_spec_body_clip_rect(&layout->body_rect);
        for (int i = 0; i < spec.widget_count; ++i) {
            const FxSpecWidget* widget = &spec.widgets[i];
            if (widget->type == FX_SPEC_WIDGET_TOGGLE || widget->type == FX_SPEC_WIDGET_DROPDOWN)
                daw_editor_control_add(surface, 18, base + widget->param_index * 2,
                    widget->control_rect, clip, true);
            daw_editor_control_add(surface, 18, base + widget->param_index * 2 + 1,
                widget->mode_rect, clip, true);
        }
    } else {
        for (unsigned i = 0; i < slot->param_count && i < FX_MAX_PARAMS; ++i)
            daw_editor_control_add(surface, 18, base + i * 2 + 1, layout->mode_rects[i], layout->body_rect, true);
    }
}

// Collects exactly the specialized view that is actually painted, excluding hidden detail chrome.
void daw_editor_effect_exceptions_collect(AppState* state, KitUiSurface* surface, const EffectsPanelLayout* layout) {
    EffectsPanelState* panel = &state->effects_panel;
    if (layout->overlay_visible) return;
    if (panel->view_mode == FX_PANEL_VIEW_LIST) {
        snapshot(state, surface, layout);
        if (panel->track_snapshot.instrument_menu_open) return;
        if (panel->list_detail_mode == FX_LIST_DETAIL_EQ) {
            equalizer(state, surface, layout->detail_rect);
        } else {
            int index = panel->list_open_slot_index;
            if (index < 0 || index >= panel->chain_count) return;
            if (panel->list_detail_mode == FX_LIST_DETAIL_EFFECT) {
                EffectsSlotLayout detail;
                if (compute_detail_slot_layout(state, layout, index, &detail)) parameters(state, surface, index, &detail);
            } else if (panel->list_detail_mode == FX_LIST_DETAIL_METER) {
                SDL_Rect buttons[3] = {{0}};
                FxTypeId type = panel->chain[index].type_id;
                if (type == 102u) effects_panel_meter_detail_compute_toggle_rects(&layout->detail_rect, &buttons[0], &buttons[1]);
                else if (type == 104u) effects_panel_meter_detail_compute_lufs_toggle_rects(&layout->detail_rect, &buttons[0], &buttons[1], &buttons[2]);
                else if (type == 105u) effects_panel_meter_detail_compute_spectrogram_toggle_rects(&layout->detail_rect, &buttons[0], &buttons[1], &buttons[2]);
                for (int i = 0; i < 3; ++i) daw_editor_control_add(surface, 18,
                    (uint64_t)panel->chain[index].id * 128 + 100 + i, buttons[i], layout->detail_rect, true);
            }
        }
    } else {
        for (int i = 0; i < layout->column_count && i < panel->chain_count; ++i) {
            if (panel->chain[i].type_id == 105u) {
                SDL_Rect buttons[3];
                effects_panel_spectrogram_card_palette_rects(&layout->slots[i].body_rect, buttons);
                for (int j = 0; j < 3; ++j) daw_editor_control_add(surface, 18,
                    (uint64_t)panel->chain[i].id * 128 + 100 + j, buttons[j], layout->slots[i].body_rect, true);
            } else parameters(state, surface, i, &layout->slots[i]);
        }
    }
}
