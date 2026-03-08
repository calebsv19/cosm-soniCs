#ifndef MEM_CONSOLE_UI_COMMON_H
#define MEM_CONSOLE_UI_COMMON_H

#include "mem_console_state.h"
#include "kit_ui.h"

int mem_console_ui_estimate_char_width_px(CoreFontTextSizeTier text_tier);
int mem_console_ui_clamp_cursor_for_text(const char *text, int cursor);
float mem_console_ui_measure_text_width_px(const KitRenderContext *render_ctx,
                                           CoreFontRoleId font_role,
                                           CoreFontTextSizeTier text_tier,
                                           const char *text);
int mem_console_ui_cursor_index_for_click(const char *text,
                                          const KitRenderContext *render_ctx,
                                          float mouse_x,
                                          float text_origin_x,
                                          CoreFontRoleId font_role,
                                          CoreFontTextSizeTier text_tier);

CoreResult mem_console_ui_draw_info_line_custom(KitUiContext *ui_ctx,
                                                KitRenderFrame *frame,
                                                KitRenderRect rect,
                                                const char *text,
                                                CoreThemeColorToken token,
                                                CoreFontRoleId font_role,
                                                CoreFontTextSizeTier text_tier);
CoreResult mem_console_ui_draw_button_custom(KitUiContext *ui_ctx,
                                             KitRenderFrame *frame,
                                             KitRenderRect rect,
                                             const char *text,
                                             KitUiWidgetState state,
                                             CoreFontRoleId font_role,
                                             CoreFontTextSizeTier text_tier);
CoreResult mem_console_ui_draw_editable_line(KitUiContext *ui_ctx,
                                             const KitRenderContext *render_ctx,
                                             KitRenderFrame *frame,
                                             KitRenderRect rect,
                                             const char *text,
                                             CoreThemeColorToken token,
                                             CoreFontRoleId font_role,
                                             CoreFontTextSizeTier text_tier,
                                             int draw_caret,
                                             int cursor_index);
CoreResult mem_console_ui_resolve_theme_color(const KitRenderContext *render_ctx,
                                              CoreThemeColorToken token,
                                              KitRenderColor *out_color);
CoreResult mem_console_ui_push_themed_rect(const KitRenderContext *render_ctx,
                                           KitRenderFrame *frame,
                                           KitRenderRect rect,
                                           float radius,
                                           CoreThemeColorToken token);
CoreResult mem_console_ui_draw_wrapped_text_block(KitUiContext *ui_ctx,
                                                  KitRenderFrame *frame,
                                                  char line_storage[][256],
                                                  int line_storage_count,
                                                  KitRenderRect bounds,
                                                  const char *text,
                                                  CoreThemeColorToken token,
                                                  CoreFontTextSizeTier text_tier,
                                                  int max_lines);

#endif
