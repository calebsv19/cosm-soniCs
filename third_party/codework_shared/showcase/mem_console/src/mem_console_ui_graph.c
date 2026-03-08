#include "mem_console_ui_graph.h"

#include <stdio.h>
#include <string.h>

#include "mem_console_ui_common.h"

static const CoreFontTextSizeTier k_graph_node_label_tiers[] = {
    CORE_FONT_TEXT_SIZE_CAPTION
};

static const CoreFontTextSizeTier k_graph_edge_label_tiers[] = {
    CORE_FONT_TEXT_SIZE_BASIC,
    CORE_FONT_TEXT_SIZE_CAPTION
};

static void configure_graph_layout_style(KitGraphStructLayoutStyle *style) {
    if (!style) {
        return;
    }
    kit_graph_struct_layout_style_default(style);
    style->padding = 10.0f;
    style->level_gap = 20.0f;
    style->sibling_gap = 12.0f;
    style->node_width = 46.0f;
    style->node_height = 20.0f;
    style->node_min_width = 34.0f;
    style->node_max_width = 74.0f;
    style->node_padding_x = 4.0f;
    style->label_char_width = 6.0f;
    style->node_label_font_role = CORE_FONT_ROLE_UI_MEDIUM;
    style->node_label_text_tier = CORE_FONT_TEXT_SIZE_CAPTION;
    style->measure_text_fn = 0;
    style->measure_text_user = 0;
    style->edge_label_padding_x = 6.0f;
    style->edge_label_height = 16.0f;
    style->edge_label_lane_gap = 3.0f;
}

static void sanitize_hud_text(const char *input, char *output, size_t output_cap) {
    size_t w = 0u;
    size_t i = 0u;
    int last_was_space = 0;
    int in_ansi_escape = 0;

    if (!output || output_cap == 0u) {
        return;
    }
    output[0] = '\0';
    if (!input) {
        return;
    }

    while (input[i] != '\0' && w + 1u < output_cap) {
        unsigned char c = (unsigned char)input[i];
        char out_ch = '?';

        if (in_ansi_escape) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                in_ansi_escape = 0;
            }
            i += 1u;
            continue;
        }

        if (c == 27u) {
            in_ansi_escape = 1;
            i += 1u;
            continue;
        }

        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            out_ch = ' ';
        } else if (c >= 32u && c <= 126u) {
            out_ch = (char)c;
        } else {
            i += 1u;
            continue;
        }

        if (out_ch == ' ') {
            if (!last_was_space) {
                output[w++] = out_ch;
                last_was_space = 1;
            }
        } else {
            output[w++] = out_ch;
            last_was_space = 0;
        }
        i += 1u;
    }

    while (w > 0u && output[w - 1u] == ' ') {
        w -= 1u;
    }
    output[w] = '\0';
    if (output[0] == '\0' && output_cap >= 2u) {
        output[0] = '-';
        output[1] = '\0';
    }
}

int mem_console_ui_graph_handle_viewport_interaction(MemConsoleState *state,
                                                     const KitUiInputState *input,
                                                     int wheel_y,
                                                     KitRenderRect graph_bounds) {
    int hovered;
    int suppress_release_click = 0;

    if (!state || !input) {
        return 0;
    }

    hovered = kit_ui_point_in_rect(graph_bounds, input->mouse_x, input->mouse_y);

    if (wheel_y != 0 && hovered) {
        float old_zoom = state->graph_viewport.zoom;
        float center_x = graph_bounds.x + (graph_bounds.width * 0.5f);
        float center_y = graph_bounds.y + (graph_bounds.height * 0.5f);
        float anchor_dx = input->mouse_x - center_x;
        float anchor_dy = input->mouse_y - center_y;
        int steps = wheel_y > 0 ? wheel_y : -wheel_y;
        float zoom_factor = wheel_y > 0 ? 1.12f : (1.0f / 1.12f);
        while (steps > 0) {
            (void)kit_graph_struct_viewport_zoom_by(&state->graph_viewport,
                                                    zoom_factor,
                                                    0.05f,
                                                    48.0f);
            steps -= 1;
        }
        if (old_zoom > 0.0001f && state->graph_viewport.zoom > 0.0001f) {
            float zoom_ratio = state->graph_viewport.zoom / old_zoom;
            state->graph_viewport.pan_x += anchor_dx * (1.0f - zoom_ratio);
            state->graph_viewport.pan_y += anchor_dy * (1.0f - zoom_ratio);
        }
    }

    if (input->mouse_pressed) {
        if (hovered) {
            state->graph_click_armed = 1;
            state->graph_drag_active = 1;
            state->graph_drag_moved = 0;
            state->graph_drag_last_x = input->mouse_x;
            state->graph_drag_last_y = input->mouse_y;
        } else {
            state->graph_click_armed = 0;
            state->graph_drag_active = 0;
            state->graph_drag_moved = 0;
        }
    }

    if (state->graph_drag_active && input->mouse_down) {
        float delta_x = input->mouse_x - state->graph_drag_last_x;
        float delta_y = input->mouse_y - state->graph_drag_last_y;
        if (delta_x != 0.0f || delta_y != 0.0f) {
            float abs_dx = delta_x < 0.0f ? -delta_x : delta_x;
            float abs_dy = delta_y < 0.0f ? -delta_y : delta_y;
            (void)kit_graph_struct_viewport_pan_by(&state->graph_viewport, delta_x, delta_y);
            state->graph_drag_last_x = input->mouse_x;
            state->graph_drag_last_y = input->mouse_y;
            if (!state->graph_drag_moved && (abs_dx >= 0.5f || abs_dy >= 0.5f)) {
                state->graph_drag_moved = 1;
            }
        }
    }

    if (state->graph_drag_active && input->mouse_released) {
        suppress_release_click = state->graph_drag_moved ? 1 : 0;
        state->graph_drag_active = 0;
        state->graph_drag_moved = 0;
    }

    if (!input->mouse_down && !input->mouse_pressed && !input->mouse_released && !hovered) {
        state->graph_click_armed = 0;
        state->graph_drag_active = 0;
        state->graph_drag_moved = 0;
    }

    return suppress_release_click;
}

