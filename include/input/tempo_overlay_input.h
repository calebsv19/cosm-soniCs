#pragma once

#include <stdbool.h>

struct AppState;

// Begins an undoable tempo overlay edit session.
bool tempo_overlay_begin_edit(struct AppState* state);
// Commits the active tempo overlay edit session.
bool tempo_overlay_commit_edit(struct AppState* state);
// Cancels the active tempo overlay edit session.
void tempo_overlay_cancel_edit(struct AppState* state);
