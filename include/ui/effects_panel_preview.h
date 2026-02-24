#ifndef UI_EFFECTS_PANEL_PREVIEW_H
#define UI_EFFECTS_PANEL_PREVIEW_H

#include <SDL2/SDL.h>

#include "app_state.h"

// EffectsPreviewMode enumerates preview rendering styles per effect type.
typedef enum EffectsPreviewMode {
    FX_PREVIEW_NONE = 0,
    FX_PREVIEW_HISTORY_GR,
    FX_PREVIEW_HISTORY_TRIM,
    FX_PREVIEW_CURVE,
    FX_PREVIEW_EQ,
    FX_PREVIEW_LFO,
    FX_PREVIEW_REVERB,
    FX_PREVIEW_DELAY
} EffectsPreviewMode;

// effects_slot_preview_mode returns the preview rendering mode for an effect type.
EffectsPreviewMode effects_slot_preview_mode(FxTypeId type_id);

// effects_slot_preview_has_gr returns true when the effect uses gain reduction samples.
bool effects_slot_preview_has_gr(FxTypeId type_id);

// effects_slot_preview_render draws the preview panel and toggle for a slot.
void effects_slot_preview_render(SDL_Renderer* renderer,
                                 const FxSlotUIState* slot,
                                 EffectsPanelPreviewSlotState* preview,
                                 const SDL_Rect* preview_rect,
                                 const SDL_Rect* toggle_rect,
                                 SDL_Color label_color,
                                 SDL_Color text_dim,
                                 float sample_rate,
                                 bool have_gr,
                                 float gr_db);

#endif