static CoreResult compute_graph_preview_layout(const MemConsoleState *state,
                                               const KitRenderContext *render_ctx,
                                               KitRenderRect bounds,
                                               KitGraphStructNode *out_nodes,
                                               uint32_t *out_node_count,
                                               KitGraphStructEdge *out_edges,
                                               uint32_t *out_edge_count,
                                               int *out_edge_state_indices,
                                               KitGraphStructNodeLayout *out_layouts,
                                               int *out_has_graph_data) {
    KitGraphStructLayoutStyle style;
    KitGraphStructViewport viewport;
    char node_id_labels[MEM_CONSOLE_GRAPH_NODE_LIMIT][24];
    float zoom_for_node_size = 1.0f;
    uint32_t node_count = 0u;
    uint32_t edge_count = 0u;
    uint32_t i;
    int has_graph_data = 0;

    if (!state || !out_nodes || !out_node_count || !out_edges || !out_edge_count ||
        !out_edge_state_indices || !out_layouts || !out_has_graph_data) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid graph preview layout request" };
    }

    has_graph_data = state->graph_node_count > 0 ? 1 : 0;
    if (has_graph_data) {
        node_count = (uint32_t)state->graph_node_count;
        if (node_count > MEM_CONSOLE_GRAPH_NODE_LIMIT) {
            node_count = MEM_CONSOLE_GRAPH_NODE_LIMIT;
        }

        for (i = 0u; i < node_count; ++i) {
            out_nodes[i].id = i + 1u;
            (void)snprintf(node_id_labels[i],
                           sizeof(node_id_labels[i]),
                           "%lld",
                           (long long)state->graph_nodes[i].item_id);
            out_nodes[i].label = node_id_labels[i];
        }

        for (i = 0u; i < (uint32_t)state->graph_edge_count && edge_count < MEM_CONSOLE_GRAPH_EDGE_LIMIT; ++i) {
            int from_index = state->graph_edges[i].from_index;
            int to_index = state->graph_edges[i].to_index;

            if (from_index < 0 || to_index < 0) {
                continue;
            }
            if ((uint32_t)from_index >= node_count || (uint32_t)to_index >= node_count) {
                continue;
            }

            out_edges[edge_count].from_id = (uint32_t)from_index + 1u;
            out_edges[edge_count].to_id = (uint32_t)to_index + 1u;
            out_edge_state_indices[edge_count] = (int)i;
            edge_count += 1u;
        }
    } else {
        node_count = 1u;
        out_nodes[0].id = 1u;
        out_nodes[0].label = "No Memory";
    }

    configure_graph_layout_style(&style);
    viewport = state->graph_viewport;
    if (viewport.zoom > 0.0001f) {
        zoom_for_node_size = viewport.zoom;
    }

    /* Keep node box and ID text sizing stable across zoom changes. */
    style.node_width /= zoom_for_node_size;
    style.node_height /= zoom_for_node_size;
    style.node_min_width /= zoom_for_node_size;
    style.node_max_width /= zoom_for_node_size;
    style.node_padding_x /= zoom_for_node_size;
    style.label_char_width /= zoom_for_node_size;

    (void)render_ctx;

    {
        CoreResult result = kit_graph_struct_compute_layered_dag_layout(out_nodes,
                                                                         node_count,
                                                                         out_edges,
                                                                         edge_count,
                                                                         bounds,
                                                                         &viewport,
                                                                         &style,
                                                                         out_layouts);
        if (result.code != CORE_OK) {
            return result;
        }
    }

    *out_node_count = node_count;
    *out_edge_count = edge_count;
    *out_has_graph_data = has_graph_data;
    return core_result_ok();
}

