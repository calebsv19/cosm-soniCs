#include "mem_console_ui.h"
#include "mem_console_ui_common.h"
#include "mem_console_ui_detail_panel.h"
#include "mem_console_ui_graph.h"
#include "mem_console_ui_left_panel.h"

#include <SDL2/SDL.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define ui_estimate_char_width_px mem_console_ui_estimate_char_width_px
#define ui_clamp_cursor_for_text mem_console_ui_clamp_cursor_for_text
#define ui_measure_text_width_px mem_console_ui_measure_text_width_px
#define ui_cursor_index_for_click mem_console_ui_cursor_index_for_click
#define draw_info_line_custom mem_console_ui_draw_info_line_custom
#define draw_button_custom mem_console_ui_draw_button_custom
#define draw_editable_line mem_console_ui_draw_editable_line
#define resolve_theme_color mem_console_ui_resolve_theme_color
#define push_themed_rect mem_console_ui_push_themed_rect
#define draw_wrapped_text_block mem_console_ui_draw_wrapped_text_block
#define handle_graph_viewport_interaction mem_console_ui_graph_handle_viewport_interaction
#define ensure_graph_preview_layout_cache mem_console_ui_graph_ensure_layout_cache
#define draw_graph_preview mem_console_ui_graph_draw_preview

static const char *k_graph_kind_filter_labels[] = {
    "ALL",
    "SUPPORTS",
    "DEPENDS",
    "REFS",
    "SUMMARY",
    "RELATED"
};

static const char *k_graph_kind_filter_values[] = {
    "",
    "supports",
    "depends_on",
    "references",
    "summarizes",
    "related"
};

static const char *k_graph_hops_labels[] = {
    "1",
    "2",
    "3",
    "4",
    "5"
};


static int graph_kind_filter_index(const MemConsoleState *state) {
    int i;

    if (!state) {
        return 0;
    }
    for (i = 0; i < (int)(sizeof(k_graph_kind_filter_values) / sizeof(k_graph_kind_filter_values[0])); ++i) {
        if (strcmp(state->graph_kind_filter, k_graph_kind_filter_values[i]) == 0) {
            return i;
        }
    }
    return 0;
}

