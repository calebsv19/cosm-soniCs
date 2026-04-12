#pragma once

#include <SDL2/SDL.h>

typedef struct TimelineTheme {
    SDL_Color bg;
    SDL_Color header_fill;
    SDL_Color header_border;
    SDL_Color text;
    SDL_Color text_muted;
    SDL_Color button_fill;
    SDL_Color button_hover_fill;
    SDL_Color button_disabled_fill;
    SDL_Color button_border;
    SDL_Color lane_header_fill;
    SDL_Color lane_header_fill_active;
    SDL_Color lane_header_border;
    SDL_Color clip_fill;
    SDL_Color clip_border;
    SDL_Color clip_border_selected;
    SDL_Color clip_text;
    SDL_Color waveform;
    SDL_Color loop_fill;
    SDL_Color loop_border;
    SDL_Color loop_handle_start;
    SDL_Color loop_handle_end;
    SDL_Color loop_handle_border;
    SDL_Color loop_label;
    SDL_Color playhead;
    SDL_Color toggle_active_loop;
    SDL_Color toggle_active_snap;
    SDL_Color toggle_active_auto;
    SDL_Color toggle_active_tempo;
    SDL_Color toggle_active_label;
} TimelineTheme;
