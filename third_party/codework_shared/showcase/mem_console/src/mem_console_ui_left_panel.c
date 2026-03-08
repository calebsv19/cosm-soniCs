#include "mem_console_ui_left_panel.h"

#include "mem_console_ui_common.h"

CoreResult mem_console_ui_left_draw_project_filter_chips(KitUiContext *ui_ctx,
                                                         const KitRenderContext *render_ctx,
                                                         KitRenderFrame *frame,
                                                         MemConsoleState *state,
                                                         const KitUiInputState *input,
                                                         KitRenderRect bounds,
                                                         int wheel_y,
                                                         int input_enabled,
                                                         int *out_changed) {
    KitRenderRect layout_rects[MEM_CONSOLE_SCOPE_FILTER_LIMIT + 1];
    const char *layout_labels[MEM_CONSOLE_SCOPE_FILTER_LIMIT + 1];
    const char *layout_keys[MEM_CONSOLE_SCOPE_FILTER_LIMIT + 1];
    float x = 0.0f;
    float y = 0.0f;
    float right = bounds.width;
    float chip_height = 22.0f;
    float line_gap = 6.0f;
    float chip_gap = 6.0f;
    float content_height = 0.0f;
    float max_scroll = 0.0f;
    int i;
    int chip_count = 0;
    int changed = 0;

    if (!ui_ctx || !render_ctx || !frame || !state || !input || !out_changed) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid project filter draw request" };
    }

    {
        float all_width = mem_console_ui_measure_text_width_px(render_ctx,
                                                               CORE_FONT_ROLE_UI_REGULAR,
                                                               CORE_FONT_TEXT_SIZE_CAPTION,
                                                               "ALL PROJECTS");
        all_width += ui_ctx->style.padding * 2.0f + 10.0f;
        if (all_width < 84.0f) {
            all_width = 84.0f;
        }
        if (all_width > bounds.width) {
            all_width = bounds.width;
        }

        layout_rects[chip_count] = (KitRenderRect){x, y, all_width, chip_height};
        layout_labels[chip_count] = "ALL PROJECTS";
        layout_keys[chip_count] = "";
        chip_count += 1;
        x += all_width + chip_gap;
    }

    for (i = 0; i < state->project_filter_option_count; ++i) {
        float label_width;
        float chip_width;

        if (state->project_filter_keys[i][0] == '\0') {
            continue;
        }

        label_width = mem_console_ui_measure_text_width_px(render_ctx,
                                                           CORE_FONT_ROLE_UI_REGULAR,
                                                           CORE_FONT_TEXT_SIZE_CAPTION,
                                                           state->project_filter_labels[i]);
        chip_width = label_width + (ui_ctx->style.padding * 2.0f) + 10.0f;
        if (chip_width < 78.0f) {
            chip_width = 78.0f;
        }
        if (chip_width > bounds.width) {
            chip_width = bounds.width;
        }

        if (x + chip_width > right) {
            x = 0.0f;
            y += chip_height + line_gap;
        }
        if (chip_count >= (int)(MEM_CONSOLE_SCOPE_FILTER_LIMIT + 1)) {
            break;
        }
        layout_rects[chip_count] = (KitRenderRect){x, y, chip_width, chip_height};
        layout_labels[chip_count] = state->project_filter_labels[i];
        layout_keys[chip_count] = state->project_filter_keys[i];
        chip_count += 1;
        x += chip_width + chip_gap;
    }

    for (i = 0; i < chip_count; ++i) {
        float bottom = layout_rects[i].y + layout_rects[i].height;
        if (bottom > content_height) {
            content_height = bottom;
        }
    }
    if (content_height < bounds.height) {
        content_height = bounds.height;
    }

    if (content_height > bounds.height) {
        max_scroll = content_height - bounds.height;
    }
    if (state->project_filter_scroll < 0.0f) {
        state->project_filter_scroll = 0.0f;
    } else if (state->project_filter_scroll > max_scroll) {
        state->project_filter_scroll = max_scroll;
    }

    if (wheel_y != 0 && kit_ui_point_in_rect(bounds, input->mouse_x, input->mouse_y)) {
        KitUiScrollResult scroll_result = kit_ui_eval_scroll(bounds,
                                                             state->project_filter_scroll,
                                                             content_height,
                                                             (float)wheel_y);
        if (scroll_result.changed) {
            state->project_filter_scroll = scroll_result.offset_y;
        }
    }

    {
        CoreResult clip_result = kit_ui_clip_push(ui_ctx, frame, bounds);
        if (clip_result.code != CORE_OK) {
            return clip_result;
        }
    }

    for (i = 0; i < chip_count; ++i) {
        const char *project_key = layout_keys[i];
        KitRenderRect chip_rect = {
            bounds.x + layout_rects[i].x,
            bounds.y + layout_rects[i].y - state->project_filter_scroll,
            layout_rects[i].width,
            layout_rects[i].height
        };
        KitUiButtonResult button_result;

        if (chip_rect.y + chip_rect.height < bounds.y || chip_rect.y > (bounds.y + bounds.height)) {
            continue;
        }

        button_result = kit_ui_eval_button(chip_rect, input, input_enabled);
        if (!project_key || project_key[0] == '\0') {
            if (state->selected_project_count == 0) {
                button_result.state = KIT_UI_STATE_ACTIVE;
            }
            if (button_result.clicked) {
                mem_console_project_filter_clear(state);
                changed = 1;
            }
        } else {
            if (mem_console_project_filter_is_selected(state, project_key)) {
                button_result.state = KIT_UI_STATE_ACTIVE;
            }
            if (button_result.clicked && mem_console_project_filter_toggle(state, project_key)) {
                changed = 1;
            }
        }

        {
            CoreResult draw_result = mem_console_ui_draw_button_custom(ui_ctx,
                                                                        frame,
                                                                        chip_rect,
                                                                        layout_labels[i],
                                                                        button_result.state,
                                                                        CORE_FONT_ROLE_UI_REGULAR,
                                                                        CORE_FONT_TEXT_SIZE_CAPTION);
            if (draw_result.code != CORE_OK) {
                (void)kit_ui_clip_pop(ui_ctx, frame);
                return draw_result;
            }
        }
    }

    {
        CoreResult clip_result = kit_ui_clip_pop(ui_ctx, frame);
        if (clip_result.code != CORE_OK) {
            return clip_result;
        }
    }

    {
        CoreResult draw_result = kit_ui_draw_scrollbar(ui_ctx,
                                                       frame,
                                                       bounds,
                                                       state->project_filter_scroll,
                                                       content_height);
        if (draw_result.code != CORE_OK) {
            return draw_result;
        }
    }

    *out_changed = changed;
    return core_result_ok();
}
