#include "undo_manager_internal.h"

#include <stdlib.h>
#include <string.h>

#define UNDO_DEFAULT_CAPACITY 32
#define UNDO_DEFAULT_LIMIT 256

static bool undo_stack_ensure(UndoCommand** stack, int* capacity, int count_needed) {
    if (!stack || !capacity) {
        return false;
    }
    if (*capacity >= count_needed) {
        return true;
    }
    int next = (*capacity > 0) ? *capacity : UNDO_DEFAULT_CAPACITY;
    while (next < count_needed) {
        next *= 2;
    }
    UndoCommand* resized = (UndoCommand*)realloc(*stack, sizeof(UndoCommand) * (size_t)next);
    if (!resized) {
        return false;
    }
    *stack = resized;
    *capacity = next;
    return true;
}

void undo_manager_init(UndoManager* manager) {
    if (!manager) {
        return;
    }
    memset(manager, 0, sizeof(*manager));
    manager->max_commands = UNDO_DEFAULT_LIMIT;
}

void undo_manager_free(UndoManager* manager) {
    if (!manager) {
        return;
    }
    undo_manager_clear(manager);
    free(manager->undo_stack);
    free(manager->redo_stack);
    manager->undo_stack = NULL;
    manager->redo_stack = NULL;
    manager->undo_capacity = 0;
    manager->redo_capacity = 0;
}

void undo_manager_clear(UndoManager* manager) {
    if (!manager) {
        return;
    }
    for (int i = 0; i < manager->undo_count; ++i) {
        undo_command_destroy(&manager->undo_stack[i]);
    }
    for (int i = 0; i < manager->redo_count; ++i) {
        undo_command_destroy(&manager->redo_stack[i]);
    }
    manager->undo_count = 0;
    manager->redo_count = 0;
    if (manager->active_drag_valid) {
        undo_command_destroy(&manager->active_drag);
        manager->active_drag_valid = false;
    }
}

void undo_manager_set_limit(UndoManager* manager, int max_commands) {
    if (!manager) {
        return;
    }
    manager->max_commands = max_commands;
}

bool undo_manager_push(UndoManager* manager, const UndoCommand* command) {
    if (!manager || !command) {
        return false;
    }
    if (!undo_stack_ensure(&manager->undo_stack, &manager->undo_capacity, manager->undo_count + 1)) {
        return false;
    }
    UndoCommand* slot = &manager->undo_stack[manager->undo_count];
    if (!undo_command_clone(slot, command)) {
        return false;
    }
    manager->undo_count += 1;
    for (int i = 0; i < manager->redo_count; ++i) {
        undo_command_destroy(&manager->redo_stack[i]);
    }
    manager->redo_count = 0;
    if (manager->max_commands > 0 && manager->undo_count > manager->max_commands) {
        undo_command_destroy(&manager->undo_stack[0]);
        memmove(manager->undo_stack, manager->undo_stack + 1, sizeof(UndoCommand) * (size_t)(manager->undo_count - 1));
        manager->undo_count -= 1;
    }
    return true;
}

bool undo_manager_begin_drag(UndoManager* manager, const UndoCommand* command) {
    if (!manager || !command) {
        return false;
    }
    if (manager->active_drag_valid) {
        undo_command_destroy(&manager->active_drag);
        manager->active_drag_valid = false;
    }
    if (!undo_command_clone(&manager->active_drag, command)) {
        return false;
    }
    manager->active_drag_valid = true;
    return true;
}

bool undo_manager_commit_drag(UndoManager* manager, const UndoCommand* command) {
    if (!manager || !command) {
        return false;
    }
    bool pushed = undo_manager_push(manager, command);
    if (manager->active_drag_valid) {
        undo_command_destroy(&manager->active_drag);
        manager->active_drag_valid = false;
    }
    return pushed;
}

void undo_manager_cancel_drag(UndoManager* manager) {
    if (!manager || !manager->active_drag_valid) {
        return;
    }
    undo_command_destroy(&manager->active_drag);
    manager->active_drag_valid = false;
}

bool undo_manager_can_undo(const UndoManager* manager) {
    return manager && manager->undo_count > 0;
}

bool undo_manager_can_redo(const UndoManager* manager) {
    return manager && manager->redo_count > 0;
}

bool undo_manager_undo(UndoManager* manager, AppState* state) {
    if (!manager || !state || manager->undo_count <= 0) {
        return false;
    }
    UndoCommand cmd = manager->undo_stack[manager->undo_count - 1];
    manager->undo_count -= 1;
    bool ok = undo_apply(state, &cmd, false);
    if (!undo_stack_ensure(&manager->redo_stack, &manager->redo_capacity, manager->redo_count + 1)) {
        undo_command_destroy(&cmd);
        return ok;
    }
    manager->redo_stack[manager->redo_count++] = cmd;
    return ok;
}

bool undo_manager_redo(UndoManager* manager, AppState* state) {
    if (!manager || !state || manager->redo_count <= 0) {
        return false;
    }
    UndoCommand cmd = manager->redo_stack[manager->redo_count - 1];
    manager->redo_count -= 1;
    bool ok = undo_apply(state, &cmd, true);
    if (!undo_stack_ensure(&manager->undo_stack, &manager->undo_capacity, manager->undo_count + 1)) {
        undo_command_destroy(&cmd);
        return ok;
    }
    manager->undo_stack[manager->undo_count++] = cmd;
    return ok;
}
