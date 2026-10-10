#pragma once
#include "daw/midi_patterns.h"
#include <SDL2/SDL.h>
// Holds transient pattern choices and stable frame-long text for the selected-region workflow.
typedef struct DawMidiPatternUI { bool open, replace; int pressed; DawActionTarget target; char message[192]; } DawMidiPatternUI;
// Shares entry/popup button geometry between native painting, input and discrete focus routing.
typedef struct DawMidiPatternLayout { SDL_Rect entry, panel, buttons[8]; } DawMidiPatternLayout;
// Computes a compact footer entry and a bounded popup within the current piano-roll pane.
void daw_midi_pattern_layout(const struct AppState*, DawMidiPatternLayout*);
// Paints cached labels and controls without synthesizing or mutating any MIDI notes.
void daw_midi_pattern_render(SDL_Renderer*, struct AppState*);
// Applies one selected major-key pattern using the common snapshot/edit transaction.
bool daw_midi_pattern_activate(struct AppState*, int, int);
// Routes popup input before hidden piano-roll commands while preserving armed keyboard ownership.
bool daw_midi_pattern_event(struct AppState*, const SDL_Event*);
