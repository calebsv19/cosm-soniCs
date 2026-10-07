#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "kit_ui_text_edit.h"

#include "session.h"
#include "session/project_manager.h"

// Tracks the current project identity and save path.
typedef struct {
    bool has_name;
    char name[SESSION_NAME_MAX];
    char path[SESSION_PATH_MAX];
} ProjectState;

// Tracks the active Save Project text-entry modal.
typedef struct {
    bool active;
    char error[128];
    char buffer[SESSION_NAME_MAX];
    int cursor;
    KitUiTextEdit text_edit;
} ProjectSavePrompt;

// Tracks the active Load Project modal list and selection state.
typedef struct {
    bool active;
    char error[128];
    ProjectInfo entries[64];
    int count;
    int selected_index;
    float scroll_offset;
    Uint32 last_click_ticks;
    int last_click_index;
} ProjectLoadModal;
