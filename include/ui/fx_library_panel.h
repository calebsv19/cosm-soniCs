#pragma once
#include "daw/fx_library.h"
#include "kit_ui_text_edit.h"
#include <SDL2/SDL.h>
struct AppState;
// Owns private library browsing, explicit save input and revision-bound loaded-chain metadata.
typedef struct DawFxLibraryUI {
    DawFxPresetList* library;
    bool open, saving, update, editing, has_loaded;
    int cursor, scroll, pressed;
    uint64_t generation;
    char search[96], name[96], message[160], rows[8][160];
    KitUiTextEdit text_edit;
    DawFxSnapshot captured;
    DawFxPreset loaded;
} DawFxLibraryUI;
// Shares bounded overlay geometry and row mapping between painting and accepted activation.
typedef struct DawFxLibraryLayout { SDL_Rect panel, field, buttons[4], rows[8], status; int indices[8], count; } DawFxLibraryLayout;
// Computes bounded browser/save controls inside the existing effects pane.
void daw_fx_library_panel_layout(const struct AppState*, DawFxLibraryLayout*);
// Draws cached library contents and shared product controls without filesystem IO in the painter.
void daw_fx_library_panel_render(SDL_Renderer*, const struct AppState*);
// Opens the selected chain library through the shared effects-header control owner.
bool daw_fx_library_panel_open(struct AppState*);
// Routes modal text, matching press/release activations and explicit revision-aware actions.
bool daw_fx_library_panel_event(struct AppState*, const SDL_Event*);
// Releases cached private entries and native text ownership at application teardown.
void daw_fx_library_panel_release(struct AppState*);
