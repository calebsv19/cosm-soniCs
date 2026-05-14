#ifndef DAW_WORKSPACE_AUTHORING_OVERLAY_H
#define DAW_WORKSPACE_AUTHORING_OVERLAY_H

#include <SDL2/SDL.h>

#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void daw_workspace_authoring_overlay_render(SDL_Renderer *renderer, AppState *state);

#ifdef __cplusplus
}
#endif

#endif
