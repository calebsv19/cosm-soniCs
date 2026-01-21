#include "render/timer_hud_adapter.h"

#include "timer_hud/time_scope.h"
#include "timer_hud/timer_hud_backend.h"
#include "ui/font.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

static SDL_Renderer* g_timer_hud_renderer = NULL;
static SDL_Window* g_timer_hud_window = NULL;

// timer_hud_get_screen_size reports the current drawable size for the HUD layout.
static int timer_hud_get_screen_size(int* out_w, int* out_h) {
    if (!g_timer_hud_window) return 0;
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(g_timer_hud_window, &w, &h);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return w > 0 && h > 0;
}

// timer_hud_measure_text returns pixel dimensions for the HUD font.
static int timer_hud_measure_text(const char* text, int* out_w, int* out_h) {
    if (!text) return 0;
    if (out_w) *out_w = ui_measure_text_width(text, 1.0f);
    if (out_h) *out_h = ui_font_line_height(1.0f);
    return 1;
}

// timer_hud_line_height returns the default HUD line height.
static int timer_hud_line_height(void) {
    return ui_font_line_height(1.0f);
}

// timer_hud_draw_rect draws the HUD background rectangle via the Vulkan SDL compat layer.
static void timer_hud_draw_rect(int x, int y, int w, int h, TimerHUDColor color) {
    if (!g_timer_hud_renderer) return;
    SDL_SetRenderDrawColor(g_timer_hud_renderer, color.r, color.g, color.b, color.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(g_timer_hud_renderer, &rect);
}

// timer_hud_draw_text draws HUD text using the DAW font renderer.
static void timer_hud_draw_text(const char* text, int x, int y, int align_flags, TimerHUDColor color) {
    if (!g_timer_hud_renderer || !text) return;
    int text_w = 0;
    int text_h = 0;
    timer_hud_measure_text(text, &text_w, &text_h);

    if (align_flags & TIMER_HUD_ALIGN_CENTER)  x -= text_w / 2;
    if (align_flags & TIMER_HUD_ALIGN_RIGHT)   x -= text_w;
    if (align_flags & TIMER_HUD_ALIGN_MIDDLE)  y -= text_h / 2;
    if (align_flags & TIMER_HUD_ALIGN_BOTTOM)  y -= text_h;

    ui_draw_text(g_timer_hud_renderer, x, y, text,
                 (SDL_Color){color.r, color.g, color.b, color.a}, 1.0f);
}

static const TimerHUDBackend g_timer_hud_backend = {
    .init = NULL,
    .shutdown = NULL,
    .get_screen_size = timer_hud_get_screen_size,
    .measure_text = timer_hud_measure_text,
    .get_line_height = timer_hud_line_height,
    .draw_rect = timer_hud_draw_rect,
    .draw_text = timer_hud_draw_text,
    .hud_padding = 6,
    .hud_spacing = 4,
    .hud_bg_alpha = 180
};

// timer_hud_register_backend connects TimerHUD to the DAW render helpers and settings path.
void timer_hud_register_backend(void) {
    ts_register_backend(&g_timer_hud_backend);
    ts_set_settings_path("config/timer_hud_settings.json");
}

// timer_hud_bind_context caches the active renderer/window for HUD draws.
void timer_hud_bind_context(const AppContext* ctx) {
    if (!ctx) return;
    g_timer_hud_renderer = ctx->renderer;
    g_timer_hud_window = ctx->window;
}
