#pragma once

#include "app_state.h"
#include "ui/effects_panel.h"

#include <stdbool.h>

void effects_panel_preview_reset(EffectsPanelPreviewSlotState* preview, FxInstId fx_id, bool open_default);
bool effects_panel_slot_supports_preview(FxTypeId type_id);
void effects_panel_sync_meter_modes_from_slot(EffectsPanelState* panel, const FxSlotUIState* slot);

void effects_panel_fill_fallback_param_spec(EffectParamSpec* spec, const char* name, float def_value);
void effects_panel_build_categories(EffectsPanelState* panel);

void effects_panel_eq_curve_set_defaults(EqCurveState* curve);
void effects_panel_eq_curve_copy_settings(EqCurveState* dst, const EqCurveState* src);
void effects_panel_eq_curve_clear_transient(EqCurveState* curve);
void effects_panel_eq_curve_store_for_view(EffectsPanelState* panel,
                                           EffectsPanelEqDetailView view_mode,
                                           EffectsPanelTarget target,
                                           int track_index);
void effects_panel_eq_curve_load_for_view(EffectsPanelState* panel,
                                          EffectsPanelEqDetailView view_mode,
                                          EffectsPanelTarget target,
                                          int track_index);

void effects_panel_ensure_last_open_tracks(EffectsPanelState* panel, int track_count);
void effects_panel_ensure_eq_curve_tracks(AppState* state, int track_count);
