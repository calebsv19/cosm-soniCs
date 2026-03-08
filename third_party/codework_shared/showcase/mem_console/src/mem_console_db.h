#ifndef MEM_CONSOLE_DB_H
#define MEM_CONSOLE_DB_H

#include "mem_console_state.h"

CoreResult create_item_from_search(CoreMemDb *db,
                                   MemConsoleState *state,
                                   int64_t *out_item_id);
CoreResult rename_selected_from_title_buffer(CoreMemDb *db, MemConsoleState *state);
CoreResult replace_selected_body_from_body_buffer(CoreMemDb *db, MemConsoleState *state);
CoreResult set_selected_flag(CoreMemDb *db,
                             const MemConsoleState *state,
                             const char *field_name,
                             int field_value);

CoreResult load_graph_neighborhood(CoreMemDb *db, MemConsoleState *state);
CoreResult refresh_state_from_db(CoreMemDb *db, MemConsoleState *state);

#endif
