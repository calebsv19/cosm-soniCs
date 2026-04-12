#pragma once

#include "app_state.h"
#include "ui/effects_panel.h"
#include "ui/effects_panel_spec.h"

#include <stdbool.h>

int find_slot_index_by_id(const EffectsPanelState* panel, FxInstId id);
bool panel_targets_track(const EffectsPanelState* panel);
void sync_meter_modes_from_slot_params(EffectsPanelState* panel, const FxSlotUIState* slot);
void fx_instance_from_slot(const FxSlotUIState* slot, SessionFxInstance* out_instance);
void begin_fx_param_drag(AppState* state, int slot_index, int param_index);

void effects_panel_toggle_preview(EffectsPanelState* panel, int slot_index);
void effects_panel_set_all_previews(EffectsPanelState* panel, bool open);
void effects_panel_flip_all_previews(EffectsPanelState* panel);

bool panel_uses_spec_ui(const EffectsPanelState* panel, const FxSlotUIState* slot);
int spec_layout_find_widget(const EffectsSpecPanelLayout* layout, uint32_t param_index);
bool compute_detail_slot_layout(const AppState* state,
                                const EffectsPanelLayout* layout,
                                int slot_index,
                                EffectsSlotLayout* out_layout);

bool toggle_slot_enabled(AppState* state, EffectsPanelState* panel, int slot_index);
int hit_column_index(const EffectsPanelLayout* layout, const EffectsPanelState* panel, const SDL_Point* pt);
float slider_value_from_mouse(const AppState* state,
                              const EffectsPanelLayout* layout,
                              int slot_index,
                              int param_index,
                              int mouse_x);
void apply_slider_value(AppState* state, int slot_index, int param_index, float value);
bool apply_spec_widget_action(AppState* state,
                              int slot_index,
                              const EffectsSpecPanelLayout* layout,
                              int widget_index);
bool adjust_spec_param(AppState* state, int slot_index, int param_index, bool increase);
void toggle_param_mode(AppState* state, int slot_index, int param_index);

void close_overlay(EffectsPanelState* panel);
