#pragma once

#include "ui/midi_editor.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;

int midi_editor_max_int(int a, int b);
bool midi_editor_rect_valid(const SDL_Rect* rect);
int midi_editor_sample_rate(const struct AppState* state);
int midi_editor_quantize_division(const struct AppState* state);
const char* midi_editor_quantize_label(const struct AppState* state);
