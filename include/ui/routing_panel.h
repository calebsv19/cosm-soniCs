#pragma once
#include "daw/routing_actions.h"
#include "daw/fx_actions.h"
#include "kit_ui_text_edit.h"
#include <SDL2/SDL.h>
struct AppState;
// Owns staged lane routing, copied guards and bounded shared percentage editing.
typedef struct DawRoutingUI {
    bool open, staged_return, automation_mode, effect_mode;
    DawFxSnapshot effect_capture; // Owns the selected stable insert chain guard.
    int auto_lane,auto_point;
    char auto_frame[32],auto_value[32],auto_labels[16][160];
    int selected, focused, cursor, pressed;
    EngineRoutingSnapshot captured;
    uint64_t destinations[ENGINE_SEND_CAP];
    char gains[ENGINE_SEND_CAP][32], names[ENGINE_ROUTING_TRACK_CAP][ENGINE_CLIP_NAME_MAX];
    char message[160], lane_label[160], send_labels[ENGINE_SEND_CAP][160];
    KitUiTextEdit text_edit;
} DawRoutingUI;
// Shares visible panel/button/field geometry between native input and rendering.
typedef struct DawRoutingLayout {
    SDL_Rect panel, buttons[8], destinations[ENGINE_SEND_CAP], gains[ENGINE_SEND_CAP], status;
    bool editable;
} DawRoutingLayout;
// Opens the routing modal over the existing effects pane with a complete captured guard.
bool daw_routing_panel_open(struct AppState*);
// Computes bounded native routing controls inside the current effects pane.
void daw_routing_panel_layout(const struct AppState*, DawRoutingLayout*);
// Routes staged input and guarded matching-release creation/application.
bool daw_routing_panel_event(struct AppState*, const SDL_Event*);
// Draws cached lane names and existing shared controls without IO or audio rendering.
void daw_routing_panel_render(SDL_Renderer*, const struct AppState*);
// Releases text and staged ownership on close, project replacement and teardown.
void daw_routing_panel_release(struct AppState*);
