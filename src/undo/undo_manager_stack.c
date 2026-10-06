#include "undo_manager_internal.h"
#include "input/timeline_selection.h"
#include "input/inspector_input.h"

#include <stdlib.h>
#include <string.h>

#define UNDO_DEFAULT_CAPACITY 32
#define UNDO_DEFAULT_LIMIT 256

// Retains a static user-facing rejection reason without allocating on the failure path.
bool undo_manager_reject(UndoManager* manager, const char* message) {
    if (manager) { manager->rejection_message = message; manager->rejection_ticks = SDL_GetTicks(); }
    return false;
}

// Exposes the most recent rejection for a bounded display interval, including timer wraparound.
const char* undo_manager_rejection_message(const UndoManager* manager) {
    return manager && manager->rejection_message && (Uint32)(SDL_GetTicks() - manager->rejection_ticks) < 5000 ?
        manager->rejection_message : NULL;
}

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
    manager->rejection_message = NULL;
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

// Reserves final history storage before the gesture is allowed to edit the project.
bool undo_manager_begin_drag(UndoManager* manager, const UndoCommand* command) {
    if (!manager || !command) return false;
    if (manager->active_drag_valid) return undo_manager_reject(manager, "Finish the current edit first.");
    if (!undo_stack_ensure(&manager->undo_stack, &manager->undo_capacity, manager->undo_count + 1) ||
        !undo_command_clone(&manager->active_drag, command))
        return undo_manager_reject(manager, "Edit not applied: undo history could not be reserved.");
    if (++manager->drag_serial == 0) ++manager->drag_serial;
    manager->active_drag_valid = true;
    return true;
}

// Transfers active history ownership without allocating a second copy at release.
bool undo_manager_commit_drag(UndoManager* manager, const UndoCommand* command) {
    if (!manager || !command) {
        return false;
    }
    if (manager->active_drag_valid && command == &manager->active_drag) {
        if (!undo_stack_ensure(&manager->undo_stack, &manager->undo_capacity, manager->undo_count + 1)) return false;
        manager->rejection_message = NULL;
        manager->undo_stack[manager->undo_count++] = manager->active_drag;
        memset(&manager->active_drag, 0, sizeof(manager->active_drag));
        manager->active_drag_valid = false;
        for (int i = 0; i < manager->redo_count; ++i) undo_command_destroy(&manager->redo_stack[i]);
        manager->redo_count = 0;
        if (manager->max_commands > 0 && manager->undo_count > manager->max_commands) {
            undo_command_destroy(&manager->undo_stack[0]);
            memmove(manager->undo_stack, manager->undo_stack + 1,
                    sizeof(UndoCommand) * (size_t)(--manager->undo_count));
        }
        return true;
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
    return manager && !manager->active_drag_valid && manager->undo_count > 0;
}

bool undo_manager_can_redo(const UndoManager* manager) {
    return manager && !manager->active_drag_valid && manager->redo_count > 0;
}

// Reserves destination storage and retains the source command until its edit has succeeded.
static bool undo_manager_step(UndoManager* manager, AppState* state, bool redo) {
    if (!manager || !state) return false;
    if (manager->active_drag_valid) return undo_manager_reject(manager, "Finish the current edit before undo or redo.");
    UndoCommand** source = redo ? &manager->redo_stack : &manager->undo_stack;
    int* source_count = redo ? &manager->redo_count : &manager->undo_count;
    UndoCommand** destination = redo ? &manager->undo_stack : &manager->redo_stack;
    int* destination_count = redo ? &manager->undo_count : &manager->redo_count;
    int* destination_capacity = redo ? &manager->undo_capacity : &manager->redo_capacity;
    if (*source_count <= 0 || !undo_stack_ensure(destination, destination_capacity, *destination_count + 1)) return false;
    uint64_t selected[TIMELINE_MAX_SELECTION] = {0};
    int selected_count = state->selection_count > TIMELINE_MAX_SELECTION ? TIMELINE_MAX_SELECTION : state->selection_count;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    uint64_t selected_track_id = tracks && state->selected_track_index >= 0 && state->selected_track_index < track_count ?
        tracks[state->selected_track_index].runtime_id : 0;
    for (int i = 0; i < selected_count; ++i) {
        TimelineSelectionEntry entry = state->selection[i];
        if (tracks && entry.track_index >= 0 && entry.track_index < track_count && entry.clip_index >= 0 &&
            entry.clip_index < tracks[entry.track_index].clip_count)
            selected[i] = tracks[entry.track_index].clips[entry.clip_index].creation_index;
    }
    UndoCommand* command = &(*source)[*source_count - 1];
    if (command->type == UNDO_CMD_CLIP_CONTENT) {
        const UndoClipContentSelection* selection = &command->data.clip_content_selection;
        if (selection->count < 0 || selection->count > TIMELINE_MAX_SELECTION) return false;
        selected_count = selection->count;
        memcpy(selected, redo ? selection->after : selection->before, (size_t)selected_count * sizeof(*selected));
    }
    if (!undo_apply(state, command, redo))
        return undo_manager_reject(manager, "Undo/redo not applied: target changed or preparation failed.");
    manager->rejection_message = NULL;
    (*destination)[(*destination_count)++] = *command;
    --*source_count;
    // Restore selection by identity only after the project and history have accepted the operation.
    tracks = engine_get_tracks(state->engine); track_count = engine_get_track_count(state->engine);
    timeline_selection_clear(state);
    for (int i = 0; i < selected_count; ++i) if (selected[i]) {
        for (int t = 0; t < track_count; ++t)
            for (int c = 0; c < tracks[t].clip_count; ++c)
                if (tracks[t].clips[c].creation_index == selected[i]) timeline_selection_add(state, t, c);
    }
    if (state->selection_count > 0) {
        timeline_selection_set_primary(state, state->selection[0].track_index, state->selection[0].clip_index);
        inspector_input_set_clip(state, state->selection[0].track_index, state->selection[0].clip_index);
    } else {
        for (int t = 0; t < track_count; ++t) {
            if (tracks[t].runtime_id == selected_track_id) { timeline_selection_add(state, t, -1); break; }
        }
        inspector_input_init(state);
    }
    return true;
}

// Applies the newest undo command without consuming history on rejection.
bool undo_manager_undo(UndoManager* manager, AppState* state) {
    return undo_manager_step(manager, state, false);
}

// Reapplies the newest redo command without consuming history on rejection.
bool undo_manager_redo(UndoManager* manager, AppState* state) {
    return undo_manager_step(manager, state, true);
}