static uint64_t graph_hash_bytes(uint64_t seed, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = seed;
    size_t i;

    for (i = 0u; i < len; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint32_t float_bits(float value) {
    uint32_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t graph_preview_layout_signature(const MemConsoleState *state, KitRenderRect bounds) {
    uint64_t hash = 1469598103934665603ull;
    uint32_t bounds_bits[4];
    uint32_t viewport_bits[3];
    int node_count = 0;
    int edge_count = 0;
    int i;

    if (!state) {
        return hash;
    }

    bounds_bits[0] = float_bits(bounds.x);
    bounds_bits[1] = float_bits(bounds.y);
    bounds_bits[2] = float_bits(bounds.width);
    bounds_bits[3] = float_bits(bounds.height);
    viewport_bits[0] = float_bits(state->graph_viewport.pan_x);
    viewport_bits[1] = float_bits(state->graph_viewport.pan_y);
    viewport_bits[2] = float_bits(state->graph_viewport.zoom);

    hash = graph_hash_bytes(hash, &state->graph_node_count, sizeof(state->graph_node_count));
    hash = graph_hash_bytes(hash, &state->graph_edge_count, sizeof(state->graph_edge_count));
    hash = graph_hash_bytes(hash, bounds_bits, sizeof(bounds_bits));
    hash = graph_hash_bytes(hash, viewport_bits, sizeof(viewport_bits));
    hash = graph_hash_bytes(hash, state->graph_kind_filter, strlen(state->graph_kind_filter));
    hash = graph_hash_bytes(hash, &state->font_preset_id, sizeof(state->font_preset_id));

    node_count = state->graph_node_count;
    if (node_count < 0) node_count = 0;
    if (node_count > MEM_CONSOLE_GRAPH_NODE_LIMIT) node_count = MEM_CONSOLE_GRAPH_NODE_LIMIT;
    edge_count = state->graph_edge_count;
    if (edge_count < 0) edge_count = 0;
    if (edge_count > MEM_CONSOLE_GRAPH_EDGE_LIMIT) edge_count = MEM_CONSOLE_GRAPH_EDGE_LIMIT;

    for (i = 0; i < node_count; ++i) {
        hash = graph_hash_bytes(hash, &state->graph_nodes[i].item_id, sizeof(state->graph_nodes[i].item_id));
        hash = graph_hash_bytes(hash,
                                state->graph_nodes[i].title,
                                strlen(state->graph_nodes[i].title));
        hash = graph_hash_bytes(hash, &state->graph_nodes[i].pinned, sizeof(state->graph_nodes[i].pinned));
        hash = graph_hash_bytes(hash, &state->graph_nodes[i].canonical, sizeof(state->graph_nodes[i].canonical));
    }
    for (i = 0; i < edge_count; ++i) {
        hash = graph_hash_bytes(hash, &state->graph_edges[i].from_index, sizeof(state->graph_edges[i].from_index));
        hash = graph_hash_bytes(hash, &state->graph_edges[i].to_index, sizeof(state->graph_edges[i].to_index));
        hash = graph_hash_bytes(hash,
                                state->graph_edges[i].kind,
                                strlen(state->graph_edges[i].kind));
    }

    return hash;
}

CoreResult mem_console_ui_graph_ensure_layout_cache(const KitRenderContext *render_ctx,
                                                    MemConsoleState *state,
                                                    KitRenderRect bounds) {
    KitGraphStructLayoutStyle style;
    KitGraphStructEdgeLabel edge_labels[MEM_CONSOLE_GRAPH_EDGE_LIMIT];
    KitGraphStructEdgeLabelOptions label_options;
    CoreResult result;
    uint64_t signature;
    uint32_t i;

    if (!state) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid graph layout cache request" };
    }

    signature = graph_preview_layout_signature(state, bounds);
    if (state->graph_layout_valid && state->graph_layout_signature == signature) {
        return core_result_ok();
    }

    result = compute_graph_preview_layout(state,
                                          render_ctx,
                                          bounds,
                                          state->graph_layout_nodes,
                                          &state->graph_layout_node_count,
                                          state->graph_layout_edges,
                                          &state->graph_layout_edge_count,
                                          state->graph_layout_edge_state_indices,
                                          state->graph_layout_node_layouts,
                                          &state->graph_layout_has_graph_data);
    if (result.code != CORE_OK) {
        return result;
    }

    configure_graph_layout_style(&style);
    for (i = 0u; i < state->graph_layout_edge_count; ++i) {
        int state_edge_index = state->graph_layout_edge_state_indices[i];
        const char *edge_kind_label = "related";
        if (state_edge_index >= 0 &&
            state_edge_index < state->graph_edge_count &&
            state->graph_edges[state_edge_index].kind[0] != '\0') {
            edge_kind_label = state->graph_edges[state_edge_index].kind;
        }
        edge_labels[i].text = edge_kind_label;
    }
    result = kit_graph_struct_compute_edge_routes(state->graph_layout_edges,
                                                  state->graph_layout_edge_count,
                                                  state->graph_layout_node_layouts,
                                                  state->graph_layout_node_count,
                                                  KIT_GRAPH_STRUCT_ROUTE_ORTHOGONAL,
                                                  state->graph_layout_edge_routes);
    if (result.code != CORE_OK) {
        return result;
    }
    kit_graph_struct_edge_label_options_default(&label_options);
    label_options.current_zoom = state->graph_viewport.zoom;
    label_options.min_zoom_for_labels = 0.80f;
    label_options.density_mode = state->graph_layout_edge_count > 10u
                                     ? KIT_GRAPH_STRUCT_EDGE_LABEL_DENSITY_CULL_OVERLAP
                                     : KIT_GRAPH_STRUCT_EDGE_LABEL_DENSITY_ALL;
    result = kit_graph_struct_compute_edge_label_layouts_routed(state->graph_layout_node_layouts,
                                                                state->graph_layout_node_count,
                                                                state->graph_layout_edge_routes,
                                                                state->graph_layout_edge_count,
                                                                &style,
                                                                edge_labels,
                                                                state->graph_layout_edge_count,
                                                                &label_options,
                                                                state->graph_layout_edge_label_layouts);
    if (result.code != CORE_OK) {
        return result;
    }

    state->graph_layout_signature = signature;
    state->graph_layout_bounds = bounds;
    state->graph_layout_valid = 1;
    return core_result_ok();
}

static KitRenderColor mix_color(KitRenderColor a, KitRenderColor b, float t) {
    float clamped_t = t;
    float inv_t;
    KitRenderColor out;

    if (clamped_t < 0.0f) clamped_t = 0.0f;
    if (clamped_t > 1.0f) clamped_t = 1.0f;
    inv_t = 1.0f - clamped_t;

    out.r = (uint8_t)((a.r * inv_t) + (b.r * clamped_t));
    out.g = (uint8_t)((a.g * inv_t) + (b.g * clamped_t));
    out.b = (uint8_t)((a.b * inv_t) + (b.b * clamped_t));
    out.a = 255u;
    return out;
}

static int has_reverse_edge_for_selected(const MemConsoleState *state,
                                         int64_t selected_item_id,
                                         int64_t neighbor_item_id) {
    int i;

    if (!state || selected_item_id == 0 || neighbor_item_id == 0) {
        return 0;
    }

    for (i = 0; i < state->graph_edge_count; ++i) {
        const MemConsoleGraphEdge *edge = &state->graph_edges[i];
        int from_index = edge->from_index;
        int to_index = edge->to_index;
        int64_t from_item_id;
        int64_t to_item_id;

        if (from_index < 0 || to_index < 0 ||
            from_index >= state->graph_node_count ||
            to_index >= state->graph_node_count) {
            continue;
        }

        from_item_id = state->graph_nodes[from_index].item_id;
        to_item_id = state->graph_nodes[to_index].item_id;
        if (from_item_id == neighbor_item_id && to_item_id == selected_item_id) {
            return 1;
        }
    }

    return 0;
}

int mem_console_ui_graph_find_node_index_at_point(const MemConsoleState *state,
                                                  float x,
                                                  float y,
                                                  uint32_t *out_index) {
    uint32_t i;

    if (!state || !out_index) {
        return 0;
    }

    for (i = 0u; i < state->graph_layout_node_count; ++i) {
        if (kit_ui_point_in_rect(state->graph_layout_node_layouts[i].rect, x, y)) {
            *out_index = i;
            return 1;
        }
    }

    return 0;
}

int mem_console_ui_graph_select_neighbor_from_edge_click(const MemConsoleState *state,
                                                         float mouse_x,
                                                         float mouse_y,
                                                         int64_t *out_item_id) {
    CoreResult result;
    KitGraphStructEdgeHit edge_hit;
    KitGraphStructEdgeLabelHit label_hit;
    int edge_index = -1;
    int best_from_index = -1;
    int best_to_index = -1;

    if (!state || !out_item_id) {
        return 0;
    }

    result = kit_graph_struct_hit_test_edge_labels(state->graph_layout_edge_label_layouts,
                                                   state->graph_layout_edge_count,
                                                   mouse_x,
                                                   mouse_y,
                                                   &label_hit);
    if (result.code == CORE_OK && label_hit.active) {
        edge_index = (int)label_hit.edge_index;
    }
    if (edge_index < 0) {
        result = kit_graph_struct_hit_test_edge_routes(state->graph_layout_edge_routes,
                                                       state->graph_layout_edge_count,
                                                       mouse_x,
                                                       mouse_y,
                                                       10.0f,
                                                       &edge_hit);
        if (result.code != CORE_OK || !edge_hit.active) {
            return 0;
        }
        edge_index = (int)edge_hit.edge_index;
    }
    if (edge_index < 0 || (uint32_t)edge_index >= state->graph_layout_edge_count) {
        return 0;
    }

    best_from_index = (int)state->graph_layout_edges[edge_index].from_id - 1;
    best_to_index = (int)state->graph_layout_edges[edge_index].to_id - 1;
    if (best_from_index < 0 || best_to_index < 0 ||
        best_from_index >= state->graph_node_count ||
        best_to_index >= state->graph_node_count) {
        return 0;
    }

    {
        int64_t from_item_id = state->graph_nodes[best_from_index].item_id;
        int64_t to_item_id = state->graph_nodes[best_to_index].item_id;
        int64_t next_item_id = 0;

        if (state->selected_item_id == from_item_id && to_item_id != 0) {
            next_item_id = to_item_id;
        } else if (state->selected_item_id == to_item_id && from_item_id != 0) {
            next_item_id = from_item_id;
        } else if (to_item_id != 0 && to_item_id != state->selected_item_id) {
            next_item_id = to_item_id;
        } else {
            next_item_id = from_item_id;
        }

        if (next_item_id != 0 && next_item_id != state->selected_item_id) {
            *out_item_id = next_item_id;
            return 1;
        }
    }
    return 0;
}

CoreResult mem_console_ui_graph_draw_preview(const KitRenderContext *render_ctx,
                                             KitUiContext *ui_ctx,
                                             const KitUiInputState *input,
                                             KitRenderFrame *frame,
                                             KitRenderRect bounds,
                                             MemConsoleState *state) {
    CoreResult result;
    KitRenderColor edge_color;
    KitRenderColor edge_white;
    KitRenderColor edge_ok;
    KitRenderColor edge_error;
    KitRenderColor node_base_color;
    KitRenderColor node_lane_color;
    KitRenderColor node_selected_color;
    CoreResult color_result;
    uint32_t node_count = 0u;
    uint32_t edge_count = 0u;
    uint32_t i;
    int has_graph_data = 0;
    int hovered_node_index = -1;

    if (!render_ctx || !ui_ctx || !frame || !state) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid argument" };
    }

    result = mem_console_ui_graph_ensure_layout_cache(render_ctx, state, bounds);
    if (result.code != CORE_OK) {
        return result;
    }
    node_count = state->graph_layout_node_count;
    edge_count = state->graph_layout_edge_count;
    has_graph_data = state->graph_layout_has_graph_data;

    if (input && has_graph_data && node_count > 0u &&
        kit_ui_point_in_rect(bounds, input->mouse_x, input->mouse_y)) {
        uint32_t rect_hit_index = 0u;
        if (mem_console_ui_graph_find_node_index_at_point(state,
                                                          input->mouse_x,
                                                          input->mouse_y,
                                                          &rect_hit_index) &&
            rect_hit_index < node_count) {
            hovered_node_index = (int)rect_hit_index;
        } else {
            KitGraphStructHit hit = {0};
            CoreResult hit_result = kit_graph_struct_hit_test(state->graph_layout_node_layouts,
                                                              node_count,
                                                              input->mouse_x,
                                                              input->mouse_y,
                                                              &hit);
            if (hit_result.code == CORE_OK && hit.active && hit.node_index < node_count) {
                hovered_node_index = (int)hit.node_index;
            }
        }
    }

    color_result = mem_console_ui_resolve_theme_color(render_ctx, CORE_THEME_COLOR_TEXT_MUTED, &edge_color);
    if (color_result.code != CORE_OK) {
        return color_result;
    }
    color_result = mem_console_ui_resolve_theme_color(render_ctx, CORE_THEME_COLOR_TEXT_PRIMARY, &edge_white);
    if (color_result.code != CORE_OK) {
        return color_result;
    }
    color_result = mem_console_ui_resolve_theme_color(render_ctx, CORE_THEME_COLOR_STATUS_OK, &edge_ok);
    if (color_result.code != CORE_OK) {
        return color_result;
    }
    color_result = mem_console_ui_resolve_theme_color(render_ctx, CORE_THEME_COLOR_STATUS_ERROR, &edge_error);
    if (color_result.code != CORE_OK) {
        return color_result;
    }
    color_result = mem_console_ui_resolve_theme_color(render_ctx, CORE_THEME_COLOR_SURFACE_1, &node_base_color);
    if (color_result.code != CORE_OK) {
        return color_result;
    }
    color_result = mem_console_ui_resolve_theme_color(render_ctx, CORE_THEME_COLOR_SURFACE_2, &node_lane_color);
    if (color_result.code != CORE_OK) {
        return color_result;
    }
    color_result = mem_console_ui_resolve_theme_color(render_ctx, CORE_THEME_COLOR_ACCENT_PRIMARY, &node_selected_color);
    if (color_result.code != CORE_OK) {
        return color_result;
    }

    for (i = 0u; i < edge_count; ++i) {
        KitRenderLineCommand line_cmd;
        KitRenderTextCommand label_text_cmd;
        KitUiTextFitResult edge_text_fit;
        KitRenderRect label_bg_rect = state->graph_layout_edge_label_layouts[i].rect;
        const KitGraphStructEdgeRoute *route = &state->graph_layout_edge_routes[i];
        int state_edge_index = state->graph_layout_edge_state_indices[i];
        const char *edge_kind_label = "related";
        KitRenderColor edge_draw_color = edge_color;
        if (state_edge_index >= 0 &&
            state_edge_index < state->graph_edge_count &&
            state->graph_edges[state_edge_index].kind[0] != '\0') {
            edge_kind_label = state->graph_edges[state_edge_index].kind;
        }
        if (route->point_count < 2u) {
            continue;
        }

        if (state_edge_index >= 0 && state_edge_index < state->graph_edge_count) {
            const MemConsoleGraphEdge *state_edge = &state->graph_edges[state_edge_index];
            int from_index = state_edge->from_index;
            int to_index = state_edge->to_index;

            if (from_index >= 0 && to_index >= 0 &&
                from_index < state->graph_node_count &&
                to_index < state->graph_node_count) {
                int64_t from_item_id = state->graph_nodes[from_index].item_id;
                int64_t to_item_id = state->graph_nodes[to_index].item_id;

                if (from_item_id == state->selected_item_id && to_item_id != 0) {
                    int bidirectional = has_reverse_edge_for_selected(state,
                                                                      state->selected_item_id,
                                                                      to_item_id);
                    edge_draw_color = bidirectional
                                          ? edge_white
                                          : mix_color(edge_white, edge_ok, 0.20f);
                } else if (to_item_id == state->selected_item_id && from_item_id != 0) {
                    int bidirectional = has_reverse_edge_for_selected(state,
                                                                      state->selected_item_id,
                                                                      from_item_id);
                    edge_draw_color = bidirectional
                                          ? edge_white
                                          : mix_color(edge_white, edge_error, 0.20f);
                }
            }
        }

        {
            uint32_t p;
            for (p = 0u; p + 1u < route->point_count; ++p) {
                line_cmd.p0 = route->points[p];
                line_cmd.p1 = route->points[p + 1u];
                line_cmd.thickness = 2.0f;
                line_cmd.color = edge_draw_color;
                line_cmd.transform = kit_render_identity_transform();
                result = kit_render_push_line(frame, &line_cmd);
                if (result.code != CORE_OK) {
                    return result;
                }
            }
        }
        if (label_bg_rect.width <= 0.0f || label_bg_rect.height <= 0.0f) {
            continue;
        }
        result = mem_console_ui_push_themed_rect(render_ctx,
                                                 frame,
                                                 label_bg_rect,
                                                 3.0f,
                                                 CORE_THEME_COLOR_SURFACE_0);
        if (result.code != CORE_OK) {
            return result;
        }

        label_text_cmd.origin.x = label_bg_rect.x + 4.0f;
        label_text_cmd.origin.y = label_bg_rect.y + (label_bg_rect.height * 0.5f);
        result = kit_ui_fit_text_to_rect(ui_ctx,
                                         edge_kind_label,
                                         CORE_FONT_ROLE_UI_REGULAR,
                                         k_graph_edge_label_tiers,
                                         (uint32_t)(sizeof(k_graph_edge_label_tiers) /
                                                    sizeof(k_graph_edge_label_tiers[0])),
                                         label_bg_rect.width - 8.0f,
                                         label_bg_rect.height - 2.0f,
                                         state->graph_draw_edge_labels[i],
                                         sizeof(state->graph_draw_edge_labels[i]),
                                         &edge_text_fit);
        if (result.code != CORE_OK) {
            return result;
        }
        label_text_cmd.text = state->graph_draw_edge_labels[i];
        label_text_cmd.font_role = CORE_FONT_ROLE_UI_REGULAR;
        label_text_cmd.text_tier = edge_text_fit.text_tier;
        label_text_cmd.color_token = CORE_THEME_COLOR_TEXT_MUTED;
        label_text_cmd.transform = kit_render_identity_transform();
        result = kit_render_push_text(frame, &label_text_cmd);
        if (result.code != CORE_OK) {
            return result;
        }
    }

    for (i = 0u; i < node_count; ++i) {
        KitRenderColor fill_color = node_base_color;
        KitRenderRectCommand rect_cmd;
        KitRenderTextCommand text_cmd;
        KitUiTextFitResult node_text_fit;
        char node_id_text[24];

        if (has_graph_data) {
            const MemConsoleGraphNode *graph_node = &state->graph_nodes[i];
            if (graph_node->item_id == state->selected_item_id) {
                fill_color = node_selected_color;
            } else if (graph_node->canonical || graph_node->pinned) {
                fill_color = node_lane_color;
            }
            if ((int)i == hovered_node_index && graph_node->item_id != state->selected_item_id) {
                fill_color = mix_color(fill_color, edge_white, 0.16f);
            }
        }

        rect_cmd.rect = state->graph_layout_node_layouts[i].rect;
        rect_cmd.corner_radius = 3.0f;
        rect_cmd.color = fill_color;
        rect_cmd.transform = kit_render_identity_transform();
        result = kit_render_push_rect(frame, &rect_cmd);
        if (result.code != CORE_OK) {
            return result;
        }

        text_cmd.origin.x = state->graph_layout_node_layouts[i].rect.x + 4.0f;
        text_cmd.origin.y = state->graph_layout_node_layouts[i].rect.y +
                            (state->graph_layout_node_layouts[i].rect.height * 0.5f);
        if (has_graph_data && i < (uint32_t)state->graph_node_count) {
            (void)snprintf(node_id_text,
                           sizeof(node_id_text),
                           "%lld",
                           (long long)state->graph_nodes[i].item_id);
        } else {
            (void)snprintf(node_id_text, sizeof(node_id_text), "0");
        }
        result = kit_ui_fit_text_to_rect(ui_ctx,
                                         node_id_text,
                                         CORE_FONT_ROLE_UI_MEDIUM,
                                         k_graph_node_label_tiers,
                                         (uint32_t)(sizeof(k_graph_node_label_tiers) /
                                                    sizeof(k_graph_node_label_tiers[0])),
                                         state->graph_layout_node_layouts[i].rect.width - 8.0f,
                                         state->graph_layout_node_layouts[i].rect.height - 4.0f,
                                         state->graph_draw_node_labels[i],
                                         sizeof(state->graph_draw_node_labels[i]),
                                         &node_text_fit);
        if (result.code != CORE_OK) {
            return result;
        }
        if (state->graph_draw_node_labels[i][0] == '\0') {
            if (node_id_text[0] != '\0') {
                state->graph_draw_node_labels[i][0] = node_id_text[0];
                state->graph_draw_node_labels[i][1] = '\0';
            } else {
                state->graph_draw_node_labels[i][0] = '0';
                state->graph_draw_node_labels[i][1] = '\0';
            }
            node_text_fit.text_tier = CORE_FONT_TEXT_SIZE_CAPTION;
        }
        text_cmd.text = state->graph_draw_node_labels[i];
        text_cmd.font_role = CORE_FONT_ROLE_UI_MEDIUM;
        text_cmd.text_tier = node_text_fit.text_tier;
        text_cmd.color_token = CORE_THEME_COLOR_TEXT_PRIMARY;
        text_cmd.transform = kit_render_identity_transform();
        result = kit_render_push_text(frame, &text_cmd);
        if (result.code != CORE_OK) {
            return result;
        }
    }

    if (has_graph_data &&
        hovered_node_index >= 0 &&
        hovered_node_index < state->graph_node_count) {
        const MemConsoleGraphNode *hovered_node = &state->graph_nodes[hovered_node_index];
        float hud_width = bounds.width * 0.48f;
        KitRenderRect hud_outer;
        KitRenderRect hud_inner;
        KitRenderRect hud_row;
        const char *raw_body = hovered_node->body_preview[0] ? hovered_node->body_preview : "(no body)";
        const char *body_text = state->graph_hud_body;

        if (hud_width < 260.0f) {
            hud_width = 260.0f;
        }
        if (hud_width > (bounds.width - 20.0f)) {
            hud_width = bounds.width - 20.0f;
        }

        hud_outer = (KitRenderRect){
            bounds.x + 10.0f,
            bounds.y + 10.0f,
            hud_width,
            154.0f
        };
        hud_inner = (KitRenderRect){
            hud_outer.x + 6.0f,
            hud_outer.y + 6.0f,
            hud_outer.width - 12.0f,
            hud_outer.height - 12.0f
        };

        result = mem_console_ui_push_themed_rect(render_ctx,
                                                 frame,
                                                 hud_outer,
                                                 8.0f,
                                                 CORE_THEME_COLOR_SURFACE_1);
        if (result.code != CORE_OK) {
            return result;
        }
        result = mem_console_ui_push_themed_rect(render_ctx,
                                                 frame,
                                                 hud_inner,
                                                 6.0f,
                                                 CORE_THEME_COLOR_SURFACE_0);
        if (result.code != CORE_OK) {
            return result;
        }

        (void)snprintf(state->graph_hud_id_line,
                       sizeof(state->graph_hud_id_line),
                       "ID %lld",
                       (long long)hovered_node->item_id);
        result = mem_console_ui_draw_info_line_custom(ui_ctx,
                                                      frame,
                                                      (KitRenderRect){
                                                          hud_inner.x + 8.0f,
                                                          hud_inner.y + 6.0f,
                                                          hud_inner.width - 16.0f,
                                                          20.0f
                                                      },
                                                      state->graph_hud_id_line,
                                                      CORE_THEME_COLOR_TEXT_MUTED,
                                                      CORE_FONT_ROLE_UI_MEDIUM,
                                                      CORE_FONT_TEXT_SIZE_CAPTION);
        if (result.code != CORE_OK) {
            return result;
        }

        format_text_for_width(state->graph_hud_title_raw,
                              sizeof(state->graph_hud_title_raw),
                              hovered_node->title[0] ? hovered_node->title : "UNTITLED",
                              hud_inner.width - 16.0f,
                              CORE_FONT_TEXT_SIZE_BASIC);
        sanitize_hud_text(state->graph_hud_title_raw,
                          state->graph_hud_title,
                          sizeof(state->graph_hud_title));
        sanitize_hud_text(raw_body, state->graph_hud_body, sizeof(state->graph_hud_body));
        result = mem_console_ui_draw_info_line_custom(ui_ctx,
                                                      frame,
                                                      (KitRenderRect){
                                                          hud_inner.x + 8.0f,
                                                          hud_inner.y + 28.0f,
                                                          hud_inner.width - 16.0f,
                                                          22.0f
                                                      },
                                                      state->graph_hud_title,
                                                      CORE_THEME_COLOR_TEXT_PRIMARY,
                                                      CORE_FONT_ROLE_UI_BOLD,
                                                      CORE_FONT_TEXT_SIZE_PARAGRAPH);
        if (result.code != CORE_OK) {
            return result;
        }

        (void)snprintf(state->graph_hud_flags,
                       sizeof(state->graph_hud_flags),
                       "PIN %s | CAN %s",
                       hovered_node->pinned ? "ON" : "OFF",
                       hovered_node->canonical ? "ON" : "OFF");
        result = mem_console_ui_draw_info_line_custom(ui_ctx,
                                                      frame,
                                                      (KitRenderRect){
                                                          hud_inner.x + 8.0f,
                                                          hud_inner.y + 50.0f,
                                                          hud_inner.width - 16.0f,
                                                          20.0f
                                                      },
                                                      state->graph_hud_flags,
                                                      CORE_THEME_COLOR_TEXT_MUTED,
                                                      CORE_FONT_ROLE_UI_REGULAR,
                                                      CORE_FONT_TEXT_SIZE_CAPTION);
        if (result.code != CORE_OK) {
            return result;
        }

        hud_row = (KitRenderRect){
            hud_inner.x + 8.0f,
            hud_inner.y + 72.0f,
            hud_inner.width - 16.0f,
            hud_inner.height - 80.0f
        };
        result = mem_console_ui_draw_wrapped_text_block(ui_ctx,
                                                        frame,
                                                        state->graph_hud_wrapped_lines,
                                                        4,
                                                        hud_row,
                                                        body_text,
                                                        CORE_THEME_COLOR_TEXT_MUTED,
                                                        CORE_FONT_TEXT_SIZE_BASIC,
                                                        4);
        if (result.code != CORE_OK) {
            return result;
        }
    }

    return core_result_ok();
}
