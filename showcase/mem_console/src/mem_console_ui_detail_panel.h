#ifndef MEM_CONSOLE_UI_DETAIL_PANEL_H
#define MEM_CONSOLE_UI_DETAIL_PANEL_H

#include "mem_console_state.h"

void mem_console_ui_detail_build_connection_summary(const MemConsoleState *state,
                                                    char *out_text,
                                                    size_t out_cap);

#endif
