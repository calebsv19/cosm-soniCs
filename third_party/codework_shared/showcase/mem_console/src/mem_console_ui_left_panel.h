#ifndef MEM_CONSOLE_UI_LEFT_PANEL_H
#define MEM_CONSOLE_UI_LEFT_PANEL_H

#include "mem_console_state.h"
#include "kit_ui.h"

CoreResult mem_console_ui_left_draw_project_filter_chips(KitUiContext *ui_ctx,
                                                         const KitRenderContext *render_ctx,
                                                         KitRenderFrame *frame,
                                                         MemConsoleState *state,
                                                         const KitUiInputState *input,
                                                         KitRenderRect bounds,
                                                         int wheel_y,
                                                         int input_enabled,
                                                         int *out_changed);

#endif
