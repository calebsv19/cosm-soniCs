#pragma once

#include "app_state.h"
#include "undo/undo_manager.h"

#include <stdbool.h>

bool undo_command_clone(UndoCommand* dst, const UndoCommand* src);
void undo_command_destroy(UndoCommand* cmd);
bool undo_apply(AppState* state, UndoCommand* command, bool apply_after);
