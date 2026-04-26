#include "ui/font.h"

#include "ui/font_bridge.h"
#include "ui/text_draw.h"

bool ui_font_set(const char* path, int base_point_size) {
    bool ok = false;

    daw_text_shutdown();
    ok = daw_font_bridge_set_active(path, base_point_size);
    return ok;
}

void ui_font_shutdown(void) {
    daw_text_shutdown();
    daw_font_bridge_shutdown();
}

void ui_font_invalidate_cache(SDL_Renderer* renderer) {
    daw_text_invalidate_cache(renderer);
}

void ui_draw_text(SDL_Renderer* renderer, int x, int y, const char* text, SDL_Color color, float scale) {
    DawResolvedFont resolved = {0};

    if (!renderer || !text) {
        return;
    }
    if (!daw_font_bridge_acquire(scale, &resolved) || !resolved.font) {
        return;
    }
    daw_text_draw_utf8_at(renderer, resolved.font, text, x, y, color);
}

void ui_draw_text_clipped(SDL_Renderer* renderer,
                          int x,
                          int y,
                          const char* text,
                          SDL_Color color,
                          float scale,
                          int max_width) {
    DawResolvedFont resolved = {0};

    if (!renderer || !text || max_width <= 0) {
        return;
    }
    if (!daw_font_bridge_acquire(scale, &resolved) || !resolved.font) {
        return;
    }
    daw_text_draw_utf8_clipped(renderer, resolved.font, text, x, y, color, max_width);
}

int ui_measure_text_width(const char* text, float scale) {
    DawResolvedFont resolved = {0};
    int width = 0;
    int height = 0;

    if (!text) {
        return 0;
    }
    if (!daw_font_bridge_acquire(scale, &resolved) || !resolved.font) {
        return 0;
    }
    if (!daw_text_measure_utf8(NULL, resolved.font, text, &width, &height)) {
        return 0;
    }
    return width;
}

int ui_font_line_height(float scale) {
    DawResolvedFont resolved = {0};
    int width = 0;
    int height = 0;

    if (!daw_font_bridge_acquire(scale, &resolved) || !resolved.font) {
        return 0;
    }
    if (!daw_text_measure_utf8(NULL, resolved.font, "", &width, &height)) {
        return 0;
    }
    return height;
}
