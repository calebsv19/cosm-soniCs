#include "render/timer_hud_adapter.h"

#include "timer_hud/time_scope.h"
#include "timer_hud/timer_hud_backend.h"
#include "ui/font.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static SDL_Renderer* g_timer_hud_renderer = NULL;
static SDL_Window* g_timer_hud_window = NULL;
static TimerHUDSession* g_timer_hud_session = NULL;

#define TIMER_HUD_DEFAULT_SETTINGS_PATH "config/timer_hud_settings.json"

TimerHUDSession* timer_hud_session(void) {
    if (!g_timer_hud_session) {
        g_timer_hud_session = ts_session_create();
    }
    return g_timer_hud_session;
}

static int timer_hud_resolve_abs_from_cwd(const char* relative_path,
                                          char* out,
                                          size_t out_cap) {
    char cwd[PATH_MAX] = {0};
    int written = 0;
    if (!relative_path || !relative_path[0] || !out || out_cap == 0) {
        return 0;
    }
    if (relative_path[0] == '/') {
        written = snprintf(out, out_cap, "%s", relative_path);
        return written > 0 && (size_t)written < out_cap;
    }
    if (!getcwd(cwd, sizeof(cwd))) {
        return 0;
    }
    written = snprintf(out, out_cap, "%s/%s", cwd, relative_path);
    return written > 0 && (size_t)written < out_cap;
}

static bool timer_hud_parse_bool_token(const char* value, bool* out_enabled) {
    if (!value || !out_enabled) {
        return false;
    }
    if (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "on") == 0 || strcasecmp(value, "yes") == 0) {
        *out_enabled = true;
        return true;
    }
    if (strcmp(value, "0") == 0 || strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "off") == 0 || strcasecmp(value, "no") == 0) {
        *out_enabled = false;
        return true;
    }
    return false;
}

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

// timer_hud_draw_line draws line segments for TimerHUD graph mode.
static void timer_hud_draw_line(int x1, int y1, int x2, int y2, TimerHUDColor color) {
    if (!g_timer_hud_renderer) return;
    SDL_SetRenderDrawBlendMode(g_timer_hud_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_timer_hud_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(g_timer_hud_renderer, x1, y1, x2, y2);
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
    .draw_line = timer_hud_draw_line,
    .draw_text = timer_hud_draw_text,
    .hud_padding = 6,
    .hud_spacing = 4,
    .hud_bg_alpha = 180
};

// timer_hud_register_backend connects TimerHUD to the DAW render helpers and settings path.
void timer_hud_register_backend(void) {
    TimerHUDSession* session = timer_hud_session();
    char default_settings_path[PATH_MAX] = {0};
    TimerHUDInitConfig init_config = {
        .program_name = "daw",
        .output_root = ".",
        .settings_path = TIMER_HUD_DEFAULT_SETTINGS_PATH,
        .default_settings_path = NULL,
        .seed_settings_if_missing = false,
    };
    const char* output_root = NULL;
    const char* override_path = NULL;

    if (!session) {
        fprintf(stderr, "[TimerHUD] failed to allocate DAW session.\n");
        return;
    }

    ts_session_register_backend(session, &g_timer_hud_backend);

    if (timer_hud_resolve_abs_from_cwd(TIMER_HUD_DEFAULT_SETTINGS_PATH,
                                       default_settings_path,
                                       sizeof(default_settings_path))) {
        init_config.default_settings_path = default_settings_path;
    }

    output_root = getenv("TIMERHUD_OUTPUT_ROOT");
    if (output_root && output_root[0]) {
        init_config.output_root = output_root;
    }

    override_path = getenv("DAW_TIMER_HUD_SETTINGS");
    if (override_path && override_path[0]) {
        init_config.settings_path = override_path;
        if (init_config.default_settings_path &&
            access(override_path, F_OK) != 0 &&
            access(init_config.default_settings_path, F_OK) == 0) {
            init_config.seed_settings_if_missing = true;
        }
    }

    (void)ts_session_apply_init_config(session, &init_config);
}

// timer_hud_bind_context caches the active renderer/window for HUD draws.
void timer_hud_bind_context(const AppContext* ctx) {
    if (!ctx) return;
    g_timer_hud_renderer = ctx->renderer;
    g_timer_hud_window = ctx->window;
}

void timer_hud_apply_startup_env_overrides(void) {
    TimerHUDSession* session = timer_hud_session();
    const char* hud_env = getenv("DAW_TIMER_HUD");
    const char* overlay_env = getenv("DAW_TIMER_HUD_OVERLAY");
    const char* visual_mode_env = getenv("DAW_TIMER_HUD_VISUAL_MODE");
    bool enabled = false;
    bool set_hybrid_by_default = false;
    TimerHUDVisualMode visual_mode = TIMER_HUD_VISUAL_MODE_INVALID;

    if (!session) {
        return;
    }

    if (hud_env && hud_env[0]) {
        if (timer_hud_parse_bool_token(hud_env, &enabled)) {
            ts_session_set_hud_enabled(session, enabled);
            set_hybrid_by_default = enabled;
        } else {
            fprintf(stderr,
                    "[TimerHUD] ignoring invalid DAW_TIMER_HUD=%s\n",
                    hud_env);
        }
    }

    if (overlay_env && overlay_env[0]) {
        if (timer_hud_parse_bool_token(overlay_env, &enabled)) {
            ts_session_set_hud_enabled(session, enabled);
            set_hybrid_by_default = enabled;
        } else {
            fprintf(stderr,
                    "[TimerHUD] ignoring invalid DAW_TIMER_HUD_OVERLAY=%s\n",
                    overlay_env);
        }
    }

    if (visual_mode_env && visual_mode_env[0]) {
        visual_mode = ts_visual_mode_from_string(visual_mode_env);
        if (visual_mode != TIMER_HUD_VISUAL_MODE_INVALID) {
            (void)ts_session_set_hud_visual_mode_kind(session, visual_mode);
        } else {
            fprintf(stderr,
                    "[TimerHUD] ignoring invalid DAW_TIMER_HUD_VISUAL_MODE=%s\n",
                    visual_mode_env);
        }
    } else if (set_hybrid_by_default) {
        (void)ts_session_set_hud_visual_mode_kind(session, TIMER_HUD_VISUAL_MODE_HYBRID);
    }

    fprintf(stderr,
            "[TimerHUD] daw startup hud_enabled=%d mode=%s log_enabled=%d log_file=%s\n",
            ts_session_is_hud_enabled(session) ? 1 : 0,
            ts_session_get_hud_visual_mode(session),
            ts_session_is_log_enabled(session) ? 1 : 0,
            ts_session_get_log_filepath(session));
}

void timer_hud_shutdown_session(void) {
    if (!g_timer_hud_session) {
        return;
    }
    ts_session_destroy(g_timer_hud_session);
    g_timer_hud_session = NULL;
}
