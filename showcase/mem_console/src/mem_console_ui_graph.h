#ifndef MEM_CONSOLE_UI_GRAPH_H
#define MEM_CONSOLE_UI_GRAPH_H

#include "mem_console_state.h"
#include "kit_ui.h"

int mem_console_ui_graph_find_node_index_at_point(const MemConsoleState *state,
                                                  float x,
                                                  float y,
                                                  uint32_t *out_index);
int mem_console_ui_graph_select_neighbor_from_edge_click(const MemConsoleState *state,
                                                         float mouse_x,
                                                         float mouse_y,
                                                         int64_t *out_item_id);
int mem_console_ui_graph_handle_viewport_interaction(MemConsoleState *state,
                                                     const KitUiInputState *input,
                                                     int wheel_y,
                                                     KitRenderRect graph_bounds);
CoreResult mem_console_ui_graph_ensure_layout_cache(const KitRenderContext *render_ctx,
                                                    MemConsoleState *state,
                                                    KitRenderRect bounds);
CoreResult mem_console_ui_graph_draw_preview(const KitRenderContext *render_ctx,
                                             KitUiContext *ui_ctx,
                                             const KitUiInputState *input,
                                             KitRenderFrame *frame,
                                             KitRenderRect bounds,
                                             MemConsoleState *state);

#endif
