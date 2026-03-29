#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct DawThemePalette {
    SDL_Color menu_fill;
    SDL_Color timeline_fill;
    SDL_Color inspector_fill;
    SDL_Color library_fill;
    SDL_Color pane_border;
    SDL_Color pane_highlight_fill;
    SDL_Color pane_highlight_border;
    SDL_Color title_text;
    SDL_Color text_primary;
    SDL_Color text_muted;
    SDL_Color control_fill;
    SDL_Color control_hover_fill;
    SDL_Color control_active_fill;
    SDL_Color control_border;
    SDL_Color slider_track;
    SDL_Color slider_handle;
    SDL_Color slider_handle_hover;
    SDL_Color timeline_border;
    SDL_Color grid_minor;
    SDL_Color grid_sub;
    SDL_Color grid_major;
    SDL_Color grid_downbeat;
    SDL_Color selection_fill;
    SDL_Color accent_primary;
    SDL_Color accent_warning;
    SDL_Color accent_error;
} DawThemePalette;

bool daw_shared_theme_resolve_palette(DawThemePalette* out_palette);
SDL_Color daw_shared_theme_title_color(void);
bool daw_shared_font_resolve_ui_regular(char* out_path, size_t out_path_size, int* out_point_size);
bool daw_shared_theme_cycle_next(void);
bool daw_shared_theme_cycle_prev(void);
bool daw_shared_theme_set_preset(const char* preset_name);
bool daw_shared_theme_current_preset(char* out_name, size_t out_name_size);
bool daw_shared_theme_load_persisted(void);
bool daw_shared_theme_save_persisted(void);
int daw_shared_font_zoom_step(void);
bool daw_shared_font_set_zoom_step(int step);
bool daw_shared_font_step_by(int delta);
bool daw_shared_font_reset_zoom_step(void);
bool daw_shared_font_zoom_load_persisted(void);
bool daw_shared_font_zoom_save_persisted(void);
