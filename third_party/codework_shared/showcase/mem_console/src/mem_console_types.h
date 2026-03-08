#ifndef MEM_CONSOLE_TYPES_H
#define MEM_CONSOLE_TYPES_H

#include <stdint.h>

#include "core_memdb.h"
#include "kit_graph_struct.h"

enum {
    MEM_CONSOLE_LIST_FETCH_LIMIT = 96,
    MEM_CONSOLE_LIST_ROW_PITCH_PX = 44,
    MEM_CONSOLE_GRAPH_NODE_LIMIT = 24,
    MEM_CONSOLE_GRAPH_EDGE_LIMIT = 48,
    MEM_CONSOLE_GRAPH_EDGE_LIMIT_MIN = 4,
    MEM_CONSOLE_GRAPH_HOPS_MIN = 1,
    MEM_CONSOLE_GRAPH_HOPS_MAX = 5,
    MEM_CONSOLE_SCOPE_FILTER_LIMIT = 8
};

typedef enum MemConsoleAction {
    MEM_CONSOLE_ACTION_NONE = 0,
    MEM_CONSOLE_ACTION_REFRESH = 1,
    MEM_CONSOLE_ACTION_CREATE_FROM_SEARCH = 2,
    MEM_CONSOLE_ACTION_BEGIN_TITLE_EDIT = 3,
    MEM_CONSOLE_ACTION_CANCEL_TITLE_EDIT = 4,
    MEM_CONSOLE_ACTION_SAVE_TITLE_EDIT = 5,
    MEM_CONSOLE_ACTION_BEGIN_BODY_EDIT = 6,
    MEM_CONSOLE_ACTION_CANCEL_BODY_EDIT = 7,
    MEM_CONSOLE_ACTION_SAVE_BODY_EDIT = 8,
    MEM_CONSOLE_ACTION_TOGGLE_PINNED = 9,
    MEM_CONSOLE_ACTION_TOGGLE_CANONICAL = 10,
    MEM_CONSOLE_ACTION_TOGGLE_GRAPH_MODE = 11,
    MEM_CONSOLE_ACTION_REFRESH_GRAPH = 12
} MemConsoleAction;

typedef enum MemConsoleInputTarget {
    MEM_CONSOLE_INPUT_SEARCH = 0,
    MEM_CONSOLE_INPUT_TITLE_EDIT = 1,
    MEM_CONSOLE_INPUT_BODY_EDIT = 2,
    MEM_CONSOLE_INPUT_GRAPH_EDGE_LIMIT = 3
} MemConsoleInputTarget;

typedef struct MemConsoleListItem {
    int64_t id;
    int pinned;
    int canonical;
    char title[160];
    char workspace_key[64];
    char project_key[64];
    char kind[64];
} MemConsoleListItem;

typedef struct MemConsoleGraphNode {
    int64_t item_id;
    int pinned;
    int canonical;
    char title[160];
    char body_preview[256];
} MemConsoleGraphNode;

typedef struct MemConsoleGraphEdge {
    int from_index;
    int to_index;
    char kind[32];
} MemConsoleGraphEdge;

