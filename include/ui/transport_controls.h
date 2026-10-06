#pragma once
#include "ui/transport.h"
#include "ui/daw_ui_button.h"

// Stable transport commands retain app-owned project, audio and viewport meaning.
typedef enum DawTransportAction {
    DAW_TRANSPORT_LOAD, DAW_TRANSPORT_SAVE, DAW_TRANSPORT_PLAY, DAW_TRANSPORT_STOP,
    DAW_TRANSPORT_GRID, DAW_TRANSPORT_BEATS, DAW_TRANSPORT_FIT_WIDTH,
    DAW_TRANSPORT_FIT_HEIGHT, DAW_TRANSPORT_ACTION_COUNT
} DawTransportAction;

// Synchronizes visible geometry and invalidates ownership across modal/engine changes.
void daw_transport_controls_sync(TransportUI* ui, const struct AppState* state);
// Routes shared release activation; existing global Space shortcuts remain authoritative.
bool daw_transport_controls_event(struct AppState* state, const SDL_Event* event);
// Paints one app-colored rounded surface and one synchronous measured caption.
void daw_transport_control_draw(SDL_Renderer* renderer, const TransportUI* ui,
    DawTransportAction action, const char* label, bool active, bool hovered,
    const DawThemePalette* palette);
// Executes existing direct command bodies without another engine command registry.
void transport_input_activate_control(struct AppState* state, DawTransportAction action,
    uint32_t modifiers);
