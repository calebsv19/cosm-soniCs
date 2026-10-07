#pragma once
#include "ui/editor_controls.h"
#include "ui/effects_panel.h"
// Registers the visible portion of a product-owned discrete control.
void daw_editor_control_add(KitUiSurface*, unsigned, uint64_t, SDL_Rect, SDL_Rect, bool);
// Collects track-header controls using the same geometry as the timeline painter.
void daw_editor_track_controls_collect(struct AppState*, KitUiSurface*);
// Collects specialized discrete controls without registering continuous gesture regions.
void daw_editor_effect_exceptions_collect(struct AppState*, KitUiSurface*, const EffectsPanelLayout*);
