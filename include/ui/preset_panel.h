#pragma once
#include "daw/preset_library.h"
#include "daw/app_actions.h"
#include "kit_ui_text_edit.h"
#include <SDL2/SDL.h>
struct AppState;
// Owns bounded preset browsing, captured save input and loaded-sound comparison on the UI thread.
typedef struct DawPresetUI {
    DawPresetList library;
    bool open, editing, save_open, update;
    char search[96], name[96], message[160];
    char row_labels[DAW_PRESET_CAPACITY][160];
    int cursor, scroll, selected_row;
    uint64_t library_generation;
    KitUiTextEdit text_edit;
    DawInstrumentSnapshot captured;
    DawActionTarget loaded_target;
    DawUserPreset loaded;
    bool has_loaded;
} DawPresetUI;
// Shares visible preset geometry between pointer ownership and actual product rendering.
typedef struct DawPresetPanelLayout {
    SDL_Rect bar, library_button, save_button, update_button, note_button, chord_button;
    SDL_Rect overlay, field, confirm, cancel, rows[8], refresh, status;
    int item_indices[8], row_count;
} DawPresetPanelLayout;
// Calculates responsive controls and a bounded browser/save overlay inside the instrument pane.
void daw_preset_panel_layout(const struct AppState*, SDL_Rect panel, SDL_Rect header, DawPresetPanelLayout*);
// Paints persistent row labels and existing shared text/button presentation.
void daw_preset_panel_render(SDL_Renderer*, const struct AppState*, const DawPresetPanelLayout*);
// Handles browsing, bounded shared text input and guarded save/apply actions before generic editor shortcuts.
bool daw_preset_panel_event(struct AppState*, const SDL_Event*);
// Activates one discrete preset control through the same pointer owner as keyboard focus.
bool daw_preset_panel_activate(struct AppState*, int x, int y);
// Refreshes the cached user library only on open, explicit refresh or a library mutation.
void daw_preset_panel_refresh(struct AppState*);
// Resolves the current sound label and modified marker without making playback depend on library lookup.
const char* daw_preset_panel_sound_name(const struct AppState*, const DawInstrumentSnapshot*, bool* modified);
// Remembers the loaded source for editing feedback, never for audio reconstruction.
void daw_preset_panel_remember(struct AppState*, DawActionTarget, const DawUserPreset*);
