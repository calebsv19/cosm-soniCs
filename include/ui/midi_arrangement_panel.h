#pragma once
#include "daw/arrangement.h"
#include "kit_ui_text_edit.h"
#include <SDL2/SDL.h>
struct AppState;
// Owns staged region placement fields and an independent complete arrangement guard.
typedef struct DawMidiArrangementUI {
    bool open;
    int focused, cursor, pressed;
    char fields[3][24], message[192];
    KitUiTextEdit text_edit;
    DawActionTarget target;
    DawArrangementSnapshot* captured;
} DawMidiArrangementUI;
// Shares visible form geometry between painting, pointer input and native control routing.
typedef struct DawMidiArrangementLayout { SDL_Rect entry, panel, fields[3], buttons[6]; } DawMidiArrangementLayout;
// Fits the arrangement form into the existing MIDI editor pane.
void daw_midi_arrangement_layout(const struct AppState*, DawMidiArrangementLayout*);
// Opens the selected region with staged fields and complete authored preconditions.
bool daw_midi_arrangement_open(struct AppState*);
// Activates an entry or modal action through the existing product control owner.
bool daw_midi_arrangement_activate(struct AppState*, int x, int y);
// Routes modal text and matching-release actions before underlying editor edits.
bool daw_midi_arrangement_event(struct AppState*, const SDL_Event*);
// Paints staged values without touching authored sound or layout.
void daw_midi_arrangement_render(SDL_Renderer*, const struct AppState*);
// Releases private snapshots and text ownership when this form or application closes.
void daw_midi_arrangement_release(struct AppState*);
