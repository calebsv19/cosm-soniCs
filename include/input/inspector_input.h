#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;
struct EngineClip;
struct InputManager;

void inspector_input_init(struct AppState* state);
void inspector_input_show(struct AppState* state, int track_index, int clip_index, const struct EngineClip* clip);
void inspector_input_set_clip(struct AppState* state, int track_index, int clip_index);
void inspector_input_commit_if_editing(struct AppState* state);
void inspector_input_begin_rename(struct AppState* state);
void inspector_input_handle_event(struct InputManager* manager, struct AppState* state, const SDL_Event* event);
void inspector_input_handle_gain_drag(struct AppState* state, int mouse_x);
void inspector_input_stop_gain_drag(struct AppState* state);
void inspector_input_sync(struct AppState* state);
bool inspector_input_has_text_focus(const struct AppState* state);
bool inspector_input_has_focus(const struct AppState* state);
