#ifndef MEM_CONSOLE_UI_H
#define MEM_CONSOLE_UI_H

#include "kit_ui.h"
#include "mem_console_state.h"

int run_frame(KitRenderContext *render_ctx,
              KitUiContext *ui_ctx,
              MemConsoleState *state,
              const KitUiInputState *input,
              int frame_width,
              int frame_height,
              int wheel_y,
              MemConsoleAction *out_action);

#endif
