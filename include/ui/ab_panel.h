#pragma once
#include <SDL2/SDL.h>
#include "kit_ui_text_edit.h"
struct AppState;
// Owns native comparison fields, matching-release gestures and persistent queued text storage.
typedef struct DawAbUI {
    bool open, dry, track_scope, matched;
    int pressed, focused, cursor, side;
    char fields[3][32], labels[15][160], message[160], summary[512], meters[2][256];
    KitUiTextEdit text;
} DawAbUI;
// Shares fully visible native control geometry with headless input verification.
typedef struct DawAbLayout {
    SDL_Rect panel, buttons[12], fields[3];
    bool editable;
} DawAbLayout;
// Opens ephemeral A/B controls in the existing effects pane without capturing or playing
// implicitly.
bool daw_ab_panel_open(struct AppState*);
// Computes bounded controls within the effects pane content clip.
void daw_ab_panel_layout(const struct AppState*, DawAbLayout*);
// Routes point-safe text and matching-release comparison actions before hidden effects controls.
bool daw_ab_panel_event(struct AppState*, const SDL_Event*);
// Displays retained original/matched metrics and atomic listening position without rendering PCM.
void daw_ab_panel_render(SDL_Renderer*, struct AppState*);
