#include "input/effects_panel_input_helpers.h"

#include "engine/engine.h"
#include "effects/param_utils.h"
#include "ui/effects_panel.h"
#include "ui/effects_panel_slot_layout.h"
#include "ui/effects_panel_spec.h"
#include "undo/undo_manager.h"

#include <math.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool panel_targets_track(const EffectsPanelState* panel) {
    return panel && panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0;
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

void fx_instance_from_slot(const FxSlotUIState* slot, SessionFxInstance* out_instance) {
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

void sync_meter_modes_from_slot_params(EffectsPanelState* panel, const FxSlotUIState* slot) {
    if (!panel || !slot) {
        return;
    }
    if (slot->type_id == 102u && slot->param_count > 0) {
        int mode = (int)lroundf(slot->param_values[0]);
        panel->meter_scope_mode = mode == 0 ? FX_METER_SCOPE_MID_SIDE : FX_METER_SCOPE_LEFT_RIGHT;
        return;
    }
    if (slot->type_id == 104u && slot->param_count > 0) {
        int mode = (int)lroundf(slot->param_values[0]);
        if (mode <= 0) {
            panel->meter_lufs_mode = FX_METER_LUFS_INTEGRATED;
        } else if (mode == 1) {
            panel->meter_lufs_mode = FX_METER_LUFS_SHORT_TERM;
        } else {
            panel->meter_lufs_mode = FX_METER_LUFS_MOMENTARY;
        }
        return;
    }
    if (slot->type_id == 105u && slot->param_count > 2) {
        int mode = (int)lroundf(slot->param_values[2]);
        if (mode <= 0) {
            panel->meter_spectrogram_mode = FX_METER_SPECTROGRAM_WHITE_BLACK;
        } else if (mode == 1) {
            panel->meter_spectrogram_mode = FX_METER_SPECTROGRAM_BLACK_WHITE;
        } else {
            panel->meter_spectrogram_mode = FX_METER_SPECTROGRAM_HEAT;
        }
    }
}

int find_slot_index_by_id(const EffectsPanelState* panel, FxInstId id) {
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

void begin_fx_param_drag(AppState* state, int slot_index, int param_index) {
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

static bool effects_panel_slot_supports_preview(FxTypeId type_id) {
    switch (type_id) {
        case 7u:
        case 20u:
        case 21u:
        case 22u:
        case 23u:
        case 60u:
        case 61u:
        case 62u:
        case 63u:
        case 64u:
        case 65u:
            return true;
        default:
            return false;
    }
}

static void effects_panel_refresh_preview_state(EffectsPanelState* panel) {
    if (!panel) {
        return;
    }
    bool previews_open = true;
    bool preview_found = false;
    for (int i = 0; i < panel->chain_count; ++i) {
        if (!effects_panel_slot_supports_preview(panel->chain[i].type_id)) {
            continue;
        }
        preview_found = true;
        if (!panel->preview_slots[i].open) {
            previews_open = false;
            break;
        }
    }
    panel->preview_all_open = preview_found && previews_open;
}

void effects_panel_toggle_preview(EffectsPanelState* panel, int slot_index) {
    if (!panel || slot_index < 0 || slot_index >= panel->chain_count) {
        return;
    }
    EffectsPanelPreviewSlotState* preview = &panel->preview_slots[slot_index];
    preview->open = !preview->open;
    effects_panel_refresh_preview_state(panel);
}

void effects_panel_set_all_previews(EffectsPanelState* panel, bool open) {
    if (!panel) {
        return;
    }
    for (int i = 0; i < panel->chain_count; ++i) {
        if (!effects_panel_slot_supports_preview(panel->chain[i].type_id)) {
            continue;
        }
        panel->preview_slots[i].open = open;
    }
    panel->preview_all_open = open;
}

void effects_panel_flip_all_previews(EffectsPanelState* panel) {
    if (!panel) {
        return;
    }
    for (int i = 0; i < panel->chain_count; ++i) {
        if (!effects_panel_slot_supports_preview(panel->chain[i].type_id)) {
            continue;
        }
        panel->preview_slots[i].open = !panel->preview_slots[i].open;
    }
    effects_panel_refresh_preview_state(panel);
}

bool panel_uses_spec_ui(const EffectsPanelState* panel, const FxSlotUIState* slot) {
    if (!panel || !slot) {
        return false;
    }
    return effects_panel_spec_enabled(panel, slot->type_id);
}

int spec_layout_find_widget(const EffectsSpecPanelLayout* layout, uint32_t param_index) {
    if (!layout) {
        return -1;
    }
    for (int i = 0; i < layout->widget_count; ++i) {
        if (layout->widgets[i].param_index == param_index) {
            return i;
        }
    }
    return -1;
}

bool compute_detail_slot_layout(const AppState* state,
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

bool toggle_slot_enabled(AppState* state, EffectsPanelState* panel, int slot_index) {
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

int hit_column_index(const EffectsPanelLayout* layout, const EffectsPanelState* panel, const SDL_Point* pt) {
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

float slider_value_from_mouse(const AppState* state,
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
    const EffectParamSpec* spec = info ? &info->param_specs[param_index] : NULL;
    FxParamMode mode = slot->param_mode[param_index];
    float t = (float)(mouse_x - slider_rect->x) / (float)slider_rect->w;
    t = clampf(t, 0.0f, 1.0f);
    float value = 0.0f;
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_spec_is_syncable(spec)) {
        float beat_min = 0.0f;
        float beat_max = 0.0f;
        if (!fx_param_spec_get_beat_bounds(spec, &state->tempo, &beat_min, &beat_max)) {
            return fx_param_map_ui_to_value(spec, t);
        }
        value = beat_min + t * (beat_max - beat_min);
        value = fx_param_quantize_beats(value);
        if (value < beat_min) value = beat_min;
        if (value > beat_max) value = beat_max;
    } else {
        value = fx_param_map_ui_to_value(spec, t);
    }
    return value;
}

void apply_slider_value(AppState* state, int slot_index, int param_index, float value) {
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
    const EffectParamSpec* spec = info ? &info->param_specs[param_index] : NULL;
    FxParamMode mode = slot->param_mode[param_index];
    float beat_value = slot->param_beats[param_index];
    float native_value = value;
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_spec_is_syncable(spec)) {
        beat_value = value;
        native_value = fx_param_spec_beats_to_native(spec, beat_value, &state->tempo);
    } else if (fx_param_spec_is_syncable(spec)) {
        beat_value = fx_param_spec_native_to_beats(spec, value, &state->tempo);
    }
    bool updated = false;
    bool use_sync = (mode != FX_PARAM_MODE_NATIVE) && fx_param_spec_is_syncable(spec);
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

bool apply_spec_widget_action(AppState* state,
                              int slot_index,
                              const EffectsSpecPanelLayout* layout,
                              int widget_index) {
    if (!state || !layout) {
        return false;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (slot_index < 0 || slot_index >= panel->chain_count) {
        return false;
    }
    FxSlotUIState* slot = &panel->chain[slot_index];
    if (widget_index < 0 || widget_index >= layout->widget_count) {
        return false;
    }
    const FxSpecWidget* widget = &layout->widgets[widget_index];
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    if (!info || widget->param_index >= slot->param_count) {
        return false;
    }
    const EffectParamSpec* spec = &info->param_specs[widget->param_index];
    float value = slot->param_values[widget->param_index];
    if (widget->type == FX_SPEC_WIDGET_TOGGLE) {
        value = value >= 0.5f ? 0.0f : 1.0f;
        apply_slider_value(state, slot_index, (int)widget->param_index, value);
        return true;
    }
    if (widget->type == FX_SPEC_WIDGET_DROPDOWN && spec->enum_count > 0) {
        int idx = (int)lroundf(value);
        idx = (idx + 1) % (int)spec->enum_count;
        value = (float)idx;
        apply_slider_value(state, slot_index, (int)widget->param_index, value);
        return true;
    }
    return false;
}

bool adjust_spec_param(AppState* state, int slot_index, int param_index, bool increase) {
    if (!state) {
        return false;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (slot_index < 0 || slot_index >= panel->chain_count) {
        return false;
    }
    FxSlotUIState* slot = &panel->chain[slot_index];
    if (param_index < 0 || param_index >= (int)slot->param_count) {
        return false;
    }
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);
    if (!info) {
        return false;
    }
    const EffectParamSpec* spec = &info->param_specs[param_index];
    FxParamMode mode = slot->param_mode[param_index];
    float value = slot->param_values[param_index];
    float step = spec->step;
    float min_value = spec->min_value;
    float max_value = spec->max_value;
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_spec_is_syncable(spec)) {
        float beat_min = 0.0f;
        float beat_max = 0.0f;
        if (!fx_param_spec_get_beat_bounds(spec, &state->tempo, &beat_min, &beat_max)) {
            return false;
        }
        value = slot->param_beats[param_index];
        if (fabsf(value) < 1e-6f) {
            value = fx_param_spec_native_to_beats(spec, slot->param_values[param_index], &state->tempo);
        }
        min_value = beat_min;
        max_value = beat_max;
        if (step <= 0.0f) {
            step = 1.0f / 16.0f;
        }
    } else if (spec->type == FX_PARAM_TYPE_BOOL || spec->type == FX_PARAM_TYPE_ENUM) {
        step = 1.0f;
    } else if (step <= 0.0f) {
        step = (max_value - min_value) * 0.01f;
    }

    if (!increase) {
        step = -step;
    }
    value += step;
    if (value < min_value) value = min_value;
    if (value > max_value) value = max_value;
    apply_slider_value(state, slot_index, param_index, value);
    return true;
}

void toggle_param_mode(AppState* state, int slot_index, int param_index) {
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
    const EffectParamSpec* spec = info ? &info->param_specs[param_index] : NULL;
    if (!fx_param_spec_is_syncable(spec)) {
        return;
    }
    FxParamMode current = slot->param_mode[param_index];
    FxParamMode next = FX_PARAM_MODE_NATIVE;
    if (current == FX_PARAM_MODE_NATIVE) {
        next = (spec && spec->unit == FX_PARAM_UNIT_HZ) ? FX_PARAM_MODE_BEAT_RATE : FX_PARAM_MODE_BEATS;
    } else {
        next = FX_PARAM_MODE_NATIVE;
    }
    float native_value = slot->param_values[param_index];
    float beat_value = slot->param_beats[param_index];
    float beat_min = 0.0f;
    float beat_max = 0.0f;
    if (!fx_param_spec_get_beat_bounds(spec, &state->tempo, &beat_min, &beat_max)) {
        return;
    }
    float t = 0.0f;
    if (current != FX_PARAM_MODE_NATIVE) {
        float current_beats = beat_value;
        if (fabsf(current_beats) < 1e-6f) {
            current_beats = fx_param_spec_native_to_beats(spec, native_value, &state->tempo);
        }
        if (current_beats < beat_min) current_beats = beat_min;
        if (current_beats > beat_max) current_beats = beat_max;
        t = (current_beats - beat_min) / (beat_max - beat_min);
    } else {
        t = fx_param_map_value_to_ui(spec, native_value);
    }
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (next != FX_PARAM_MODE_NATIVE) {
        beat_value = beat_min + t * (beat_max - beat_min);
        beat_value = fx_param_quantize_beats(beat_value);
        if (beat_value < beat_min) beat_value = beat_min;
        if (beat_value > beat_max) beat_value = beat_max;
        native_value = fx_param_spec_beats_to_native(spec, beat_value, &state->tempo);
    } else {
        native_value = fx_param_map_ui_to_value(spec, t);
        beat_value = fx_param_spec_native_to_beats(spec, native_value, &state->tempo);
    }
    slot->param_mode[param_index] = next;
    slot->param_beats[param_index] = beat_value;
    apply_slider_value(state, slot_index, param_index, next == FX_PARAM_MODE_NATIVE ? native_value : beat_value);
    fx_instance_from_slot(slot, &cmd.data.fx_edit.after_state);
    undo_manager_push(&state->undo, &cmd);
}

void close_overlay(EffectsPanelState* panel) {
    panel->overlay_layer = FX_PANEL_OVERLAY_CLOSED;
    panel->hovered_category_index = -1;
    panel->hovered_effect_index = -1;
    panel->active_category_index = -1;
    panel->overlay_scroll_index = 0;
}
