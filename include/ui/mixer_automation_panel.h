#pragma once
#include <SDL2/SDL.h>
struct AppState;
// Shares bounded point controls and curve preview geometry between native input and headless
// verification.
typedef struct DawMixerAutomationLayout {
    SDL_Rect panel, buttons[14], frame, value, preview, status;
    bool editable;
} DawMixerAutomationLayout;
// Opens qualified Gain/Pan curves for the current child/group/return/master insert chain.
bool daw_effect_automation_open(struct AppState*);
// Selects the group's fader or first bound send within the guarded routing editor.
void daw_mixer_automation_open(struct AppState*);
// Computes fully visible automation controls in the current effects pane.
void daw_mixer_automation_layout(const struct AppState*, DawMixerAutomationLayout*);
// Edits physical project-time points through the same reserved routing history as MCP.
bool daw_mixer_automation_event(struct AppState*, const SDL_Event*);
// Draws persistent labels, selected points and a bounded original-level curve preview.
void daw_mixer_automation_render(SDL_Renderer*, const struct AppState*);
