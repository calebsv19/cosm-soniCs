#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;
struct InputManager;
struct Pane;

bool library_input_is_editing(const struct AppState* state);
void library_input_start_edit(struct AppState* state, const struct Pane* library_pane, int mouse_x);
void library_input_stop_edit(struct AppState* state);
bool library_input_handle_event(struct InputManager* manager, struct AppState* state, const SDL_Event* event);
bool library_input_open_folder_dialog(struct AppState* state);
bool library_input_handle_primary_click(struct AppState* state, int mouse_x, int mouse_y);
bool library_input_handle_drop_file(struct AppState* state, const char* source_path, int drop_x, int drop_y);
