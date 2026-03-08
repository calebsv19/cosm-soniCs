#ifndef MEM_CONSOLE_PREFS_H
#define MEM_CONSOLE_PREFS_H

#include <stddef.h>

#include "mem_console_state.h"

int mem_console_build_prefs_path(const char *db_path, char *out_path, size_t out_cap);
CoreResult mem_console_prefs_load(const char *prefs_path, MemConsoleState *state);
CoreResult mem_console_prefs_save(const char *prefs_path, const MemConsoleState *state);

#endif
