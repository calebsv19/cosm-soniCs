#pragma once
#include <SDL2/SDL.h>
struct AppState;
// Retains native export choices, matching-release gestures and per-label frame-lifetime text.
typedef struct DawSongExportUI {
    bool open, normalize;
    int format, tail_choice, pressed;
    char labels[6][80], summary[256], path[512], message[160], pressed_job[96];
} DawSongExportUI;
// Shares native export button geometry between drawing and input qualification.
typedef struct DawSongExportLayout {
    SDL_Rect panel, buttons[6];
    bool editable;
} DawSongExportLayout;
// Opens whole-song choices without exporting implicitly or changing authored gains.
bool daw_song_export_panel_open(struct AppState*);
// Computes clipped export controls within the existing effects pane.
void daw_song_export_panel_layout(const struct AppState*, DawSongExportLayout*);
// Routes matching-release export/cancel controls before hidden effects rows.
bool daw_song_export_panel_event(struct AppState*, const SDL_Event*);
// Presents atomic progress and verified output paths with persistent text storage.
void daw_song_export_panel_render(SDL_Renderer*, struct AppState*);