typedef struct MemConsoleState {
    const char *db_path;
    char search_text[256];
    char db_summary_line[384];
    char schema_version[32];
    char status_line[160];
    char theme_name[64];
    char font_name[64];
    char selected_title[160];
    char selected_body[256];
    char title_edit_text[160];
    char body_edit_text[1024];
    char schema_summary_line[96];
    char runtime_summary_line[128];
    char kernel_summary_line[96];
    char theme_summary_line[96];
    char font_summary_line[96];
    char visible_summary_line[96];
    char detail_meta_line[96];
    char pinned_button_label[32];
    char canonical_button_label[32];
    char graph_mode_button_label[32];
    char graph_status_line[96];
    char graph_kind_filter[32];
    char graph_edge_limit_text[16];
    char project_filter_summary_line[128];
    char list_item_labels[MEM_CONSOLE_LIST_FETCH_LIMIT][220];
    char project_filter_labels[MEM_CONSOLE_SCOPE_FILTER_LIMIT][96];
    char project_filter_keys[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64];
    int64_t project_filter_counts[MEM_CONSOLE_SCOPE_FILTER_LIMIT];
    char selected_project_keys[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64];
    char wrapped_body_lines[6][256];
    CoreThemePresetId theme_preset_id;
    CoreFontPresetId font_preset_id;
    int64_t selected_item_id;
    int64_t active_count;
    int64_t matching_count;
    int selected_pinned;
    int selected_canonical;
    int title_edit_mode;
    int body_edit_mode;
    int search_cursor;
    int title_edit_cursor;
    int body_edit_cursor;
    int graph_edge_limit_cursor;
    int graph_mode_enabled;
    int graph_query_edge_limit;
    int graph_query_hops;
    int list_query_offset;
    int visible_start_index;
    int visible_count;
    int search_refresh_pending;
    MemConsoleInputTarget input_target;
    uint64_t search_last_input_ms;
    uint64_t runtime_refresh_submitted;
    uint64_t runtime_refresh_applied;
    uint64_t runtime_refresh_dropped;
    uint64_t runtime_refresh_errors;
    uint64_t runtime_refresh_coalesced;
    int runtime_refresh_in_flight;
    int runtime_pending_intent;
    int project_filter_option_count;
    int selected_project_count;
    int kernel_bridge_enabled;
    uint64_t kernel_tick_count;
    uint64_t kernel_last_work_units;
    int kernel_last_render_requested;
    float list_scroll;
    float project_filter_scroll;
    MemConsoleListItem visible_items[MEM_CONSOLE_LIST_FETCH_LIMIT];
    int graph_node_count;
    int graph_edge_count;
    MemConsoleGraphNode graph_nodes[MEM_CONSOLE_GRAPH_NODE_LIMIT];
    MemConsoleGraphEdge graph_edges[MEM_CONSOLE_GRAPH_EDGE_LIMIT];
    int graph_layout_valid;
    int graph_layout_has_graph_data;
    uint64_t graph_layout_signature;
    KitRenderRect graph_layout_bounds;
    uint32_t graph_layout_node_count;
    uint32_t graph_layout_edge_count;
    KitGraphStructNode graph_layout_nodes[MEM_CONSOLE_GRAPH_NODE_LIMIT];
    KitGraphStructEdge graph_layout_edges[MEM_CONSOLE_GRAPH_EDGE_LIMIT];
    int graph_layout_edge_state_indices[MEM_CONSOLE_GRAPH_EDGE_LIMIT];
    KitGraphStructNodeLayout graph_layout_node_layouts[MEM_CONSOLE_GRAPH_NODE_LIMIT];
    KitGraphStructEdgeRoute graph_layout_edge_routes[MEM_CONSOLE_GRAPH_EDGE_LIMIT];
    KitGraphStructEdgeLabelLayout graph_layout_edge_label_layouts[MEM_CONSOLE_GRAPH_EDGE_LIMIT];
    char graph_draw_edge_labels[MEM_CONSOLE_GRAPH_EDGE_LIMIT][96];
    char graph_draw_node_labels[MEM_CONSOLE_GRAPH_NODE_LIMIT][192];
    char graph_hud_id_line[48];
    char graph_hud_title_raw[176];
    char graph_hud_title[176];
    char graph_hud_flags[96];
    char graph_hud_body[256];
    char graph_hud_wrapped_lines[4][256];
    int graph_drag_active;
    int graph_drag_moved;
    int graph_click_armed;
    uint64_t graph_last_click_ms;
    int64_t graph_last_click_item_id;
    float graph_drag_last_x;
    float graph_drag_last_y;
    KitRenderRect left_pane;
    KitRenderRect right_pane;
    KitGraphStructViewport graph_viewport;
} MemConsoleState;

#endif
