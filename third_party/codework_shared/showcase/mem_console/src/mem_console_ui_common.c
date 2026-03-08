#include "mem_console_ui_common.h"

#include <stdio.h>
#include <string.h>

int mem_console_ui_estimate_char_width_px(CoreFontTextSizeTier text_tier) {
    switch (text_tier) {
        case CORE_FONT_TEXT_SIZE_HEADER:
            return 13;
        case CORE_FONT_TEXT_SIZE_TITLE:
            return 11;
        case CORE_FONT_TEXT_SIZE_PARAGRAPH:
            return 9;
        case CORE_FONT_TEXT_SIZE_CAPTION:
            return 8;
        case CORE_FONT_TEXT_SIZE_BASIC:
        default:
            return 9;
    }
}

int mem_console_ui_clamp_cursor_for_text(const char *text, int cursor) {
    int len;

    if (!text) {
        return 0;
    }

    len = (int)strlen(text);
    if (cursor < 0) return 0;
    if (cursor > len) return len;
    return cursor;
}

float mem_console_ui_measure_text_width_px(const KitRenderContext *render_ctx,
                                           CoreFontRoleId font_role,
                                           CoreFontTextSizeTier text_tier,
                                           const char *text) {
    KitRenderTextMetrics metrics;
    CoreResult result;
    size_t len;
    int char_w;

    if (!text || !text[0]) {
        return 0.0f;
    }

    if (render_ctx) {
        result = kit_render_measure_text(render_ctx, font_role, text_tier, text, &metrics);
        if (result.code == CORE_OK && metrics.width_px >= 0.0f) {
            return metrics.width_px;
        }
    }

    len = strlen(text);
    char_w = mem_console_ui_estimate_char_width_px(text_tier);
    if (char_w < 1) {
        char_w = 8;
    }
    return (float)((int)len * char_w);
}

static float mem_console_ui_measure_prefix_width_px(const KitRenderContext *render_ctx,
                                                    CoreFontRoleId font_role,
                                                    CoreFontTextSizeTier text_tier,
                                                    const char *text,
                                                    int prefix_len) {
    char prefix[1024];
    int len;

    if (!text) {
        return 0.0f;
    }

    len = mem_console_ui_clamp_cursor_for_text(text, prefix_len);
    if (len <= 0) {
        return 0.0f;
    }
    if (len >= (int)sizeof(prefix)) {
        len = (int)sizeof(prefix) - 1;
    }

    memcpy(prefix, text, (size_t)len);
    prefix[len] = '\0';
    return mem_console_ui_measure_text_width_px(render_ctx, font_role, text_tier, prefix);
}

int mem_console_ui_cursor_index_for_click(const char *text,
                                          const KitRenderContext *render_ctx,
                                          float mouse_x,
                                          float text_origin_x,
                                          CoreFontRoleId font_role,
                                          CoreFontTextSizeTier text_tier) {
    float delta_x;
    float advance = 0.0f;
    char glyph[2];
    int index = 0;
    int len;

    if (!text) {
        return 0;
    }

    delta_x = mouse_x - text_origin_x;
    if (delta_x <= 0.0f) {
        return 0;
    }

    len = (int)strlen(text);
    glyph[1] = '\0';
    for (index = 0; index < len; ++index) {
        float glyph_width;

        glyph[0] = text[index];
        glyph_width = mem_console_ui_measure_text_width_px(render_ctx, font_role, text_tier, glyph);
        if (glyph_width <= 0.0f) {
            glyph_width = (float)mem_console_ui_estimate_char_width_px(text_tier);
            if (glyph_width <= 0.0f) {
                glyph_width = 8.0f;
            }
        }

        if (delta_x < advance + (glyph_width * 0.5f)) {
            return index;
        }
        advance += glyph_width;
    }
    return index;
}

CoreResult mem_console_ui_draw_info_line_custom(KitUiContext *ui_ctx,
                                                KitRenderFrame *frame,
                                                KitRenderRect rect,
                                                const char *text,
                                                CoreThemeColorToken token,
                                                CoreFontRoleId font_role,
                                                CoreFontTextSizeTier text_tier) {
    return kit_ui_draw_label_custom(ui_ctx, frame, rect, text, token, font_role, text_tier);
}

CoreResult mem_console_ui_draw_button_custom(KitUiContext *ui_ctx,
                                             KitRenderFrame *frame,
                                             KitRenderRect rect,
                                             const char *text,
                                             KitUiWidgetState state,
                                             CoreFontRoleId font_role,
                                             CoreFontTextSizeTier text_tier) {
    return kit_ui_draw_button_custom(ui_ctx, frame, rect, text, state, font_role, text_tier);
}