static int find_node_index_at_point(const KitGraphStructNodeLayout *layouts,
                                    uint32_t node_count,
                                    float x,
                                    float y,
                                    uint32_t *out_index) {
    uint32_t i;

    if (!layouts || !out_index) {
        return 0;
    }
    for (i = 0u; i < node_count; ++i) {
        if (kit_ui_point_in_rect(layouts[i].rect, x, y)) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

static int select_neighbor_from_edge_click(const MemConsoleState *state,
                                           const KitGraphStructEdge *edges,
                                           uint32_t edge_count,
                                           const KitGraphStructEdgeRoute *routes,
                                           const KitGraphStructEdgeLabelLayout *label_layouts,
                                           uint32_t label_count,
                                           float mouse_x,
                                           float mouse_y,
                                           int64_t *out_item_id) {
    (void)edges;
    (void)edge_count;
    (void)routes;
    (void)label_layouts;
    (void)label_count;
    return mem_console_ui_graph_select_neighbor_from_edge_click(state, mouse_x, mouse_y, out_item_id);
}

static int handle_graph_node_click(MemConsoleState *state,
                                   int64_t hit_item_id,
                                   MemConsoleAction *out_action) {
    uint64_t now_ms;
    int is_double_click = 0;

    if (!state || !out_action || hit_item_id == 0) {
        return 0;
    }

    now_ms = SDL_GetTicks64();
    if (state->graph_last_click_item_id == hit_item_id &&
        now_ms >= state->graph_last_click_ms &&
        (now_ms - state->graph_last_click_ms) <= 300u) {
        is_double_click = 1;
    }

    state->graph_last_click_item_id = hit_item_id;
    state->graph_last_click_ms = now_ms;

    if (is_double_click) {
        state->selected_item_id = hit_item_id;
        kit_graph_struct_viewport_default(&state->graph_viewport);
        state->graph_layout_valid = 0;
        if (*out_action == MEM_CONSOLE_ACTION_NONE) {
            *out_action = MEM_CONSOLE_ACTION_REFRESH;
        }
        return 1;
    }

    if (hit_item_id != state->selected_item_id) {
        state->selected_item_id = hit_item_id;
        if (*out_action == MEM_CONSOLE_ACTION_NONE) {
            *out_action = MEM_CONSOLE_ACTION_REFRESH;
        }
        return 1;
    }

    return 0;
}

int run_frame(KitRenderContext *render_ctx,
              KitUiContext *ui_ctx,
              MemConsoleState *state,
              const KitUiInputState *input,
              int frame_width,
              int frame_height,
              int wheel_y,
              MemConsoleAction *out_action) {
    KitRenderCommand commands[4096];
    KitRenderCommandBuffer command_buffer;
    KitRenderFrame frame;
    KitUiStackLayout left_layout;
    KitUiStackLayout right_layout;
    KitRenderRect row;
    KitRenderRect search_box;
    KitRenderRect project_filter_box;
    KitRenderRect list_header;
    KitRenderRect list_viewport;
    KitRenderRect item_rect;
    KitRenderRect body_panel;
    KitRenderRect graph_panel;
    KitRenderRect detail_title_row;
    KitRenderRect graph_filter_bar;
    KitRenderRect graph_settings_row;
    KitRenderRect graph_edge_label_rect;
    KitRenderRect graph_edge_input_rect;
    KitRenderRect graph_edge_apply_rect;
    KitRenderRect graph_hops_label_rect;
    KitRenderRect graph_hops_rect;
    KitRenderRect action_bar;
    KitRenderRect action_button_rect;
    KitRenderRect detail_meta_left;
    KitRenderRect detail_meta_right;
    CoreResult result;
    KitRenderColor clear_color;
    KitUiButtonResult button_result;
    int draw_width;
    int draw_height;
    int loaded_end_index;
    int matching_rows;
    int first_visible_index;
    int desired_query_offset;
    int has_any_edit_mode;
    int action_index;
    int graph_kind_index;
    int graph_hovered_index;
    int graph_hops_index;
    int graph_hops_hovered_index;
    int i;
    int search_input_active;
    int title_input_active;
    int graph_edge_input_active;
    int project_filters_changed;
    int suppress_graph_click_on_release;
    const char *search_display_text;
    float content_height;
    float max_scroll;
    float row_pitch;
    char db_summary_draw[384];
    char detail_title_draw[192];
    char connection_summary_lines[6][256];
    char runtime_summary_draw[128];
    char status_line_draw[160];
    char connection_summary_text[640];

    if (!render_ctx || !ui_ctx || !state || !input || !out_action) {
        return 1;
    }

    *out_action = MEM_CONSOLE_ACTION_NONE;
    search_input_active = state->input_target == MEM_CONSOLE_INPUT_SEARCH;
    title_input_active = state->input_target == MEM_CONSOLE_INPUT_TITLE_EDIT;
    graph_edge_input_active = state->input_target == MEM_CONSOLE_INPUT_GRAPH_EDGE_LIMIT;
    has_any_edit_mode = state->title_edit_mode || state->body_edit_mode;
    project_filters_changed = 0;
    suppress_graph_click_on_release = 0;
    draw_width = frame_width;
    draw_height = frame_height;
    if (draw_width < 1100) {
        draw_width = 1100;
    }
    if (draw_height < 760) {
        draw_height = 760;
    }

    compute_layout(state, draw_width, draw_height);

    command_buffer.commands = commands;
    command_buffer.capacity = 4096u;
    command_buffer.count = 0u;
    kit_ui_clip_stack_reset(ui_ctx);

    result = kit_render_begin_frame(render_ctx,
                                    (uint32_t)draw_width,
                                    (uint32_t)draw_height,
                                    &command_buffer,
                                    &frame);
    if (result.code != CORE_OK) {
        fprintf(stderr, "mem_console: kit_render_begin_frame failed: %d\n", (int)result.code);
        return 1;
    }

    result = resolve_theme_color(render_ctx, CORE_THEME_COLOR_SURFACE_0, &clear_color);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = kit_render_push_clear(&frame, clear_color);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = push_themed_rect(render_ctx,
                              &frame,
                              state->left_pane,
                              12.0f,
                              CORE_THEME_COLOR_SURFACE_1);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = push_themed_rect(render_ctx,
                              &frame,
                              state->right_pane,
                              12.0f,
                              CORE_THEME_COLOR_SURFACE_0);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_begin(&left_layout,
                                KIT_UI_AXIS_VERTICAL,
                                (KitRenderRect){
                                    state->left_pane.x + 18.0f,
                                    state->left_pane.y + 18.0f,
                                    state->left_pane.width - 36.0f,
                                    state->left_pane.height - 36.0f
                                },
                                ui_ctx->style.gap);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&left_layout, 28.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   "MEMORY CONSOLE",
                                   CORE_THEME_COLOR_TEXT_PRIMARY,
                                   CORE_FONT_ROLE_UI_BOLD,
                                   CORE_FONT_TEXT_SIZE_TITLE);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&left_layout, 22.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    (void)snprintf(state->db_summary_line, sizeof(state->db_summary_line), "DB: %s", state->db_path);
    format_text_for_width(db_summary_draw,
                          sizeof(db_summary_draw),
                          state->db_summary_line,
                          row.width - (ui_ctx->style.padding * 2.0f),
                          CORE_FONT_TEXT_SIZE_CAPTION);
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   db_summary_draw,
                                   CORE_THEME_COLOR_TEXT_MUTED,
                                   CORE_FONT_ROLE_UI_REGULAR,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&left_layout, 22.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    (void)snprintf(state->theme_summary_line,
                   sizeof(state->theme_summary_line),
                   "Theme %s | Font %s",
                   state->theme_name[0] ? state->theme_name : "unknown",
                   state->font_name[0] ? state->font_name : "unknown");
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   state->theme_summary_line,
                                   CORE_THEME_COLOR_TEXT_MUTED,
                                   CORE_FONT_ROLE_UI_REGULAR,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&left_layout, 22.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    (void)snprintf(state->schema_summary_line,
                   sizeof(state->schema_summary_line),
                   "Schema v%s  |  Active %lld",
                   state->schema_version[0] ? state->schema_version : "?",
                   (long long)state->active_count);
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   state->schema_summary_line,
                                   CORE_THEME_COLOR_TEXT_MUTED,
                                   CORE_FONT_ROLE_UI_REGULAR,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&left_layout, 22.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    format_text_for_width(runtime_summary_draw,
                          sizeof(runtime_summary_draw),
                          state->runtime_summary_line,
                          row.width - (ui_ctx->style.padding * 2.0f),
                          CORE_FONT_TEXT_SIZE_CAPTION);
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   runtime_summary_draw,
                                   CORE_THEME_COLOR_TEXT_MUTED,
                                   CORE_FONT_ROLE_UI_REGULAR,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&left_layout, 22.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    format_text_for_width(runtime_summary_draw,
                          sizeof(runtime_summary_draw),
                          state->kernel_summary_line,
                          row.width - (ui_ctx->style.padding * 2.0f),
                          CORE_FONT_TEXT_SIZE_CAPTION);
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   runtime_summary_draw,
                                   CORE_THEME_COLOR_TEXT_MUTED,
                                   CORE_FONT_ROLE_UI_REGULAR,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&left_layout, ui_ctx->style.control_height, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    button_result = kit_ui_eval_button(row, input, 1);
    if (button_result.clicked) {
        *out_action = MEM_CONSOLE_ACTION_REFRESH;
    }
    result = draw_button_custom(ui_ctx,
                                &frame,
                                row,
                                "RELOAD",
                                button_result.state,
                                CORE_FONT_ROLE_UI_MEDIUM,
                                CORE_FONT_TEXT_SIZE_PARAGRAPH);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&left_layout, 20.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   "SEARCH FILTER",
                                   CORE_THEME_COLOR_TEXT_PRIMARY,
                                   CORE_FONT_ROLE_UI_MEDIUM,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&left_layout, 34.0f, 0.0f, &search_box);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = push_themed_rect(render_ctx,
                              &frame,
                              search_box,
                              8.0f,
                              CORE_THEME_COLOR_SURFACE_2);
    if (result.code != CORE_OK) {
        return 1;
    }
    if (state->search_text[0]) {
        search_display_text = state->search_text;
    } else if (search_input_active) {
        search_display_text = "";
    } else {
        search_display_text = "ALL ACTIVE MEMORIES";
    }
    result = draw_editable_line(ui_ctx,
                                render_ctx,
                                &frame,
                                (KitRenderRect){search_box.x + 10.0f, search_box.y + 7.0f, search_box.width - 20.0f, 20.0f},
                                search_display_text,
                                state->search_text[0] ? CORE_THEME_COLOR_TEXT_PRIMARY : CORE_THEME_COLOR_TEXT_MUTED,
                                CORE_FONT_ROLE_UI_REGULAR,
                                CORE_FONT_TEXT_SIZE_PARAGRAPH,
                                search_input_active,
                                state->search_cursor);
    if (result.code != CORE_OK) {
        return 1;
    }
    if (input->mouse_released &&
        !(state->title_edit_mode || state->body_edit_mode) &&
        kit_ui_point_in_rect(search_box, input->mouse_x, input->mouse_y)) {
        float text_origin_x = search_box.x + 10.0f + ui_ctx->style.padding;
        state->input_target = MEM_CONSOLE_INPUT_SEARCH;
        state->search_cursor = ui_cursor_index_for_click(state->search_text,
                                                         render_ctx,
                                                         input->mouse_x,
                                                         text_origin_x,
                                                         CORE_FONT_ROLE_UI_REGULAR,
                                                         CORE_FONT_TEXT_SIZE_PARAGRAPH);
    }

    result = kit_ui_stack_next(&left_layout, 18.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   state->project_filter_summary_line,
                                   CORE_THEME_COLOR_TEXT_MUTED,
                                   CORE_FONT_ROLE_UI_REGULAR,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&left_layout, 84.0f, 0.0f, &project_filter_box);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = push_themed_rect(render_ctx,
                              &frame,
                              project_filter_box,
                              8.0f,
                              CORE_THEME_COLOR_SURFACE_0);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = mem_console_ui_left_draw_project_filter_chips(ui_ctx,
                                                           render_ctx,
                                                           &frame,
                                                           state,
                                                           input,
                                                           (KitRenderRect){
                                                               project_filter_box.x + 6.0f,
                                                               project_filter_box.y + 5.0f,
                                                               project_filter_box.width - 12.0f,
                                                               project_filter_box.height - 10.0f
                                                           },
                                                           wheel_y,
                                                           !has_any_edit_mode,
                                                           &project_filters_changed);
    if (result.code != CORE_OK) {
        return 1;
    }
    if (project_filters_changed) {
        state->list_scroll = 0.0f;
        state->list_query_offset = 0;
        state->search_refresh_pending = 0;
        if (*out_action == MEM_CONSOLE_ACTION_NONE) {
            *out_action = MEM_CONSOLE_ACTION_REFRESH;
        }
    }

    result = kit_ui_stack_next(&left_layout, 18.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    (void)snprintf(state->visible_summary_line,
                   sizeof(state->visible_summary_line),
                   "%d loaded | %lld matching",
                   state->visible_count,
                   (long long)state->matching_count);
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   state->visible_summary_line,
                                   CORE_THEME_COLOR_TEXT_MUTED,
                                   CORE_FONT_ROLE_UI_REGULAR,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&left_layout, 20.0f, 0.0f, &list_header);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   list_header,
                                   "RESULTS",
                                   CORE_THEME_COLOR_TEXT_PRIMARY,
                                   CORE_FONT_ROLE_UI_MEDIUM,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    list_viewport = (KitRenderRect){
        left_layout.bounds.x,
        left_layout.bounds.y + left_layout.cursor,
        left_layout.bounds.width,
        left_layout.bounds.y + left_layout.bounds.height - (left_layout.bounds.y + left_layout.cursor) - 26.0f
    };
    if (list_viewport.height < 0.0f) {
        list_viewport.height = 0.0f;
    }

    row_pitch = (float)MEM_CONSOLE_LIST_ROW_PITCH_PX;
    if (row_pitch <= 0.0f) {
        row_pitch = 44.0f;
    }
    if (state->matching_count > (int64_t)INT_MAX) {
        matching_rows = INT_MAX;
    } else if (state->matching_count < 0) {
        matching_rows = 0;
    } else {
        matching_rows = (int)state->matching_count;
    }
    content_height = kit_ui_scroll_content_height_top_anchor(matching_rows,
                                                             row_pitch,
                                                             list_viewport.height);
    max_scroll = 0.0f;
    if (content_height > list_viewport.height) {
        max_scroll = content_height - list_viewport.height;
    }
    if (state->list_scroll < 0.0f) {
        state->list_scroll = 0.0f;
    } else if (state->list_scroll > max_scroll) {
        state->list_scroll = max_scroll;
    }

    if (wheel_y != 0 && kit_ui_point_in_rect(list_viewport, input->mouse_x, input->mouse_y)) {
        KitUiScrollResult scroll_result = kit_ui_eval_scroll(list_viewport,
                                                             state->list_scroll,
                                                             content_height,
                                                             (float)wheel_y);
        if (scroll_result.changed) {
            state->list_scroll = scroll_result.offset_y;
        }
    }

    if (matching_rows > 0) {
        first_visible_index = (int)(state->list_scroll / row_pitch);
        if (first_visible_index < 0) {
            first_visible_index = 0;
        }
        if (first_visible_index >= matching_rows) {
            first_visible_index = matching_rows - 1;
        }

        desired_query_offset = first_visible_index - 2;
        if (desired_query_offset < 0) {
            desired_query_offset = 0;
        }

        loaded_end_index = state->visible_start_index + state->visible_count;
        if (state->visible_count == 0 ||
            desired_query_offset < state->visible_start_index ||
            desired_query_offset >= loaded_end_index) {
            state->list_query_offset = desired_query_offset;
            if (*out_action == MEM_CONSOLE_ACTION_NONE) {
                *out_action = MEM_CONSOLE_ACTION_REFRESH;
            }
        }
    } else {
        state->list_query_offset = 0;
    }

    result = push_themed_rect(render_ctx,
                              &frame,
                              list_viewport,
                              8.0f,
                              CORE_THEME_COLOR_SURFACE_0);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_clip_push(ui_ctx, &frame, list_viewport);
    if (result.code != CORE_OK) {
        return 1;
    }

    if (state->visible_count == 0) {
        const char *empty_text = "NO MATCHES FOR THE CURRENT FILTER";
        if (state->matching_count > 0) {
            empty_text = "LOADING LIST WINDOW...";
        }
        result = draw_info_line_custom(ui_ctx,
                                       &frame,
                                       (KitRenderRect){
                                           list_viewport.x + 10.0f,
                                           list_viewport.y + 10.0f,
                                           list_viewport.width - 20.0f,
                                           22.0f
                                       },
                                       empty_text,
                                       CORE_THEME_COLOR_TEXT_MUTED,
                                       CORE_FONT_ROLE_UI_REGULAR,
                                       CORE_FONT_TEXT_SIZE_CAPTION);
        if (result.code != CORE_OK) {
            (void)kit_ui_clip_pop(ui_ctx, &frame);
            return 1;
        }
    } else {
        for (i = 0; i < state->visible_count; ++i) {
            int row_index = state->visible_start_index + i;
            float viewport_bottom = list_viewport.y + list_viewport.height;

            item_rect = (KitRenderRect){
                list_viewport.x + 8.0f,
                list_viewport.y + 8.0f + ((float)row_index * row_pitch) - state->list_scroll,
                list_viewport.width - 24.0f,
                36.0f
            };
            if (item_rect.y + item_rect.height < list_viewport.y || item_rect.y > viewport_bottom) {
                continue;
            }

            {
                char raw_label[220];
                (void)snprintf(raw_label,
                               sizeof(raw_label),
                               "%lld %s%s%s%s%s%s",
                               (long long)state->visible_items[i].id,
                               state->visible_items[i].pinned ? "[P] " : "",
                               state->visible_items[i].canonical ? "[C] " : "",
                               state->visible_items[i].project_key[0] ? "[" : "",
                               state->visible_items[i].project_key[0] ? state->visible_items[i].project_key : "",
                               state->visible_items[i].project_key[0] ? "] " : "",
                               state->visible_items[i].title[0] ? state->visible_items[i].title : "UNTITLED");
                format_text_for_width(state->list_item_labels[i],
                                      sizeof(state->list_item_labels[i]),
                                      raw_label,
                                      item_rect.width - (ui_ctx->style.padding * 2.0f),
                                      CORE_FONT_TEXT_SIZE_CAPTION);
            }

            button_result = kit_ui_eval_button(item_rect,
                                               input,
                                               !(state->title_edit_mode || state->body_edit_mode));
            if (button_result.clicked) {
                state->selected_item_id = state->visible_items[i].id;
                *out_action = MEM_CONSOLE_ACTION_REFRESH;
            }
            if (state->visible_items[i].id == state->selected_item_id) {
                button_result.state = KIT_UI_STATE_ACTIVE;
            }

            result = draw_button_custom(ui_ctx,
                                        &frame,
                                        item_rect,
                                        state->list_item_labels[i],
                                        button_result.state,
                                        CORE_FONT_ROLE_UI_REGULAR,
                                        CORE_FONT_TEXT_SIZE_CAPTION);
            if (result.code != CORE_OK) {
                (void)kit_ui_clip_pop(ui_ctx, &frame);
                return 1;
            }
        }
    }

    result = kit_ui_clip_pop(ui_ctx, &frame);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_draw_scrollbar(ui_ctx,
                                   &frame,
                                   list_viewport,
                                   state->list_scroll,
                                   content_height);
    if (result.code != CORE_OK) {
        return 1;
    }

    format_text_for_width(status_line_draw,
                          sizeof(status_line_draw),
                          state->status_line,
                          left_layout.bounds.width - (ui_ctx->style.padding * 2.0f),
                          CORE_FONT_TEXT_SIZE_CAPTION);
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   (KitRenderRect){
                                       left_layout.bounds.x,
                                       state->left_pane.y + state->left_pane.height - 24.0f,
                                       left_layout.bounds.width,
                                       20.0f
                                   },
                                   status_line_draw,
                                   CORE_THEME_COLOR_TEXT_MUTED,
                                   CORE_FONT_ROLE_UI_REGULAR,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_begin(&right_layout,
                                KIT_UI_AXIS_VERTICAL,
                                (KitRenderRect){
                                    state->right_pane.x + 18.0f,
                                    state->right_pane.y + 18.0f,
                                    state->right_pane.width - 36.0f,
                                    state->right_pane.height - 36.0f
                                },
                                ui_ctx->style.gap);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&right_layout, 28.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   "DETAIL",
                                   CORE_THEME_COLOR_TEXT_PRIMARY,
                                   CORE_FONT_ROLE_UI_BOLD,
                                   CORE_FONT_TEXT_SIZE_TITLE);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&right_layout, 22.0f, 0.0f, &detail_title_row);
    if (result.code != CORE_OK) {
        return 1;
    }
    if (state->title_edit_mode) {
        result = draw_editable_line(ui_ctx,
                                    render_ctx,
                                    &frame,
                                    detail_title_row,
                                    state->title_edit_text,
                                    CORE_THEME_COLOR_TEXT_PRIMARY,
                                    CORE_FONT_ROLE_UI_BOLD,
                                    CORE_FONT_TEXT_SIZE_PARAGRAPH,
                                    title_input_active,
                                    state->title_edit_cursor);
        if (result.code != CORE_OK) {
            return 1;
        }
        if (input->mouse_released &&
            kit_ui_point_in_rect(detail_title_row, input->mouse_x, input->mouse_y)) {
            float text_origin_x = detail_title_row.x + ui_ctx->style.padding;
            state->input_target = MEM_CONSOLE_INPUT_TITLE_EDIT;
            state->title_edit_cursor = ui_cursor_index_for_click(state->title_edit_text,
                                                                 render_ctx,
                                                                 input->mouse_x,
                                                                 text_origin_x,
                                                                 CORE_FONT_ROLE_UI_BOLD,
                                                                 CORE_FONT_TEXT_SIZE_PARAGRAPH);
        }
    } else {
        format_text_for_width(detail_title_draw,
                              sizeof(detail_title_draw),
                              state->selected_title,
                              detail_title_row.width - (ui_ctx->style.padding * 2.0f),
                              CORE_FONT_TEXT_SIZE_PARAGRAPH);
        result = draw_info_line_custom(ui_ctx,
                                       &frame,
                                       detail_title_row,
                                       detail_title_draw,
                                       CORE_THEME_COLOR_TEXT_PRIMARY,
                                       CORE_FONT_ROLE_UI_BOLD,
                                       CORE_FONT_TEXT_SIZE_PARAGRAPH);
    }
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&right_layout, 62.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    mem_console_ui_detail_build_connection_summary(state,
                                                   connection_summary_text,
                                                   sizeof(connection_summary_text));
    detail_meta_left = row;
    detail_meta_left.width = row.width * 0.36f;
    if (detail_meta_left.width < 180.0f) {
        detail_meta_left.width = 180.0f;
    }
    if (detail_meta_left.width > row.width - 120.0f) {
        detail_meta_left.width = row.width - 120.0f;
    }
    detail_meta_right = row;
    detail_meta_right.x = detail_meta_left.x + detail_meta_left.width + 8.0f;
    detail_meta_right.width = row.width - detail_meta_left.width - 8.0f;
    if (detail_meta_right.width < 100.0f) {
        detail_meta_right.width = 100.0f;
    }

    if (state->selected_item_id != 0) {
        (void)snprintf(state->detail_meta_line,
                       sizeof(state->detail_meta_line),
                       "MEMORY ID %lld",
                       (long long)state->selected_item_id);
    } else {
        (void)snprintf(state->detail_meta_line,
                       sizeof(state->detail_meta_line),
                       "SELECT A MEMORY TO EDIT");
    }
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   (KitRenderRect){
                                       detail_meta_left.x,
                                       detail_meta_left.y + 4.0f,
                                       detail_meta_left.width,
                                       20.0f
                                   },
                                   state->detail_meta_line,
                                   CORE_THEME_COLOR_TEXT_MUTED,
                                   CORE_FONT_ROLE_UI_REGULAR,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = push_themed_rect(render_ctx,
                              &frame,
                              detail_meta_right,
                              8.0f,
                              CORE_THEME_COLOR_SURFACE_1);
    if (result.code != CORE_OK) {
        return 1;
    }
    {
        KitRenderRect summary_content = {
            detail_meta_right.x + 6.0f,
            detail_meta_right.y + 6.0f,
            detail_meta_right.width - 12.0f,
            detail_meta_right.height - 12.0f
        };
        result = push_themed_rect(render_ctx,
                                  &frame,
                                  summary_content,
                                  6.0f,
                                  CORE_THEME_COLOR_SURFACE_0);
        if (result.code != CORE_OK) {
            return 1;
        }
        result = draw_wrapped_text_block(ui_ctx,
                                         &frame,
                                         connection_summary_lines,
                                         6,
                                         summary_content,
                                         connection_summary_text,
                                         CORE_THEME_COLOR_TEXT_MUTED,
                                         CORE_FONT_TEXT_SIZE_CAPTION,
                                         4);
        if (result.code != CORE_OK) {
            return 1;
        }
    }

    result = kit_ui_stack_next(&right_layout, 20.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   "BODY",
                                   CORE_THEME_COLOR_TEXT_PRIMARY,
                                   CORE_FONT_ROLE_UI_MEDIUM,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&right_layout, 128.0f, 0.0f, &body_panel);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = push_themed_rect(render_ctx,
                              &frame,
                              body_panel,
                              10.0f,
                              CORE_THEME_COLOR_SURFACE_1);
    if (result.code != CORE_OK) {
        return 1;
    }
    {
        KitRenderRect body_content = {
            body_panel.x + 6.0f,
            body_panel.y + 6.0f,
            body_panel.width - 12.0f,
            body_panel.height - 12.0f
        };
        result = push_themed_rect(render_ctx,
                                  &frame,
                                  body_content,
                                  8.0f,
                                  CORE_THEME_COLOR_SURFACE_0);
        if (result.code != CORE_OK) {
            return 1;
        }
        result = draw_wrapped_text_block(ui_ctx,
                                         &frame,
                                         state->wrapped_body_lines,
                                         6,
                                         body_content,
                                         state->body_edit_mode ? state->body_edit_text : state->selected_body,
                                         CORE_THEME_COLOR_TEXT_MUTED,
                                         CORE_FONT_TEXT_SIZE_BASIC,
                                         6);
        if (result.code != CORE_OK) {
            return 1;
        }
        if (state->body_edit_mode) {
            KitRenderLineCommand caret_line;
            KitRenderColor caret_color;
            int char_w = ui_estimate_char_width_px(CORE_FONT_TEXT_SIZE_BASIC);
            int body_len = (int)strlen(state->body_edit_text);
            int cursor = ui_clamp_cursor_for_text(state->body_edit_text, state->body_edit_cursor);
            int line_capacity = (int)((body_content.width - 16.0f) / (float)char_w);
            int line_index;
            int line_start_idx;
            int line_prefix_len;
            int i;
            char line_prefix[256];
            float caret_x;
            float caret_y0;

            if (char_w < 1) {
                char_w = 8;
            }
            if (line_capacity < 1) line_capacity = 1;
            line_index = cursor / line_capacity;
            if (cursor >= body_len && body_len > 0 && body_len % line_capacity == 0) {
                line_index = body_len / line_capacity;
            }

            line_start_idx = line_index * line_capacity;
            if (line_start_idx < 0) {
                line_start_idx = 0;
            }
            if (line_start_idx > body_len) {
                line_start_idx = body_len;
            }
            line_prefix_len = cursor - line_start_idx;
            if (line_prefix_len < 0) {
                line_prefix_len = 0;
            }
            if (line_prefix_len > (int)sizeof(line_prefix) - 1) {
                line_prefix_len = (int)sizeof(line_prefix) - 1;
            }
            for (i = 0; i < line_prefix_len; ++i) {
                line_prefix[i] = state->body_edit_text[line_start_idx + i];
            }
            line_prefix[line_prefix_len] = '\0';

            caret_x = body_content.x + 8.0f +
                      ui_measure_text_width_px(render_ctx,
                                               CORE_FONT_ROLE_UI_REGULAR,
                                               CORE_FONT_TEXT_SIZE_BASIC,
                                               line_prefix);
            caret_y0 = body_content.y + 8.0f + ((float)line_index * 24.0f);

            if (caret_x > body_content.x + body_content.width - 8.0f) {
                caret_x = body_content.x + body_content.width - 8.0f;
            }
            if (caret_y0 > body_content.y + body_content.height - 20.0f) {
                caret_y0 = body_content.y + body_content.height - 20.0f;
            }

            if (input->mouse_released && kit_ui_point_in_rect(body_content, input->mouse_x, input->mouse_y)) {
                float text_x = body_content.x + 8.0f;
                float text_y = body_content.y + 8.0f;
                int click_row = (int)((input->mouse_y - text_y + 12.0f) / 24.0f);
                int candidate_cursor;
                int line_end_idx;
                float delta_x;
                float advance = 0.0f;
                char glyph[2];

                if (click_row < 0) click_row = 0;
                line_start_idx = click_row * line_capacity;
                if (line_start_idx < 0) {
                    line_start_idx = 0;
                }
                if (line_start_idx > body_len) {
                    line_start_idx = body_len;
                }

                line_end_idx = line_start_idx + line_capacity;
                if (line_end_idx > body_len) {
                    line_end_idx = body_len;
                }

                delta_x = input->mouse_x - text_x;
                if (delta_x <= 0.0f) {
                    candidate_cursor = line_start_idx;
                } else {
                    candidate_cursor = line_end_idx;
                    glyph[1] = '\0';
                    for (i = line_start_idx; i < line_end_idx; ++i) {
                        float glyph_w;
                        glyph[0] = state->body_edit_text[i];
                        glyph_w = ui_measure_text_width_px(render_ctx,
                                                           CORE_FONT_ROLE_UI_REGULAR,
                                                           CORE_FONT_TEXT_SIZE_BASIC,
                                                           glyph);
                        if (glyph_w <= 0.0f) {
                            glyph_w = (float)char_w;
                        }
                        if (delta_x < advance + (glyph_w * 0.5f)) {
                            candidate_cursor = i;
                            break;
                        }
                        advance += glyph_w;
                    }
                }
                if (candidate_cursor < 0) candidate_cursor = 0;
                if (candidate_cursor > body_len) candidate_cursor = body_len;

                state->input_target = MEM_CONSOLE_INPUT_BODY_EDIT;
                state->body_edit_cursor = candidate_cursor;
                cursor = candidate_cursor;
                line_index = cursor / line_capacity;
                line_start_idx = line_index * line_capacity;
                if (line_start_idx < 0) {
                    line_start_idx = 0;
                }
                if (line_start_idx > body_len) {
                    line_start_idx = body_len;
                }
                line_prefix_len = cursor - line_start_idx;
                if (line_prefix_len < 0) {
                    line_prefix_len = 0;
                }
                if (line_prefix_len > (int)sizeof(line_prefix) - 1) {
                    line_prefix_len = (int)sizeof(line_prefix) - 1;
                }
                for (i = 0; i < line_prefix_len; ++i) {
                    line_prefix[i] = state->body_edit_text[line_start_idx + i];
                }
                line_prefix[line_prefix_len] = '\0';
                caret_x = body_content.x + 8.0f +
                          ui_measure_text_width_px(render_ctx,
                                                   CORE_FONT_ROLE_UI_REGULAR,
                                                   CORE_FONT_TEXT_SIZE_BASIC,
                                                   line_prefix);
                caret_y0 = body_content.y + 8.0f + ((float)line_index * 24.0f);
            }

            result = resolve_theme_color(render_ctx, CORE_THEME_COLOR_ACCENT_PRIMARY, &caret_color);
            if (result.code != CORE_OK) {
                return 1;
            }

            result = kit_ui_clip_push(ui_ctx, &frame, body_content);
            if (result.code != CORE_OK) {
                return 1;
            }

            caret_line.p0.x = caret_x;
            caret_line.p0.y = caret_y0;
            caret_line.p1.x = caret_x;
            caret_line.p1.y = caret_y0 + 18.0f;
            caret_line.thickness = 1.0f;
            caret_line.color = caret_color;
            caret_line.transform = kit_render_identity_transform();
            result = kit_render_push_line(&frame, &caret_line);
            if (result.code != CORE_OK) {
                (void)kit_ui_clip_pop(ui_ctx, &frame);
                return 1;
            }

            result = kit_ui_clip_pop(ui_ctx, &frame);
            if (result.code != CORE_OK) {
                return 1;
            }
        }
    }
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&right_layout, 18.0f, 0.0f, &row);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   row,
                                   "GRAPH",
                                   CORE_THEME_COLOR_TEXT_PRIMARY,
                                   CORE_FONT_ROLE_UI_MEDIUM,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = kit_ui_stack_next(&right_layout, 28.0f, 0.0f, &graph_filter_bar);
    if (result.code != CORE_OK) {
        return 1;
    }
    graph_kind_index = graph_kind_filter_index(state);
    graph_hovered_index = -1;
    if (kit_ui_point_in_rect(graph_filter_bar, input->mouse_x, input->mouse_y)) {
        float seg_w = graph_filter_bar.width / (float)(sizeof(k_graph_kind_filter_labels) / sizeof(k_graph_kind_filter_labels[0]));
        if (seg_w > 0.0f) {
            graph_hovered_index = (int)((input->mouse_x - graph_filter_bar.x) / seg_w);
            if (graph_hovered_index < 0) {
                graph_hovered_index = 0;
            }
            if (graph_hovered_index >= (int)(sizeof(k_graph_kind_filter_labels) / sizeof(k_graph_kind_filter_labels[0]))) {
                graph_hovered_index = (int)(sizeof(k_graph_kind_filter_labels) / sizeof(k_graph_kind_filter_labels[0])) - 1;
            }
        }
    }
    {
        KitUiSegmentedResult segmented_result = kit_ui_eval_segmented(graph_filter_bar,
                                                                       input,
                                                                       1,
                                                                       (int)(sizeof(k_graph_kind_filter_labels) / sizeof(k_graph_kind_filter_labels[0])),
                                                                       graph_kind_index);
        result = kit_ui_draw_segmented(ui_ctx,
                                       &frame,
                                       graph_filter_bar,
                                       k_graph_kind_filter_labels,
                                       (int)(sizeof(k_graph_kind_filter_labels) / sizeof(k_graph_kind_filter_labels[0])),
                                       graph_kind_index,
                                       graph_hovered_index,
                                       1);
        if (result.code != CORE_OK) {
            return 1;
        }
        if (segmented_result.changed) {
            int selected = segmented_result.selected_index;
            if (selected < 0) {
                selected = 0;
            }
            if (selected >= (int)(sizeof(k_graph_kind_filter_values) / sizeof(k_graph_kind_filter_values[0]))) {
                selected = (int)(sizeof(k_graph_kind_filter_values) / sizeof(k_graph_kind_filter_values[0])) - 1;
            }
            (void)snprintf(state->graph_kind_filter,
                           sizeof(state->graph_kind_filter),
                           "%s",
                           k_graph_kind_filter_values[selected]);
            if (state->graph_mode_enabled && *out_action == MEM_CONSOLE_ACTION_NONE) {
                *out_action = MEM_CONSOLE_ACTION_REFRESH_GRAPH;
            }
        }
    }

    result = kit_ui_stack_next(&right_layout, 30.0f, 0.0f, &graph_settings_row);
    if (result.code != CORE_OK) {
        return 1;
    }
    graph_edge_label_rect = (KitRenderRect){
        graph_settings_row.x,
        graph_settings_row.y + 4.0f,
        118.0f,
        graph_settings_row.height - 8.0f
    };
    graph_edge_input_rect = (KitRenderRect){
        graph_edge_label_rect.x + graph_edge_label_rect.width + 8.0f,
        graph_settings_row.y + 2.0f,
        86.0f,
        graph_settings_row.height - 4.0f
    };
    graph_edge_apply_rect = (KitRenderRect){
        graph_edge_input_rect.x + graph_edge_input_rect.width + 8.0f,
        graph_settings_row.y + 2.0f,
        76.0f,
        graph_settings_row.height - 4.0f
    };
    graph_hops_label_rect = (KitRenderRect){
        graph_edge_apply_rect.x + graph_edge_apply_rect.width + 10.0f,
        graph_settings_row.y + 4.0f,
        44.0f,
        graph_settings_row.height - 8.0f
    };
    graph_hops_rect = (KitRenderRect){
        graph_hops_label_rect.x + graph_hops_label_rect.width + 8.0f,
        graph_settings_row.y + 2.0f,
        (graph_settings_row.x + graph_settings_row.width) -
            (graph_hops_label_rect.x + graph_hops_label_rect.width + 8.0f),
        graph_settings_row.height - 4.0f
    };
    if (graph_hops_rect.width < 180.0f) {
        graph_hops_rect.width = 180.0f;
    }

    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   graph_edge_label_rect,
                                   "EDGE LIMIT",
                                   CORE_THEME_COLOR_TEXT_MUTED,
                                   CORE_FONT_ROLE_UI_MEDIUM,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = push_themed_rect(render_ctx,
                              &frame,
                              graph_edge_input_rect,
                              6.0f,
                              CORE_THEME_COLOR_SURFACE_2);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = draw_editable_line(ui_ctx,
                                render_ctx,
                                &frame,
                                (KitRenderRect){
                                    graph_edge_input_rect.x + 8.0f,
                                    graph_edge_input_rect.y + 5.0f,
                                    graph_edge_input_rect.width - 16.0f,
                                    graph_edge_input_rect.height - 10.0f
                                },
                                state->graph_edge_limit_text[0] ? state->graph_edge_limit_text : "48",
                                CORE_THEME_COLOR_TEXT_PRIMARY,
                                CORE_FONT_ROLE_UI_REGULAR,
                                CORE_FONT_TEXT_SIZE_CAPTION,
                                graph_edge_input_active,
                                state->graph_edge_limit_cursor);
    if (result.code != CORE_OK) {
        return 1;
    }
    if (input->mouse_released &&
        !has_any_edit_mode &&
        kit_ui_point_in_rect(graph_edge_input_rect, input->mouse_x, input->mouse_y)) {
        float text_origin_x = graph_edge_input_rect.x + 8.0f + ui_ctx->style.padding;
        state->input_target = MEM_CONSOLE_INPUT_GRAPH_EDGE_LIMIT;
        state->graph_edge_limit_cursor = ui_cursor_index_for_click(state->graph_edge_limit_text,
                                                                   render_ctx,
                                                                   input->mouse_x,
                                                                   text_origin_x,
                                                                   CORE_FONT_ROLE_UI_REGULAR,
                                                                   CORE_FONT_TEXT_SIZE_CAPTION);
        graph_edge_input_active = 1;
    }

    button_result = kit_ui_eval_button(graph_edge_apply_rect, input, 1);
    if (button_result.clicked) {
        int parsed_limit = mem_console_graph_edge_limit_parse(state->graph_edge_limit_text,
                                                              state->graph_query_edge_limit);
        mem_console_graph_edge_limit_set(state, parsed_limit);
        if (state->graph_mode_enabled && *out_action == MEM_CONSOLE_ACTION_NONE) {
            *out_action = MEM_CONSOLE_ACTION_REFRESH_GRAPH;
        }
    }
    result = draw_button_custom(ui_ctx,
                                &frame,
                                graph_edge_apply_rect,
                                "APPLY",
                                button_result.state,
                                CORE_FONT_ROLE_UI_MEDIUM,
                                CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }

    result = draw_info_line_custom(ui_ctx,
                                   &frame,
                                   graph_hops_label_rect,
                                   "HOPS",
                                   CORE_THEME_COLOR_TEXT_MUTED,
                                   CORE_FONT_ROLE_UI_MEDIUM,
                                   CORE_FONT_TEXT_SIZE_CAPTION);
    if (result.code != CORE_OK) {
        return 1;
    }
    graph_hops_index = mem_console_graph_hops_clamp(state->graph_query_hops) - MEM_CONSOLE_GRAPH_HOPS_MIN;
    graph_hops_hovered_index = -1;
    if (kit_ui_point_in_rect(graph_hops_rect, input->mouse_x, input->mouse_y)) {
        float seg_w = graph_hops_rect.width / (float)(sizeof(k_graph_hops_labels) / sizeof(k_graph_hops_labels[0]));
        if (seg_w > 0.0f) {
            graph_hops_hovered_index = (int)((input->mouse_x - graph_hops_rect.x) / seg_w);
            if (graph_hops_hovered_index < 0) {
                graph_hops_hovered_index = 0;
            }
            if (graph_hops_hovered_index >= (int)(sizeof(k_graph_hops_labels) / sizeof(k_graph_hops_labels[0]))) {
                graph_hops_hovered_index = (int)(sizeof(k_graph_hops_labels) / sizeof(k_graph_hops_labels[0])) - 1;
            }
        }
    }
    {
        KitUiSegmentedResult hops_result = kit_ui_eval_segmented(graph_hops_rect,
                                                                  input,
                                                                  1,
                                                                  (int)(sizeof(k_graph_hops_labels) / sizeof(k_graph_hops_labels[0])),
                                                                  graph_hops_index);
        result = kit_ui_draw_segmented(ui_ctx,
                                       &frame,
                                       graph_hops_rect,
                                       k_graph_hops_labels,
                                       (int)(sizeof(k_graph_hops_labels) / sizeof(k_graph_hops_labels[0])),
                                       graph_hops_index,
                                       graph_hops_hovered_index,
                                       1);
        if (result.code != CORE_OK) {
            return 1;
        }
        if (hops_result.changed) {
            int selected = hops_result.selected_index;
            if (selected < 0) {
                selected = 0;
            }
            if (selected >= (int)(sizeof(k_graph_hops_labels) / sizeof(k_graph_hops_labels[0]))) {
                selected = (int)(sizeof(k_graph_hops_labels) / sizeof(k_graph_hops_labels[0])) - 1;
            }
            state->graph_query_hops = mem_console_graph_hops_clamp(MEM_CONSOLE_GRAPH_HOPS_MIN + selected);
            if (state->graph_mode_enabled && *out_action == MEM_CONSOLE_ACTION_NONE) {
                *out_action = MEM_CONSOLE_ACTION_REFRESH_GRAPH;
            }
        }
    }

    result = kit_ui_stack_next(&right_layout, 30.0f, 0.0f, &action_bar);
    if (result.code != CORE_OK) {
        return 1;
    }
    result = push_themed_rect(render_ctx,
                              &frame,
                              action_bar,
                              6.0f,
                              CORE_THEME_COLOR_SURFACE_1);
    if (result.code != CORE_OK) {
        return 1;
    }

    action_button_rect = (KitRenderRect){
        action_bar.x + 4.0f,
        action_bar.y + 3.0f,
        (action_bar.width - 12.0f) / 8.0f,
        action_bar.height - 6.0f
    };
    if (action_button_rect.width < 44.0f) {
        action_button_rect.width = 44.0f;
    }

    for (action_index = 0; action_index < 8; ++action_index) {
        const char *label = "";
        int enabled = 1;
        action_button_rect.x = action_bar.x + 4.0f + ((float)action_index * ((action_bar.width - 8.0f) / 8.0f));
        action_button_rect.width = ((action_bar.width - 8.0f) / 8.0f) - 2.0f;
        if (action_button_rect.width < 42.0f) {
            action_button_rect.width = 42.0f;
        }

        switch (action_index) {
            case 0:
                label = state->selected_pinned ? "PIN ON" : "PIN OFF";
                enabled = state->selected_item_id != 0;
                break;
            case 1:
                label = state->selected_canonical ? "CAN ON" : "CAN OFF";
                enabled = state->selected_item_id != 0;
                break;
            case 2:
                label = "NEW";
                enabled = 1;
                break;
            case 3:
                label = state->title_edit_mode ? "T SAVE" : "T EDIT";
                enabled = state->selected_item_id != 0;
                break;
            case 4:
                label = state->body_edit_mode ? "B SAVE" : "B EDIT";
                enabled = state->selected_item_id != 0;
                break;
            case 5:
                label = "CANCEL";
                enabled = has_any_edit_mode;
                break;
            case 6:
                label = state->graph_mode_enabled ? "GRAPH ON" : "GRAPH OFF";
                enabled = 1;
                break;
            case 7:
                label = "REFRESH";
                enabled = state->graph_mode_enabled;
                break;
            default:
                break;
        }

        button_result = kit_ui_eval_button(action_button_rect, input, enabled);
        if (button_result.clicked) {
            switch (action_index) {
                case 0:
                    *out_action = MEM_CONSOLE_ACTION_TOGGLE_PINNED;
                    break;
                case 1:
                    *out_action = MEM_CONSOLE_ACTION_TOGGLE_CANONICAL;
                    break;
                case 2:
                    *out_action = MEM_CONSOLE_ACTION_CREATE_FROM_SEARCH;
                    break;
                case 3:
                    *out_action = state->title_edit_mode
                                      ? MEM_CONSOLE_ACTION_SAVE_TITLE_EDIT
                                      : MEM_CONSOLE_ACTION_BEGIN_TITLE_EDIT;
                    break;
                case 4:
                    *out_action = state->body_edit_mode
                                      ? MEM_CONSOLE_ACTION_SAVE_BODY_EDIT
                                      : MEM_CONSOLE_ACTION_BEGIN_BODY_EDIT;
                    break;
                case 5:
                    if (state->title_edit_mode) {
                        *out_action = MEM_CONSOLE_ACTION_CANCEL_TITLE_EDIT;
                    } else if (state->body_edit_mode) {
                        *out_action = MEM_CONSOLE_ACTION_CANCEL_BODY_EDIT;
                    }
                    break;
                case 6:
                    *out_action = MEM_CONSOLE_ACTION_TOGGLE_GRAPH_MODE;
                    break;
                case 7:
                    *out_action = MEM_CONSOLE_ACTION_REFRESH_GRAPH;
                    break;
                default:
                    break;
            }
        }

        result = draw_button_custom(ui_ctx,
                                    &frame,
                                    action_button_rect,
                                    label,
                                    button_result.state,
                                    CORE_FONT_ROLE_UI_MEDIUM,
                                    CORE_FONT_TEXT_SIZE_CAPTION);
        if (result.code != CORE_OK) {
            return 1;
        }
    }

    if (state->graph_mode_enabled) {
        {
            float remaining = right_layout.bounds.height - right_layout.cursor;
            if (remaining < 220.0f) {
                remaining = 220.0f;
            }
            result = kit_ui_stack_next(&right_layout, remaining, 0.0f, &graph_panel);
            if (result.code != CORE_OK) {
                return 1;
            }
        }
        result = push_themed_rect(render_ctx,
                                  &frame,
                                  graph_panel,
                                  10.0f,
                                  CORE_THEME_COLOR_SURFACE_1);
        if (result.code != CORE_OK) {
            return 1;
        }

        {
            KitRenderRect graph_content = {
                graph_panel.x + 6.0f,
                graph_panel.y + 6.0f,
                graph_panel.width - 12.0f,
                graph_panel.height - 12.0f
            };
            KitRenderRect graph_bounds = {
                graph_content.x + 8.0f,
                graph_content.y + 8.0f,
                graph_content.width - 16.0f,
                graph_content.height - 16.0f
            };

            suppress_graph_click_on_release = handle_graph_viewport_interaction(state,
                                                                                 input,
                                                                                 wheel_y,
                                                                                 graph_bounds);

            result = push_themed_rect(render_ctx,
                                      &frame,
                                      graph_content,
                                      8.0f,
                                      CORE_THEME_COLOR_SURFACE_0);
            if (result.code != CORE_OK) {
                return 1;
            }

            result = kit_ui_clip_push(ui_ctx, &frame, graph_bounds);
            if (result.code != CORE_OK) {
                return 1;
            }

            result = draw_graph_preview(render_ctx,
                                        ui_ctx,
                                        input,
                                        &frame,
                                        graph_bounds,
                                        state);
            if (result.code != CORE_OK) {
                (void)kit_ui_clip_pop(ui_ctx, &frame);
                return 1;
            }

            result = kit_ui_clip_pop(ui_ctx, &frame);
            if (result.code != CORE_OK) {
                return 1;
            }
        }

        if (input->mouse_released && state->graph_click_armed && !suppress_graph_click_on_release) {
            KitRenderRect graph_content = {
                graph_panel.x + 6.0f,
                graph_panel.y + 6.0f,
                graph_panel.width - 12.0f,
                graph_panel.height - 12.0f
            };
            KitRenderRect graph_bounds = {
                graph_content.x + 8.0f,
                graph_content.y + 8.0f,
                graph_content.width - 16.0f,
                graph_content.height - 16.0f
            };

            if (state->graph_node_count > 0 &&
                kit_ui_point_in_rect(graph_bounds, input->mouse_x, input->mouse_y)) {
                KitGraphStructHit hit = {0};
                uint32_t rect_hit_index = 0u;
                int node_selected = 0;
                int64_t next_item_id = 0;

                result = ensure_graph_preview_layout_cache(render_ctx, state, graph_bounds);
                if (result.code == CORE_OK &&
                    state->graph_layout_has_graph_data &&
                    state->graph_layout_node_count > 0u) {
                    if (find_node_index_at_point(state->graph_layout_node_layouts,
                                                 state->graph_layout_node_count,
                                                 input->mouse_x,
                                                 input->mouse_y,
                                                 &rect_hit_index) &&
                        rect_hit_index < (uint32_t)state->graph_node_count) {
                        int64_t hit_item_id = state->graph_nodes[rect_hit_index].item_id;
                        node_selected = handle_graph_node_click(state, hit_item_id, out_action);
                    }
                    if (!node_selected) {
                        result = kit_graph_struct_hit_test(state->graph_layout_node_layouts,
                                                           state->graph_layout_node_count,
                                                           input->mouse_x,
                                                           input->mouse_y,
                                                           &hit);
                        if (result.code == CORE_OK &&
                            hit.active &&
                            hit.node_index < (uint32_t)state->graph_node_count) {
                            int64_t hit_item_id = state->graph_nodes[hit.node_index].item_id;
                            node_selected = handle_graph_node_click(state, hit_item_id, out_action);
                        } else {
                            result = core_result_ok();
                        }
                    }
                    if (!node_selected &&
                        select_neighbor_from_edge_click(state,
                                                        state->graph_layout_edges,
                                                        state->graph_layout_edge_count,
                                                        state->graph_layout_edge_routes,
                                                        state->graph_layout_edge_label_layouts,
                                                        state->graph_layout_edge_count,
                                                        input->mouse_x,
                                                        input->mouse_y,
                                                        &next_item_id)) {
                        state->selected_item_id = next_item_id;
                        *out_action = MEM_CONSOLE_ACTION_REFRESH;
                    }
                } else if (result.code != CORE_OK) {
                    return 1;
                }
            }
        }
        if (input->mouse_released) {
            state->graph_click_armed = 0;
        }
    } else {
        state->graph_click_armed = 0;
        state->graph_drag_active = 0;
        state->graph_drag_moved = 0;
        {
            float remaining = right_layout.bounds.height - right_layout.cursor;
            if (remaining < 220.0f) {
                remaining = 220.0f;
            }
            result = kit_ui_stack_next(&right_layout, remaining, 0.0f, &graph_panel);
            if (result.code != CORE_OK) {
                return 1;
            }
        }
        result = push_themed_rect(render_ctx,
                                  &frame,
                                  graph_panel,
                                  10.0f,
                                  CORE_THEME_COLOR_SURFACE_1);
        if (result.code != CORE_OK) {
            return 1;
        }
        {
            KitRenderRect graph_content = {
                graph_panel.x + 6.0f,
                graph_panel.y + 6.0f,
                graph_panel.width - 12.0f,
                graph_panel.height - 12.0f
            };
            result = push_themed_rect(render_ctx,
                                      &frame,
                                      graph_content,
                                      8.0f,
                                      CORE_THEME_COLOR_SURFACE_0);
            if (result.code != CORE_OK) {
                return 1;
            }
            row = (KitRenderRect){
                graph_content.x + 8.0f,
                graph_content.y + 8.0f,
                graph_content.width - 16.0f,
                24.0f
            };
            result = draw_info_line_custom(ui_ctx,
                                           &frame,
                                           row,
                                           "Graph mode is off. Toggle GRAPH ON.",
                                           CORE_THEME_COLOR_TEXT_MUTED,
                                           CORE_FONT_ROLE_UI_REGULAR,
                                           CORE_FONT_TEXT_SIZE_CAPTION);
            if (result.code != CORE_OK) {
                return 1;
            }

            row = (KitRenderRect){
                graph_content.x + 8.0f,
                graph_content.y + 38.0f,
                graph_content.width - 16.0f,
                graph_content.height - 46.0f
            };
        }
        result = draw_graph_preview(render_ctx,
                                    ui_ctx,
                                    input,
                                    &frame,
                                    row,
                                    state);
        if (result.code != CORE_OK) {
            return 1;
        }
    }

    result = kit_render_end_frame(render_ctx, &frame);
    if (result.code != CORE_OK) {
        fprintf(stderr, "mem_console: kit_render_end_frame failed: %d\n", (int)result.code);
        return 1;
    }

    return 0;
}
