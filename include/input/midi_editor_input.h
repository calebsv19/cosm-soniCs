#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

struct AppState;
struct InputManager;

bool midi_editor_input_handle_event(struct InputManager* manager, struct AppState* state, const SDL_Event* event);
void midi_editor_input_update(struct InputManager* manager, struct AppState* state, bool left_was_down, bool left_is_down);
bool midi_editor_input_qwerty_capturing(const struct AppState* state);
void midi_editor_input_set_selected_clip(struct AppState* state,
                                         int track_index,
                                         int clip_index,
                                         uint64_t clip_creation_index);
void midi_editor_input_clear_selected_clip(struct AppState* state);
