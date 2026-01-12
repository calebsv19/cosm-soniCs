#include "input/effects_panel_input.h"

#include "app_state.h"
#include "engine/engine.h"
#include "effects/param_utils.h"
#include "input/effects_panel_eq_detail_input.h"
#include "input/effects_panel_track_snapshot.h"
#include "input/timeline_input.h"
#include "ui/effects_panel.h"
#include "ui/effects_panel_meter_detail.h"
#include "ui/font.h"
#include "undo/undo_manager.h"

#include <stdbool.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool panel_targets_track(const EffectsPanelState* panel);

static void fx_instance_from_slot(const FxSlotUIState* slot, SessionFxInstance* out_instance) {
    if (!slot || !out_instance) {
        return;
    }
    memset(out_instance, 0, sizeof(*out_instance));
    out_instance->type = slot->type_id;
    out_instance->enabled = slot->enabled;
    out_instance->param_count = slot->param_count;
    for (uint32_t i = 0; i < slot->param_count && i < FX_MAX_PARAMS; ++i) {
        out_instance->params[i] = slot->param_values[i];
        out_instance->param_mode[i] = slot->param_mode[i];
        out_instance->param_beats[i] = slot->param_beats[i];
    }
    out_instance->name[0] = '\0';
}

static int find_slot_index_by_id(const EffectsPanelState* panel, FxInstId id) {
    if (!panel) {
        return -1;
    }
    for (int i = 0; i < panel->chain_count; ++i) {
        if (panel->chain[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void begin_fx_param_drag(AppState* state, int slot_index, int param_index) {
    if (!state) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (slot_index < 0 || slot_index >= panel->chain_count) {
        return;
    }
    FxSlotUIState* slot = &panel->chain[slot_index];
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_FX_EDIT;
    cmd.data.fx_edit.kind = UNDO_FX_EDIT_PARAM;
    cmd.data.fx_edit.target = panel_targets_track(panel) ? UNDO_FX_TARGET_TRACK : UNDO_FX_TARGET_MASTER;
    cmd.data.fx_edit.track_index = panel_targets_track(panel) ? panel->target_track_index : -1;
    cmd.data.fx_edit.id = slot->id;
    cmd.data.fx_edit.param_index = (uint32_t)param_index;
    fx_instance_from_slot(slot, &cmd.data.fx_edit.before_state);
    cmd.data.fx_edit.after_state = cmd.data.fx_edit.before_state;
    undo_manager_begin_drag(&state->undo, &cmd);
}

static const FxTypeUIInfo* find_type_info(const EffectsPanelState* panel, FxTypeId type_id) {
    if (!panel) {
        return NULL;
    }
    for (int i = 0; i < panel->type_count; ++i) {
        if (panel->types[i].type_id == type_id) {
            return &panel->types[i];
        }
    }
    return NULL;
}

static bool panel_targets_track(const EffectsPanelState* panel) {
    return panel && panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0;
}

static bool compute_detail_slot_layout(const AppState* state,
                                       const EffectsPanelLayout* layout,
                                       int slot_index,
                                       EffectsSlotLayout* out_layout) {
    if (!state || !layout || !out_layout) {
        return false;
    }
    if (slot_index < 0 || slot_index >= state->effects_panel.chain_count) {
        return false;
    }
    SDL_Rect slot_rect = layout->detail_rect;
    if (slot_rect.w <= 0 || slot_rect.h <= 0) {
        return false;
    }
    slot_rect.x += 6;
    slot_rect.y += 6;
    slot_rect.w -= 12;
    slot_rect.h -= 12;
    effects_slot_compute_layout((EffectsPanelState*)&state->effects_panel,
                                slot_index,
                                &slot_rect,
                                FX_PANEL_HEADER_HEIGHT,
                                FX_PANEL_INNER_MARGIN,
                                FX_PANEL_PARAM_GAP,
                                out_layout);
    return true;
}

static bool toggle_slot_enabled(AppState* state, EffectsPanelState* panel, int slot_index) {
    if (!state || !panel || !state->engine) {
        return false;
    }
    if (slot_index < 0 || slot_index >= panel->chain_count) {
        return false;
    }
    FxInstId id = panel->chain[slot_index].id;
    SessionFxInstance before_state;
    fx_instance_from_slot(&panel->chain[slot_index], &before_state);
    bool enabled = !panel->chain[slot_index].enabled;
    bool updated = false;
    if (panel_targets_track(panel)) {
        updated = engine_fx_track_set_enabled(state->engine, panel->target_track_index, id, enabled);
    } else {
        updated = engine_fx_master_set_enabled(state->engine, id, enabled);
    }
    if (id != 0 && updated) {
        effects_panel_sync_from_engine(state);
        int new_index = find_slot_index_by_id(panel, id);
        if (new_index >= 0) {
            UndoCommand cmd = {0};
            cmd.type = UNDO_CMD_FX_EDIT;
            cmd.data.fx_edit.kind = UNDO_FX_EDIT_ENABLE;
            cmd.data.fx_edit.target = panel_targets_track(panel) ? UNDO_FX_TARGET_TRACK : UNDO_FX_TARGET_MASTER;
            cmd.data.fx_edit.track_index = panel_targets_track(panel) ? panel->target_track_index : -1;
            cmd.data.fx_edit.id = id;
            cmd.data.fx_edit.before_state = before_state;
            fx_instance_from_slot(&panel->chain[new_index], &cmd.data.fx_edit.after_state);
            undo_manager_push(&state->undo, &cmd);
        }
    }
    return updated;
}

static int hit_column_index(const EffectsPanelLayout* layout, const EffectsPanelState* panel, const SDL_Point* pt) {
    if (!layout || !panel || !pt) {
        return -1;
    }
    for (int i = 0; i < layout->column_count && i < panel->chain_count; ++i) {
        if (SDL_PointInRect(pt, &layout->slots[i].column_rect)) {
            return i;
        }
    }
    return -1;
}

static float slider_value_from_mouse(const AppState* state,
                                     const EffectsPanelLayout* layout,
                                     int slot_index,
                                     int param_index,
                                     int mouse_x) {
    if (!state || !layout) {
        return 0.0f;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    if (slot_index < 0 || slot_index >= panel->chain_count) {
        return 0.0f;
    }
    const FxSlotUIState* slot = &panel->chain[slot_index];
    if (param_index < 0 || param_index >= (int)slot->param_count) {
        return 0.0f;
    }
    const SDL_Rect* slider_rect = &layout->slots[slot_index].slider_rects[param_index];
    if (slider_rect->w <= 0) {
        return slot->param_values[param_index];
    }
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    float min_v = info ? info->param_min[param_index] : 0.0f;
    float max_v = info ? info->param_max[param_index] : 1.0f;
    if (max_v - min_v < 1e-6f) {
        max_v = min_v + 1.0f;
    }
    FxParamMode mode = slot->param_mode[param_index];
    FxParamKind kind = info ? info->param_kind[param_index] : FX_PARAM_KIND_GENERIC;
    const float beat_min = 1.0f / 64.0f;
    const float beat_max = 8.0f;
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_kind_is_syncable(kind)) {
        min_v = beat_min;
        max_v = beat_max;
    }
    if (min_v > max_v) {
        float tmp = min_v;
        min_v = max_v;
        max_v = tmp;
    }
    float t = (float)(mouse_x - slider_rect->x) / (float)slider_rect->w;
    t = clampf(t, 0.0f, 1.0f);
    float value = min_v + t * (max_v - min_v);
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_kind_is_syncable(kind)) {
        value = fx_param_quantize_beats(value);
        if (value < beat_min) value = beat_min;
        if (value > beat_max) value = beat_max;
    }
    return value;
}

static void apply_slider_value(AppState* state, int slot_index, int param_index, float value) {
    if (!state || !state->engine) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (slot_index < 0 || slot_index >= panel->chain_count) {
        return;
    }
    FxSlotUIState* slot = &panel->chain[slot_index];
    if (param_index < 0 || param_index >= (int)slot->param_count) {
        return;
    }
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    FxParamMode mode = slot->param_mode[param_index];
    FxParamKind kind = info ? info->param_kind[param_index] : FX_PARAM_KIND_GENERIC;
    float beat_value = slot->param_beats[param_index];
    float native_value = value;
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_kind_is_syncable(kind)) {
        beat_value = value;
        native_value = fx_param_beats_to_native(kind, beat_value, &state->tempo);
    } else if (fx_param_kind_is_syncable(kind)) {
        beat_value = fx_param_native_to_beats(kind, value, &state->tempo);
    }
    bool updated = false;
    bool use_sync = (mode != FX_PARAM_MODE_NATIVE) && fx_param_kind_is_syncable(kind);
    if (panel_targets_track(panel)) {
        if (use_sync) {
            updated = engine_fx_track_set_param_with_mode(state->engine,
                                                          panel->target_track_index,
                                                          slot->id,
                                                          (uint32_t)param_index,
                                                          native_value,
                                                          mode,
                                                          beat_value);
        } else {
            updated = engine_fx_track_set_param(state->engine,
                                                panel->target_track_index,
                                                slot->id,
                                                (uint32_t)param_index,
                                                native_value);
        }
    } else {
        if (use_sync) {
            updated = engine_fx_master_set_param_with_mode(state->engine,
                                                           slot->id,
                                                           (uint32_t)param_index,
                                                           native_value,
                                                           mode,
                                                           beat_value);
        } else {
            updated = engine_fx_master_set_param(state->engine, slot->id, (uint32_t)param_index, native_value);
        }
    }
    if (updated) {
        slot->param_values[param_index] = native_value;
        slot->param_beats[param_index] = beat_value;
    }
}

static void toggle_param_mode(AppState* state, int slot_index, int param_index) {
    if (!state || !state->engine) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (slot_index < 0 || slot_index >= panel->chain_count) {
        return;
    }
    FxSlotUIState* slot = &panel->chain[slot_index];
    if (param_index < 0 || param_index >= (int)slot->param_count) {
        return;
    }
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_FX_EDIT;
    cmd.data.fx_edit.kind = UNDO_FX_EDIT_PARAM;
    cmd.data.fx_edit.target = panel_targets_track(panel) ? UNDO_FX_TARGET_TRACK : UNDO_FX_TARGET_MASTER;
    cmd.data.fx_edit.track_index = panel_targets_track(panel) ? panel->target_track_index : -1;
    cmd.data.fx_edit.id = slot->id;
    cmd.data.fx_edit.param_index = (uint32_t)param_index;
    fx_instance_from_slot(slot, &cmd.data.fx_edit.before_state);
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    FxParamKind kind = info ? info->param_kind[param_index] : FX_PARAM_KIND_GENERIC;
    if (!fx_param_kind_is_syncable(kind)) {
        return;
    }
    FxParamMode current = slot->param_mode[param_index];
    FxParamMode next = FX_PARAM_MODE_NATIVE;
    if (current == FX_PARAM_MODE_NATIVE) {
        next = (kind == FX_PARAM_KIND_RATE_HZ) ? FX_PARAM_MODE_BEAT_RATE : FX_PARAM_MODE_BEATS;
    } else {
        next = FX_PARAM_MODE_NATIVE;
    }
    float native_value = slot->param_values[param_index];
    float beat_value = slot->param_beats[param_index];
    const float beat_min = 1.0f / 64.0f;
    const float beat_max = 8.0f;
    if (next != FX_PARAM_MODE_NATIVE) {
        beat_value = fx_param_native_to_beats(kind, native_value, &state->tempo);
        beat_value = fx_param_quantize_beats(beat_value);
        if (beat_value < beat_min) beat_value = beat_min;
        if (beat_value > beat_max) beat_value = beat_max;
        native_value = fx_param_beats_to_native(kind, beat_value, &state->tempo);
    } else if (fx_param_kind_is_syncable(kind)) {
        beat_value = fx_param_native_to_beats(kind, native_value, &state->tempo);
    }
    slot->param_mode[param_index] = next;
    slot->param_beats[param_index] = beat_value;
    apply_slider_value(state, slot_index, param_index, next == FX_PARAM_MODE_NATIVE ? native_value : beat_value);
    fx_instance_from_slot(slot, &cmd.data.fx_edit.after_state);
    undo_manager_push(&state->undo, &cmd);
}

static void close_overlay(EffectsPanelState* panel) {
    panel->overlay_layer = FX_PANEL_OVERLAY_CLOSED;
    panel->hovered_category_index = -1;
    panel->hovered_effect_index = -1;
    panel->active_category_index = -1;
    panel->overlay_scroll_index = 0;
}

void effects_panel_input_init(AppState* state) {
    if (!state) {
        return;
    }
    EffectsPanelViewMode preserved_view = state->effects_panel.view_mode;
    EffectsPanelListDetailMode preserved_detail = state->effects_panel.list_detail_mode;
    EffectsPanelEqDetailView preserved_eq_view = state->effects_panel.eq_detail.view_mode;
    bool preserved_restore = state->effects_panel.restore_pending;
    int preserved_selected = preserved_restore ? state->effects_panel.restore_selected_index
                                               : state->effects_panel.selected_slot_index;
    int preserved_open = preserved_restore ? state->effects_panel.restore_open_index
                                           : state->effects_panel.list_open_slot_index;
    EqCurveState preserved_curve = state->effects_panel.eq_curve;
    EqCurveState preserved_master = state->effects_panel.eq_curve_master;
    EqCurveState* preserved_tracks = state->effects_panel.eq_curve_tracks;
    int preserved_tracks_count = state->effects_panel.eq_curve_tracks_count;
    effects_panel_init(state);
    state->effects_panel.view_mode = preserved_view;
    state->effects_panel.list_detail_mode = preserved_detail;
    state->effects_panel.eq_detail.view_mode = preserved_eq_view;
    state->effects_panel.eq_curve = preserved_curve;
    state->effects_panel.eq_curve_master = preserved_master;
    state->effects_panel.eq_curve_tracks = preserved_tracks;
    state->effects_panel.eq_curve_tracks_count = preserved_tracks_count;
    if (preserved_selected >= 0 || preserved_open >= 0) {
        state->effects_panel.restore_pending = true;
        state->effects_panel.restore_selected_index = preserved_selected;
        state->effects_panel.restore_open_index = preserved_open;
    }
    if (state->engine) {
        effects_panel_refresh_catalog(state);
        effects_panel_sync_from_engine(state);
    }
}

void effects_panel_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    (void)manager;
    if (!state || !event) {
        return;
    }
    if (state->inspector.visible) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (!panel->initialized && state->engine) {
        effects_panel_refresh_catalog(state);
    }

    EffectsPanelLayout layout;
    effects_panel_compute_layout(state, &layout);
    bool overlay_open = (panel->overlay_layer != FX_PANEL_OVERLAY_CLOSED);

    switch (event->type) {
        case SDL_KEYDOWN: {
            if (!panel->focused || panel->selected_slot_index < 0 || overlay_open) {
                break;
            }
            if (state->track_name_editor.editing) {
                break;
            }
            SDL_Keycode key = event->key.keysym.sym;
            if (key != SDLK_LEFT && key != SDLK_RIGHT && key != SDLK_UP && key != SDLK_DOWN) {
                break;
            }
            int selected = panel->selected_slot_index;
            if (selected < 0 || selected >= panel->chain_count) {
                break;
            }
            int new_index = selected;
            SDL_Keymod mods = SDL_GetModState();
            bool move_prev = (key == SDLK_LEFT || key == SDLK_UP);
            bool move_next = (key == SDLK_RIGHT || key == SDLK_DOWN);
            if (move_prev) {
                new_index = (mods & KMOD_SHIFT) ? 0 : (selected - 1);
            } else if (move_next) {
                new_index = (mods & KMOD_SHIFT) ? (panel->chain_count - 1) : (selected + 1);
            }
            if (new_index < 0) new_index = 0;
            if (new_index >= panel->chain_count) new_index = panel->chain_count - 1;
            if (new_index != selected && state->engine) {
                FxInstId id = panel->chain[selected].id;
                bool reordered = false;
                if (panel_targets_track(panel)) {
                    reordered = engine_fx_track_reorder(state->engine, panel->target_track_index, id, new_index);
                } else {
                    reordered = engine_fx_master_reorder(state->engine, id, new_index);
                }
                if (reordered) {
                    UndoCommand cmd = {0};
                    cmd.type = UNDO_CMD_FX_EDIT;
                    cmd.data.fx_edit.kind = UNDO_FX_EDIT_REORDER;
                    cmd.data.fx_edit.target = panel_targets_track(panel) ? UNDO_FX_TARGET_TRACK : UNDO_FX_TARGET_MASTER;
                    cmd.data.fx_edit.track_index = panel_targets_track(panel) ? panel->target_track_index : -1;
                    cmd.data.fx_edit.id = id;
                    cmd.data.fx_edit.before_index = selected;
                    cmd.data.fx_edit.after_index = new_index;
                    undo_manager_push(&state->undo, &cmd);
                    effects_panel_sync_from_engine(state);
                    panel->selected_slot_index = new_index;
                    if (panel->list_open_slot_index == selected) {
                        panel->list_open_slot_index = new_index;
                    }
                }
            }
            break;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (event->button.button != SDL_BUTTON_LEFT) {
                break;
            }
            SDL_Point pt = {event->button.x, event->button.y};
            if (!SDL_PointInRect(&pt, &layout.panel_rect)) {
                panel->focused = false;
                panel->selected_slot_index = -1;
                panel->highlighted_slot_index = -1;
                panel->hovered_toggle_slot_index = -1;
                if (overlay_open) {
                    close_overlay(panel);
                }
                return;
            }
            panel->focused = true;

            if (!overlay_open && panel->view_mode == FX_PANEL_VIEW_LIST) {
                if (SDL_PointInRect(&pt, &layout.list_rect)) {
                    if (effects_panel_track_snapshot_handle_mouse_down(state, &layout, event)) {
                        return;
                    }
                    for (int i = 0; i < layout.list_row_count && i < panel->chain_count; ++i) {
                        if (SDL_PointInRect(&pt, &layout.list_toggle_rects[i])) {
                            toggle_slot_enabled(state, panel, i);
                            return;
                        }
                        if (SDL_PointInRect(&pt, &layout.list_row_rects[i])) {
                            panel->selected_slot_index = i;
                            Uint32 now = SDL_GetTicks();
                            bool is_double = event->button.clicks >= 2;
                            if (!is_double &&
                                panel->list_last_click_ticks != 0 &&
                                panel->list_last_click_index == i &&
                                now - panel->list_last_click_ticks <= 500) {
                                is_double = true;
                            }
                            panel->list_last_click_ticks = now;
                            panel->list_last_click_index = i;
                            if (is_double) {
                                panel->list_open_slot_index = i;
                                FxTypeId type_id = panel->chain[i].type_id;
                                if (type_id >= 100u && type_id <= 109u) {
                                    panel->list_detail_mode = FX_LIST_DETAIL_METER;
                                } else {
                                    panel->list_detail_mode = FX_LIST_DETAIL_EFFECT;
                                }
                                panel->track_snapshot.eq_open = false;
                            }
                            return;
                        }
                    }
                }
                if (panel->list_detail_mode == FX_LIST_DETAIL_EQ) {
                    if (effects_panel_eq_detail_handle_mouse_down(state, &layout, event)) {
                        return;
                    }
                } else if (panel->list_detail_mode == FX_LIST_DETAIL_METER) {
                    if (panel->list_open_slot_index >= 0 && panel->list_open_slot_index < panel->chain_count) {
                        FxTypeId type_id = panel->chain[panel->list_open_slot_index].type_id;
                        if (type_id == 102u) {
                            SDL_Rect toggle_ms;
                            SDL_Rect toggle_lr;
                            effects_panel_meter_detail_compute_toggle_rects(&layout.detail_rect, &toggle_ms, &toggle_lr);
                            if (SDL_PointInRect(&pt, &toggle_ms)) {
                                panel->meter_scope_mode = FX_METER_SCOPE_MID_SIDE;
                                return;
                            }
                            if (SDL_PointInRect(&pt, &toggle_lr)) {
                                panel->meter_scope_mode = FX_METER_SCOPE_LEFT_RIGHT;
                                return;
                            }
                        }
                    }
                }
            }

            if (!overlay_open) {
                int column_hit = hit_column_index(&layout, panel, &pt);
                if (column_hit >= 0) {
                    panel->selected_slot_index = column_hit;
                }
            }

            // Scrollbar drag start (per slot)
            if (panel->view_mode == FX_PANEL_VIEW_LIST) {
                int open_index = panel->list_open_slot_index;
                if (open_index >= 0 && open_index < panel->chain_count) {
                    EffectsSlotLayout detail_layout;
                    if (compute_detail_slot_layout(state, &layout, open_index, &detail_layout)) {
                        if (panel->slot_runtime[open_index].scroll_max > 0.5f &&
                            SDL_PointInRect(&pt, &detail_layout.scrollbar_track)) {
                            panel->slot_runtime[open_index].dragging = true;
                            panel->param_scroll_drag_slot = open_index;
                            panel->slot_runtime[open_index].drag_start_y = pt.y;
                            panel->slot_runtime[open_index].drag_start_val = panel->slot_runtime[open_index].scroll;
                            return;
                        }
                    }
                }
            } else {
                for (int i = 0; i < layout.column_count && i < panel->chain_count; ++i) {
                    if (panel->slot_runtime[i].scroll_max > 0.5f && SDL_PointInRect(&pt, &layout.slots[i].scrollbar_track)) {
                        panel->slot_runtime[i].dragging = true;
                        panel->param_scroll_drag_slot = i;
                        panel->slot_runtime[i].drag_start_y = pt.y;
                        panel->slot_runtime[i].drag_start_val = panel->slot_runtime[i].scroll;
                        return;
                    }
                }
            }

            // Begin track title rename on double-click (track targets only).
            if (panel_targets_track(panel) && SDL_PointInRect(&pt, &layout.target_label_rect)) {
                Uint32 now = SDL_GetTicks();
                bool is_double = event->button.clicks >= 2;
                if (!is_double && panel->title_last_click_ticks != 0 && now - panel->title_last_click_ticks <= 500) {
                    is_double = true;
                }
                ((EffectsPanelState*)panel)->title_debug_last_click = true;
                ((EffectsPanelState*)panel)->title_last_click_ticks = now;
                if (is_double) {
                    int track_idx = panel->target_track_index >= 0 ? panel->target_track_index : state->selected_track_index;
                    if (track_idx >= 0) {
                        track_name_editor_stop(state, true);
                        track_name_editor_start(state, track_idx);
                        TrackNameEditor* editor = &state->track_name_editor;
                        if (editor->editing) {
                            float scale = FX_PANEL_TITLE_SCALE;
                            int prefix_w = ui_measure_text_width("Track FX: ", scale);
                            int rel = event->button.x - (layout.target_label_rect.x + 6 + prefix_w);
                            if (rel < 0) rel = 0;
                            int len = (int)strlen(editor->buffer);
                            int cursor = len; // default to end
                            char temp[ENGINE_CLIP_NAME_MAX];
                            for (int i = 0; i <= len; ++i) {
                                snprintf(temp, sizeof(temp), "%.*s", i, editor->buffer);
                                int w = ui_measure_text_width(temp, scale);
                                if (w >= rel) {
                                    cursor = i;
                                    break;
                                }
                            }
                            if (cursor > len) cursor = len;
                            editor->cursor = cursor;
                        }
                    }
                }
                return;
            }

            bool toggle_hit = SDL_PointInRect(&pt, &layout.view_toggle_rect);
            if (toggle_hit) {
                panel->view_mode = panel->view_mode == FX_PANEL_VIEW_STACK ? FX_PANEL_VIEW_LIST : FX_PANEL_VIEW_STACK;
                if (overlay_open) {
                    close_overlay(panel);
                }
                return;
            }

            bool button_hit = SDL_PointInRect(&pt, &layout.dropdown_button_rect);
            if (button_hit) {
                if (overlay_open) {
                    close_overlay(panel);
                } else {
                    if (!panel->initialized && state->engine) {
                        effects_panel_refresh_catalog(state);
                    }
                    if (panel->category_count > 0) {
                        panel->overlay_layer = FX_PANEL_OVERLAY_CATEGORIES;
                        panel->hovered_category_index = -1;
                        panel->hovered_effect_index = -1;
                        panel->active_category_index = -1;
                        panel->overlay_scroll_index = 0;
                    }
                }
                break;
            }

            if (overlay_open) {
                bool handled = false;
                if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS &&
                    SDL_PointInRect(&pt, &layout.overlay_back_rect) &&
                    layout.overlay_back_rect.w > 0) {
                    panel->overlay_layer = FX_PANEL_OVERLAY_CATEGORIES;
                    panel->hovered_category_index = -1;
                    panel->hovered_effect_index = -1;
                    panel->active_category_index = -1;
                    panel->overlay_scroll_index = 0;
                    handled = true;
                } else if (SDL_PointInRect(&pt, &layout.overlay_rect)) {
                    for (int i = 0; i < layout.overlay_item_count; ++i) {
                        if (SDL_PointInRect(&pt, &layout.overlay_item_rects[i])) {
                            if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
                                int cat_index = layout.overlay_item_order[i];
                                if (cat_index >= 0 && cat_index < panel->category_count) {
                                    const FxCategoryUIInfo* cat = &panel->categories[cat_index];
                                    panel->active_category_index = cat_index;
                                    panel->hovered_category_index = cat_index;
                                    panel->overlay_scroll_index = 0;
                                    if (cat->type_count > 0) {
                                        panel->overlay_layer = FX_PANEL_OVERLAY_EFFECTS;
                                        panel->hovered_effect_index = -1;
                                    } else {
                                        panel->overlay_layer = FX_PANEL_OVERLAY_CATEGORIES;
                                    }
                                }
                            } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
                                int type_index = layout.overlay_item_order[i];
                                if (type_index >= 0 && type_index < panel->type_count && state->engine) {
                                    FxTypeId type = panel->types[type_index].type_id;
                                    FxInstId id = 0;
                                    if (panel_targets_track(panel)) {
                                        id = engine_fx_track_add(state->engine, panel->target_track_index, type);
                                    } else {
                                        id = engine_fx_master_add(state->engine, type);
                                    }
                                    if (id != 0) {
                                        effects_panel_sync_from_engine(state);
                                        int new_index = find_slot_index_by_id(panel, id);
                                        if (new_index >= 0) {
                                            UndoCommand cmd = {0};
                                            cmd.type = UNDO_CMD_FX_EDIT;
                                            cmd.data.fx_edit.kind = UNDO_FX_EDIT_ADD;
                                            cmd.data.fx_edit.target = panel_targets_track(panel) ? UNDO_FX_TARGET_TRACK : UNDO_FX_TARGET_MASTER;
                                            cmd.data.fx_edit.track_index = panel_targets_track(panel) ? panel->target_track_index : -1;
                                            cmd.data.fx_edit.id = id;
                                            cmd.data.fx_edit.before_index = -1;
                                            cmd.data.fx_edit.after_index = new_index;
                                            fx_instance_from_slot(&panel->chain[new_index], &cmd.data.fx_edit.after_state);
                                            undo_manager_push(&state->undo, &cmd);
                                        }
                                        panel->highlighted_slot_index = panel->chain_count > 0 ? panel->chain_count - 1 : -1;
                                    }
                                    close_overlay(panel);
                                }
                            }
                            handled = true;
                            break;
                        }
                    }
                    if (!handled && layout.overlay_has_scrollbar &&
                        SDL_PointInRect(&pt, &layout.overlay_scrollbar_track)) {
                        handled = true;
                    }
                }
                if (!handled) {
                    close_overlay(panel);
                }
                break;
            }

            // Remove buttons (only when overlay is closed)
            if (panel->view_mode == FX_PANEL_VIEW_LIST) {
                int open_index = panel->list_open_slot_index;
                if (open_index >= 0 && open_index < panel->chain_count) {
                    EffectsSlotLayout detail_layout;
                    if (compute_detail_slot_layout(state, &layout, open_index, &detail_layout)) {
                        if (SDL_PointInRect(&pt, &detail_layout.toggle_rect)) {
                            toggle_slot_enabled(state, panel, open_index);
                            return;
                        }
                        if (SDL_PointInRect(&pt, &detail_layout.remove_rect)) {
                            if (state->engine) {
                                FxInstId id = panel->chain[open_index].id;
                                UndoCommand cmd = {0};
                                cmd.type = UNDO_CMD_FX_EDIT;
                                cmd.data.fx_edit.kind = UNDO_FX_EDIT_REMOVE;
                                cmd.data.fx_edit.target = panel_targets_track(panel) ? UNDO_FX_TARGET_TRACK : UNDO_FX_TARGET_MASTER;
                                cmd.data.fx_edit.track_index = panel_targets_track(panel) ? panel->target_track_index : -1;
                                cmd.data.fx_edit.id = id;
                                cmd.data.fx_edit.before_index = open_index;
                                fx_instance_from_slot(&panel->chain[open_index], &cmd.data.fx_edit.before_state);
                                bool removed = false;
                                if (panel_targets_track(panel)) {
                                    removed = engine_fx_track_remove(state->engine, panel->target_track_index, id);
                                } else {
                                    removed = engine_fx_master_remove(state->engine, id);
                                }
                                if (id != 0 && removed) {
                                    effects_panel_sync_from_engine(state);
                                    undo_manager_push(&state->undo, &cmd);
                                }
                            }
                            panel->highlighted_slot_index = -1;
                            panel->selected_slot_index = -1;
                            return;
                        }
                    }
                }
            } else {
                for (int i = 0; i < layout.column_count && i < panel->chain_count; ++i) {
                    if (SDL_PointInRect(&pt, &layout.slots[i].toggle_rect)) {
                        toggle_slot_enabled(state, panel, i);
                        return;
                    }
                    if (SDL_PointInRect(&pt, &layout.slots[i].remove_rect)) {
                        if (state->engine) {
                            FxInstId id = panel->chain[i].id;
                            UndoCommand cmd = {0};
                            cmd.type = UNDO_CMD_FX_EDIT;
                            cmd.data.fx_edit.kind = UNDO_FX_EDIT_REMOVE;
                            cmd.data.fx_edit.target = panel_targets_track(panel) ? UNDO_FX_TARGET_TRACK : UNDO_FX_TARGET_MASTER;
                            cmd.data.fx_edit.track_index = panel_targets_track(panel) ? panel->target_track_index : -1;
                            cmd.data.fx_edit.id = id;
                            cmd.data.fx_edit.before_index = i;
                            fx_instance_from_slot(&panel->chain[i], &cmd.data.fx_edit.before_state);
                            bool removed = false;
                            if (panel_targets_track(panel)) {
                                removed = engine_fx_track_remove(state->engine, panel->target_track_index, id);
                            } else {
                                removed = engine_fx_master_remove(state->engine, id);
                            }
                            if (id != 0 && removed) {
                                effects_panel_sync_from_engine(state);
                                undo_manager_push(&state->undo, &cmd);
                            }
                        }
                        panel->highlighted_slot_index = -1;
                        panel->selected_slot_index = -1;
                        return;
                    }
                }
            }

            // Slider hit test
            if (panel->view_mode == FX_PANEL_VIEW_LIST) {
                int open_index = panel->list_open_slot_index;
                if (open_index >= 0 && open_index < panel->chain_count) {
                    EffectsSlotLayout detail_layout;
                    if (compute_detail_slot_layout(state, &layout, open_index, &detail_layout)) {
                        FxSlotUIState* slot = &panel->chain[open_index];
                        for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
                            SDL_Rect mode_rect = detail_layout.mode_rects[p];
                            if (mode_rect.w > 0 && mode_rect.h > 0 && SDL_PointInRect(&pt, &mode_rect)) {
                                toggle_param_mode(state, open_index, (int)p);
                                return;
                            }
                            if (SDL_PointInRect(&pt, &detail_layout.slider_rects[p])) {
                                panel->dragging_slider = true;
                                panel->active_slot_index = open_index;
                                panel->active_param_index = (int)p;
                                begin_fx_param_drag(state, open_index, (int)p);
                                EffectsPanelLayout temp_layout;
                                SDL_zero(temp_layout);
                                temp_layout.slots[open_index] = detail_layout;
                                float value = slider_value_from_mouse(state, &temp_layout, open_index, (int)p, event->button.x);
                                apply_slider_value(state, open_index, (int)p, value);
                                return;
                            }
                        }
                    }
                }
            } else {
                for (int i = 0; i < layout.column_count && i < panel->chain_count; ++i) {
                    FxSlotUIState* slot = &panel->chain[i];
                    for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
                        SDL_Rect mode_rect = layout.slots[i].mode_rects[p];
                        if (mode_rect.w > 0 && mode_rect.h > 0 && SDL_PointInRect(&pt, &mode_rect)) {
                            toggle_param_mode(state, i, (int)p);
                            return;
                        }
                        if (SDL_PointInRect(&pt, &layout.slots[i].slider_rects[p])) {
                            panel->dragging_slider = true;
                            panel->active_slot_index = i;
                            panel->active_param_index = (int)p;
                            begin_fx_param_drag(state, i, (int)p);
                            float value = slider_value_from_mouse(state, &layout, i, (int)p, event->button.x);
                            apply_slider_value(state, i, (int)p, value);
                            return;
                        }
                    }
                }
            }
            break;
        }
        case SDL_MOUSEBUTTONUP: {
            if (event->button.button != SDL_BUTTON_LEFT) {
                break;
            }
            if (panel->dragging_slider) {
                panel->dragging_slider = false;
                panel->active_slot_index = -1;
                panel->active_param_index = -1;
                if (state->undo.active_drag_valid) {
                    UndoCommand* cmd = &state->undo.active_drag;
                    if (cmd->type == UNDO_CMD_FX_EDIT && cmd->data.fx_edit.kind == UNDO_FX_EDIT_PARAM) {
                        int slot_index = find_slot_index_by_id(panel, cmd->data.fx_edit.id);
                        if (slot_index >= 0) {
                            fx_instance_from_slot(&panel->chain[slot_index], &cmd->data.fx_edit.after_state);
                            if (cmd->data.fx_edit.before_state.param_count != cmd->data.fx_edit.after_state.param_count ||
                                memcmp(&cmd->data.fx_edit.before_state, &cmd->data.fx_edit.after_state,
                                       sizeof(SessionFxInstance)) != 0) {
                                undo_manager_commit_drag(&state->undo, cmd);
                            } else {
                                undo_manager_cancel_drag(&state->undo);
                            }
                        } else {
                            undo_manager_cancel_drag(&state->undo);
                        }
                    } else {
                        undo_manager_cancel_drag(&state->undo);
                    }
                }
                if (state->engine) {
                    effects_panel_sync_from_engine(state);
                }
            }
            if (effects_panel_track_snapshot_handle_mouse_up(state, event)) {
                break;
            }
            if (panel->view_mode == FX_PANEL_VIEW_LIST && panel->list_detail_mode == FX_LIST_DETAIL_EQ) {
                if (effects_panel_eq_detail_handle_mouse_up(state, event)) {
                    break;
                }
            }
            if (panel->param_scroll_drag_slot >= 0 &&
                panel->param_scroll_drag_slot < FX_MASTER_MAX) {
                int slot = panel->param_scroll_drag_slot;
                panel->slot_runtime[slot].dragging = false;
            }
            panel->param_scroll_drag_slot = -1;
            break;
        }
        case SDL_MOUSEMOTION: {
            SDL_Point pt = {event->motion.x, event->motion.y};
            int drag_slot = panel->param_scroll_drag_slot;
            if (drag_slot >= 0 &&
                drag_slot < panel->chain_count &&
                panel->slot_runtime[drag_slot].dragging &&
                panel->slot_runtime[drag_slot].scroll_max > 0.5f) {
                int dy = event->motion.y - panel->slot_runtime[drag_slot].drag_start_y;
                int track_h = 0;
                if (panel->view_mode == FX_PANEL_VIEW_LIST) {
                    EffectsSlotLayout detail_layout;
                    if (compute_detail_slot_layout(state, &layout, drag_slot, &detail_layout)) {
                        track_h = detail_layout.scrollbar_track.h - detail_layout.scrollbar_thumb.h;
                    }
                } else {
                    track_h = layout.slots[drag_slot].scrollbar_track.h - layout.slots[drag_slot].scrollbar_thumb.h;
                }
                if (track_h < 1) track_h = 1;
                float delta = ((float)dy / (float)track_h) * panel->slot_runtime[drag_slot].scroll_max;
                panel->slot_runtime[drag_slot].scroll = panel->slot_runtime[drag_slot].drag_start_val + delta;
                if (panel->slot_runtime[drag_slot].scroll < 0.0f) panel->slot_runtime[drag_slot].scroll = 0.0f;
                if (panel->slot_runtime[drag_slot].scroll > panel->slot_runtime[drag_slot].scroll_max) panel->slot_runtime[drag_slot].scroll = panel->slot_runtime[drag_slot].scroll_max;
                effects_panel_compute_layout(state, &layout);
            }
            if (overlay_open && layout.overlay_visible) {
                if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
                    panel->hovered_category_index = -1;
                    for (int i = 0; i < layout.overlay_item_count; ++i) {
                        if (SDL_PointInRect(&pt, &layout.overlay_item_rects[i])) {
                            int cat_index = layout.overlay_item_order[i];
                            if (cat_index >= 0 && cat_index < panel->category_count) {
                                panel->hovered_category_index = cat_index;
                            }
                            break;
                        }
                    }
                } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
                    panel->hovered_effect_index = -1;
                    for (int i = 0; i < layout.overlay_item_count; ++i) {
                        if (SDL_PointInRect(&pt, &layout.overlay_item_rects[i])) {
                            int type_index = layout.overlay_item_order[i];
                            if (type_index >= 0 && type_index < panel->type_count) {
                                panel->hovered_effect_index = type_index;
                            }
                            break;
                        }
                    }
                }
                break;
            }

            if (panel->view_mode == FX_PANEL_VIEW_LIST) {
                if (effects_panel_track_snapshot_handle_mouse_motion(state, &layout, event)) {
                    break;
                }
                if (panel->list_detail_mode == FX_LIST_DETAIL_EQ) {
                    if (effects_panel_eq_detail_handle_mouse_motion(state, &layout, event)) {
                        break;
                    }
                }
            }
            if (panel->dragging_slider) {
                int slot_index = panel->active_slot_index;
                int param_index = panel->active_param_index;
                if (panel->view_mode == FX_PANEL_VIEW_LIST) {
                    EffectsSlotLayout detail_layout;
                    if (compute_detail_slot_layout(state, &layout, slot_index, &detail_layout)) {
                        EffectsPanelLayout temp_layout;
                        SDL_zero(temp_layout);
                        temp_layout.slots[slot_index] = detail_layout;
                        float value = slider_value_from_mouse(state, &temp_layout, slot_index, param_index, event->motion.x);
                        apply_slider_value(state, slot_index, param_index, value);
                    }
                } else {
                    float value = slider_value_from_mouse(state, &layout, slot_index, param_index, event->motion.x);
                    apply_slider_value(state, slot_index, param_index, value);
                }
            } else {
                panel->highlighted_slot_index = -1;
                panel->hovered_toggle_slot_index = -1;
                if (panel->view_mode == FX_PANEL_VIEW_LIST) {
                    int open_index = panel->list_open_slot_index;
                    if (open_index >= 0 && open_index < panel->chain_count) {
                        EffectsSlotLayout detail_layout;
                        if (compute_detail_slot_layout(state, &layout, open_index, &detail_layout)) {
                            if (SDL_PointInRect(&pt, &detail_layout.toggle_rect)) {
                                panel->hovered_toggle_slot_index = open_index;
                            }
                            if (SDL_PointInRect(&pt, &detail_layout.remove_rect)) {
                                panel->highlighted_slot_index = open_index;
                            }
                        }
                    }
                } else {
                    for (int i = 0; i < layout.column_count && i < panel->chain_count; ++i) {
                        if (SDL_PointInRect(&pt, &layout.slots[i].toggle_rect)) {
                            panel->hovered_toggle_slot_index = i;
                        }
                        if (SDL_PointInRect(&pt, &layout.slots[i].remove_rect)) {
                            panel->highlighted_slot_index = i;
                            break;
                        }
                    }
                }
            }
            break;
        }
        case SDL_MOUSEWHEEL: {
            if (overlay_open && layout.overlay_visible) {
                SDL_Point pt = {state->mouse_x, state->mouse_y};
                if (SDL_PointInRect(&pt, &layout.overlay_rect)) {
                    int max_scroll = layout.overlay_total_items - layout.overlay_visible_count;
                    if (max_scroll < 0) max_scroll = 0;
                    if (max_scroll > 0) {
                        int delta = 0;
                        if (event->wheel.y > 0) delta = -1;
                        else if (event->wheel.y < 0) delta = 1;
                        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                            delta = -delta;
                        }
                        if (delta != 0) {
                            int new_scroll = panel->overlay_scroll_index + delta;
                            if (new_scroll < 0) new_scroll = 0;
                            if (new_scroll > max_scroll) new_scroll = max_scroll;
                            if (new_scroll != panel->overlay_scroll_index) {
                                panel->overlay_scroll_index = new_scroll;
                                panel->hovered_category_index = -1;
                                panel->hovered_effect_index = -1;
                            }
                        }
                    }
                }
            } else {
                SDL_Point pt = {state->mouse_x, state->mouse_y};
                if (panel->view_mode == FX_PANEL_VIEW_LIST) {
                    if (panel->track_snapshot.list_scroll_max > 0.0f &&
                        SDL_PointInRect(&pt, &layout.track_snapshot.list_clip_rect)) {
                        int dy = event->wheel.y;
                        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                            dy = -dy;
                        }
                        if (dy != 0) {
                            float step = (float)(FX_PANEL_LIST_ROW_HEIGHT + FX_PANEL_LIST_ROW_GAP);
                            panel->track_snapshot.list_scroll -= (float)dy * step;
                            if (panel->track_snapshot.list_scroll < 0.0f) {
                                panel->track_snapshot.list_scroll = 0.0f;
                            }
                            if (panel->track_snapshot.list_scroll > panel->track_snapshot.list_scroll_max) {
                                panel->track_snapshot.list_scroll = panel->track_snapshot.list_scroll_max;
                            }
                            effects_panel_compute_layout(state, &layout);
                        }
                        break;
                    }
                    int open_index = panel->list_open_slot_index;
                    if (open_index >= 0 && open_index < panel->chain_count &&
                        SDL_PointInRect(&pt, &layout.detail_rect) &&
                        panel->slot_runtime[open_index].scroll_max > 0.0f) {
                        int dy = event->wheel.y;
                        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                            dy = -dy;
                        }
                        if (dy != 0) {
                            panel->slot_runtime[open_index].scroll -= (float)dy * 30.0f;
                            if (panel->slot_runtime[open_index].scroll < 0.0f) panel->slot_runtime[open_index].scroll = 0.0f;
                            if (panel->slot_runtime[open_index].scroll > panel->slot_runtime[open_index].scroll_max) panel->slot_runtime[open_index].scroll = panel->slot_runtime[open_index].scroll_max;
                            effects_panel_compute_layout(state, &layout);
                        }
                    }
                } else {
                    int slot = hit_column_index(&layout, panel, &pt);
                    if (slot >= 0 && slot < panel->chain_count && panel->slot_runtime[slot].scroll_max > 0.0f) {
                        int dy = event->wheel.y;
                        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                            dy = -dy;
                        }
                        if (dy != 0) {
                            panel->slot_runtime[slot].scroll -= (float)dy * 30.0f;
                            if (panel->slot_runtime[slot].scroll < 0.0f) panel->slot_runtime[slot].scroll = 0.0f;
                            if (panel->slot_runtime[slot].scroll > panel->slot_runtime[slot].scroll_max) panel->slot_runtime[slot].scroll = panel->slot_runtime[slot].scroll_max;
                            effects_panel_compute_layout(state, &layout);
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

void effects_panel_input_update(InputManager* manager, AppState* state, bool left_was_down, bool left_is_down) {
    (void)manager;
    (void)left_was_down;
    (void)left_is_down;
    if (!state) {
        return;
    }
    if (state->inspector.visible) {
        close_overlay(&state->effects_panel);
        state->effects_panel.focused = false;
        state->effects_panel.selected_slot_index = -1;
        return;
    }
    if (state->engine) {
        EffectsPanelState* panel = &state->effects_panel;
        if (!panel->initialized || panel->type_count == 0) {
            effects_panel_refresh_catalog(state);
        }
        effects_panel_sync_from_engine(state);
    }
}
