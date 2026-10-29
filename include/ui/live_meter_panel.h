#pragma once
#include <SDL2/SDL.h>
struct AppState;
// Retains live scope, exact monitor gesture guard and per-label frame-lifetime presentation
// strings.
typedef struct DawLiveMeterUI {
    bool open, selected;
    int pressed;
    char guard[128], labels[4][48], lines[7][256], message[160];
} DawLiveMeterUI;
// Shares fully clipped native meter/control geometry with headless qualification.
typedef struct DawLiveMeterLayout {
    SDL_Rect panel, buttons[4];
    bool editable;
} DawLiveMeterLayout;
// Opens live meter readback without starting transport or changing audio implicitly.
bool daw_live_meter_panel_open(struct AppState*);
// Computes bounded shared meter controls in the effects pane.
void daw_live_meter_panel_layout(const struct AppState*, DawLiveMeterLayout*);
// Routes matching releases against the monitor endpoint/revision visible at press.
bool daw_live_meter_panel_event(struct AppState*, const SDL_Event*);
// Draws qualified channel/phase/mono measurements and stale/warm-up state using persistent backing.
void daw_live_meter_panel_render(SDL_Renderer*, struct AppState*);
// Returns a persistent toolbar indication when listening is mono even with the panel closed.
const char* daw_live_meter_toolbar_label(struct AppState*);
