#include "ui/text_edit.h"
#include "kit_ui_text_presentation.h"
#include "ui/font.h"
#include "ui/render_utils.h"

// Holds synchronous native painter state; no queued text borrows stack storage.
typedef struct DawTextPainter {
    SDL_Renderer* renderer;
    SDL_Color text;
    float scale;
} DawTextPainter;

// Measures in the same logical coordinate space used by native text drawing.
static CoreResult text_measure(void* user, const char* text, float* width) {
    DawTextPainter* painter = user;
    *width = (float)ui_measure_text_width(text, painter->scale);
    return (CoreResult){CORE_OK, NULL};
}

// Paints shared selection/caret/preedit rectangles under the field clip.
static CoreResult text_rect(void* user, KitRenderRect rect, KitRenderColor color) {
    DawTextPainter* painter = user;
    SDL_SetRenderDrawColor(painter->renderer, color.r, color.g, color.b, color.a);
    SDL_Rect pixels = {(int)rect.x, (int)rect.y, (int)rect.width, (int)rect.height};
    SDL_RenderFillRect(painter->renderer, &pixels);
    return (CoreResult){CORE_OK, NULL};
}

// Consumes presentation-owned text immediately, before its frame-local storage expires.
static CoreResult text_run(void* user, KitRenderVec2 origin, const char* text) {
    DawTextPainter* painter = user;
    ui_draw_text(painter->renderer, (int)origin.x, (int)origin.y, text,
                 painter->text, painter->scale);
    return (CoreResult){CORE_OK, NULL};
}

void daw_text_edit_draw(SDL_Renderer* renderer, const KitUiTextEdit* stored,
    char* text, size_t capacity, int cursor, SDL_Rect viewport,
    SDL_Color ink, SDL_Color accent, float scale) {
    if (!renderer || !stored || viewport.w <= 0 || viewport.h <= 0) return;
    KitUiTextEdit edit = *stored;
    if (kit_ui_text_bind(&edit, text, capacity, stored->flags).code != CORE_OK) return;
    if (cursor < 0 || (size_t)cursor != edit.cursor)
        (void)kit_ui_text_position(&edit, cursor < 0 ? 0 : (size_t)cursor,
                                   cursor < 0 ? 0 : (size_t)cursor);
    DawTextPainter native = {renderer, ink, scale};
    KitUiTextPresentation presentation;
    KitUiTextPresentationOptions options = {0};
    int line_height = ui_font_line_height(scale);
    options.viewport = (KitRenderRect){viewport.x,
        viewport.y + (viewport.h - line_height) / 2, viewport.w, line_height};
    options.line_height = line_height;
    options.caret_width = 1;
    options.active = 1;
    options.reveal_caret = 1;
    if (kit_ui_text_presentation_build(&presentation, &edit, &options,
                                       text_measure, &native).code != CORE_OK) return;
    SDL_Rect previous, clip = viewport;
    SDL_bool had_clip = SDL_RenderIsClipEnabled(renderer);
    SDL_RenderGetClipRect(renderer, &previous);
    if (had_clip && !SDL_IntersectRect(&viewport, &previous, &clip)) return;
    SDL_RenderSetClipRect(renderer, &clip);
    KitUiTextPresentationColors colors = {
        {accent.r, accent.g, accent.b, 100}, {ink.r, ink.g, ink.b, ink.a},
        {accent.r, accent.g, accent.b, accent.a}};
    KitUiTextPainter painter = {&native, text_rect, text_run};
    (void)kit_ui_text_presentation_paint(&presentation, &colors, &painter);
    SDL_RenderSetClipRect(renderer, had_clip ? &previous : NULL);
}

void daw_text_edit_click(KitUiTextEdit* edit, char* text, size_t capacity,
    int* cursor, unsigned flags, float scale, int left, int width, int mouse_x) {
    if (!edit || !cursor || width <= 0) return;
    if (kit_ui_text_bind(edit, text, capacity, flags & 3u).code != CORE_OK) return;
    if (*cursor < 0 || (size_t)*cursor != edit->cursor)
        (void)kit_ui_text_position(edit, *cursor < 0 ? 0 : (size_t)*cursor,
                                   *cursor < 0 ? 0 : (size_t)*cursor);
    DawTextPainter native = {NULL, {0}, scale};
    KitUiTextPresentation presentation;
    KitUiTextPresentationOptions options = {0};
    options.line_height = ui_font_line_height(scale);
    options.viewport = (KitRenderRect){left, 0, width, options.line_height};
    options.caret_width = 1;
    options.active = 1;
    options.reveal_caret = 1;
    if (kit_ui_text_presentation_build(&presentation, edit, &options,
                                       text_measure, &native).code != CORE_OK) return;
    size_t position;
    if (kit_ui_text_presentation_hit(&presentation, mouse_x, 0,
                                     text_measure, &native, &position).code != CORE_OK) return;
    (void)kit_ui_text_position(edit, position, position);
    *cursor = (int)edit->cursor;
}