CoreResult mem_console_ui_draw_editable_line(KitUiContext *ui_ctx,
                                             const KitRenderContext *render_ctx,
                                             KitRenderFrame *frame,
                                             KitRenderRect rect,
                                             const char *text,
                                             CoreThemeColorToken token,
                                             CoreFontRoleId font_role,
                                             CoreFontTextSizeTier text_tier,
                                             int draw_caret,
                                             int cursor_index) {
    CoreResult result;

    if (!ui_ctx || !render_ctx || !frame || !text) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid editable line" };
    }

    result = kit_ui_clip_push(ui_ctx, frame, rect);
    if (result.code != CORE_OK) {
        return result;
    }

    if (text[0] != '\0') {
        result = mem_console_ui_draw_info_line_custom(ui_ctx,
                                                      frame,
                                                      rect,
                                                      text,
                                                      token,
                                                      font_role,
                                                      text_tier);
        if (result.code != CORE_OK) {
            (void)kit_ui_clip_pop(ui_ctx, frame);
            return result;
        }
    }

    if (draw_caret) {
        KitRenderColor caret_color;
        KitRenderLineCommand line_cmd;
        int cursor = mem_console_ui_clamp_cursor_for_text(text, cursor_index);
        float text_x = rect.x + ui_ctx->style.padding;
        float caret_offset = mem_console_ui_measure_prefix_width_px(render_ctx,
                                                                    font_role,
                                                                    text_tier,
                                                                    text,
                                                                    cursor);
        float caret_x = text_x + caret_offset;
        float min_x = text_x;
        float max_x = rect.x + rect.width - ui_ctx->style.padding;

        if (caret_x < min_x) caret_x = min_x;
        if (caret_x > max_x) caret_x = max_x;

        result = mem_console_ui_resolve_theme_color(render_ctx, CORE_THEME_COLOR_ACCENT_PRIMARY, &caret_color);
        if (result.code != CORE_OK) {
            (void)kit_ui_clip_pop(ui_ctx, frame);
            return result;
        }

        line_cmd.p0.x = caret_x;
        line_cmd.p0.y = rect.y + 5.0f;
        line_cmd.p1.x = caret_x;
        line_cmd.p1.y = rect.y + rect.height - 5.0f;
        line_cmd.thickness = 1.0f;
        line_cmd.color = caret_color;
        line_cmd.transform = kit_render_identity_transform();
        result = kit_render_push_line(frame, &line_cmd);
        if (result.code != CORE_OK) {
            (void)kit_ui_clip_pop(ui_ctx, frame);
            return result;
        }
    }

    return kit_ui_clip_pop(ui_ctx, frame);
}

CoreResult mem_console_ui_resolve_theme_color(const KitRenderContext *render_ctx,
                                              CoreThemeColorToken token,
                                              KitRenderColor *out_color) {
    CoreResult result;

    if (!render_ctx || !out_color) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid theme color request" };
    }

    result = kit_render_resolve_theme_color(render_ctx, token, out_color);
    if (result.code != CORE_OK) {
        *out_color = (KitRenderColor){255, 255, 255, 255};
    }
    return core_result_ok();
}

CoreResult mem_console_ui_push_themed_rect(const KitRenderContext *render_ctx,
                                           KitRenderFrame *frame,
                                           KitRenderRect rect,
                                           float radius,
                                           CoreThemeColorToken token) {
    KitRenderColor color;
    CoreResult result;

    result = mem_console_ui_resolve_theme_color(render_ctx, token, &color);
    if (result.code != CORE_OK) {
        return result;
    }

    return kit_render_push_rect(frame,
                                &(KitRenderRectCommand){
                                    rect, radius, color, {0.0f, 0.0f, 1.0f, 1.0f}
                                });
}

CoreResult mem_console_ui_draw_wrapped_text_block(KitUiContext *ui_ctx,
                                                  KitRenderFrame *frame,
                                                  char line_storage[][256],
                                                  int line_storage_count,
                                                  KitRenderRect bounds,
                                                  const char *text,
                                                  CoreThemeColorToken token,
                                                  CoreFontTextSizeTier text_tier,
                                                  int max_lines) {
    const char *cursor;
    int line_count = 0;
    int max_chars;
    CoreResult result;

    if (!ui_ctx || !frame || !line_storage || line_storage_count <= 0 || !text || max_lines <= 0) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid wrapped text" };
    }
    if (max_lines > line_storage_count) {
        max_lines = line_storage_count;
    }

    max_chars = (int)(bounds.width / 8.0f);
    if (max_chars < 18) max_chars = 18;
    if (max_chars > 120) max_chars = 120;

    result = kit_ui_clip_push(ui_ctx, frame, bounds);
    if (result.code != CORE_OK) {
        return result;
    }

    cursor = text;
    while (*cursor != '\0' && line_count < max_lines) {
        char *line = line_storage[line_count];
        const char *line_start;
        const char *break_at = 0;
        int len = 0;
        KitRenderRect line_rect;

        while (*cursor == ' ') {
            cursor += 1;
        }
        line_start = cursor;

        while (*cursor != '\0' && *cursor != '\n' && len < max_chars) {
            if (*cursor == ' ') {
                break_at = cursor;
            }
            cursor += 1;
            len += 1;
        }

        if (*cursor != '\0' && *cursor != '\n' && len >= max_chars && break_at && break_at > line_start) {
            cursor = break_at;
            len = (int)(break_at - line_start);
        }

        if (len <= 0) {
            if (*cursor == '\n') {
                cursor += 1;
            } else if (*cursor != '\0') {
                cursor += 1;
            }
            continue;
        }

        if ((size_t)len >= 256u) {
            len = 255;
        }
        memcpy(line, line_start, (size_t)len);
        line[len] = '\0';

        line_rect = (KitRenderRect){
            bounds.x + 8.0f,
            bounds.y + 8.0f + ((float)line_count * 24.0f),
            bounds.width - 16.0f,
            20.0f
        };
        result = mem_console_ui_draw_info_line_custom(ui_ctx,
                                                      frame,
                                                      line_rect,
                                                      line,
                                                      token,
                                                      CORE_FONT_ROLE_UI_REGULAR,
                                                      text_tier);
        if (result.code != CORE_OK) {
            (void)kit_ui_clip_pop(ui_ctx, frame);
            return result;
        }

        while (*cursor == ' ') {
            cursor += 1;
        }
        if (*cursor == '\n') {
            cursor += 1;
        }

        line_count += 1;
    }

    return kit_ui_clip_pop(ui_ctx, frame);
}
