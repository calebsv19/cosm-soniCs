#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "ui/clip_inspector.h"

struct AppState;
struct EngineClip;

// Initializes fade-related inspector input state.
void inspector_fade_input_init(struct AppState* state);
// Resets fade-related inspector input state when showing a clip.
void inspector_fade_input_show(struct AppState* state, const struct EngineClip* clip);
// Updates fade-related inspector input state when switching clips.
void inspector_fade_input_set_clip(struct AppState* state, const struct EngineClip* clip);
// Syncs fade lengths from the engine when not actively adjusting.
void inspector_fade_input_sync(struct AppState* state, const struct EngineClip* clip);
// Handles a waveform mouse-down event for fade selection and dragging.
bool inspector_fade_input_handle_waveform_mouse_down(struct AppState* state,
                                                     const ClipInspectorLayout* layout,
                                                     const SDL_Point* point,
                                                     bool shift_held,
                                                     bool alt_held);
// Handles a fade track mouse-down event for a specific fade side.
bool inspector_fade_input_handle_track_mouse_down(struct AppState* state,
                                                  const ClipInspectorLayout* layout,
                                                  const SDL_Point* point,
                                                  bool shift_held,
                                                  bool is_fade_in);
// Clears any active fade drag state on mouse up.
void inspector_fade_input_handle_mouse_up(struct AppState* state);
// Updates pending fade drags from mouse movement.
bool inspector_fade_input_handle_pending_drag(struct AppState* state, int mouse_x);
// Updates active fade drags from mouse movement.
bool inspector_fade_input_handle_active_drag(struct AppState* state, int mouse_x);
// Handles fade curve cycling for key input when fades are selected.
bool inspector_fade_input_handle_keydown(struct AppState* state, SDL_Keycode key);
