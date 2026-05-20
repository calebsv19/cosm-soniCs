#pragma once

#include <SDL2/SDL.h>

#include "kit_ui.h"
#include "ui/shared_theme_font_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef KitUiButtonState DawUiButtonState;
typedef KitUiButtonSpec DawUiButtonSpec;
typedef KitUiButtonStyle DawUiButtonStyle;
typedef KitUiButtonVariant DawUiButtonVariant;

void daw_ui_button_spec_init(DawUiButtonSpec* spec, const char* label);
void daw_ui_button_state_init(DawUiButtonState* state);
int daw_ui_button_style_resolve(const DawThemePalette* palette,
                                const DawUiButtonSpec* spec,
                                DawUiButtonStyle* out_style);
int daw_ui_button_draw_frame(SDL_Renderer* renderer,
                             const SDL_Rect* rect,
                             const DawUiButtonStyle* style);

#ifdef __cplusplus
}
#endif
