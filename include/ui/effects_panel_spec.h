#pragma once

#include <SDL2/SDL.h>

#include "app_state.h"
#include "effects/effects_manager.h"
#include "effects/param_spec.h"

// Identifies the spec-driven widget type used for a parameter.
typedef enum {
    FX_SPEC_WIDGET_SLIDER = 0,
    FX_SPEC_WIDGET_KNOB,
    FX_SPEC_WIDGET_TOGGLE,
    FX_SPEC_WIDGET_DROPDOWN
} FxSpecWidgetType;

// Describes a single spec-driven widget and its layout rectangles.
typedef struct {
    FxSpecWidgetType type;
    int slot_index;
    uint32_t param_index;
    SDL_Rect rect;
    SDL_Rect label_rect;
    SDL_Rect control_rect;
    SDL_Rect value_rect;
    SDL_Rect mode_rect;
} FxSpecWidget;

// Captures layout data for a spec-driven effects panel section with widget rectangles and group labels.
typedef struct {
    SDL_Rect body_rect;
    FxSpecWidget widgets[FX_MAX_PARAMS];
    int widget_count;
    const char* group_labels[FX_MAX_PARAMS];
    SDL_Rect group_label_rects[FX_MAX_PARAMS];
    int group_count;
} EffectsSpecPanelLayout;

// Returns true if the spec-driven UI should be used for the given effect type.
bool effects_panel_spec_enabled(const struct EffectsPanelState* panel, FxTypeId type_id);

// Builds widget layout for a spec-driven panel section using the current scroll offset.
void effects_panel_spec_compute_layout(const struct AppState* state,
                                       const struct EffectsPanelState* panel,
                                       const FxSlotUIState* slot,
                                       const SDL_Rect* body_rect,
                                       float scroll_offset,
                                       EffectsSpecPanelLayout* out_layout);

// Measures the vertical content height for a spec panel layout so scroll bounds can be computed.
int effects_panel_spec_measure_height(const struct EffectsPanelState* panel,
                                      const FxSlotUIState* slot,
                                      const SDL_Rect* body_rect,
                                      int* out_widget_height);

// Renders a spec-driven panel section using the given layout.
void effects_panel_spec_render(SDL_Renderer* renderer,
                               const struct AppState* state,
                               const FxSlotUIState* slot,
                               const EffectsSpecPanelLayout* layout,
                               SDL_Color label_color,
                               SDL_Color value_color);

// Finds the widget index and hit region for a point within a spec panel.
bool effects_panel_spec_hit_test(const EffectsSpecPanelLayout* layout,
                                 const SDL_Point* point,
                                 int* out_index,
                                 bool* out_mode_toggle);

// Maps a mouse position to a param value for slider/knob widgets.
float effects_panel_spec_value_from_point(const struct AppState* state,
                                          const FxSlotUIState* slot,
                                          const EffectsSpecPanelLayout* layout,
                                          int widget_index,
                                          int mouse_x,
                                          int mouse_y);
