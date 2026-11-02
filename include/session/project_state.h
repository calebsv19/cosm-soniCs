#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "kit_ui_text_edit.h"

#include "session.h"
#include "session/project_manager.h"
#include "session/project_collections.h"

// Tracks the current project identity and save path.
typedef struct {
    bool has_name;
    char persistence_notice[128]; // Persistent product acknowledgement; never serialized as authored state.
    DawSaveResult last_save_result; // Exact acknowledgement for the accepted project path.
    char name[SESSION_NAME_MAX];
    char path[SESSION_PATH_MAX];
} ProjectState;

// Tracks the active Save Project text-entry modal.
typedef struct {
    bool active;
    char error[128];
    char buffer[SESSION_PATH_MAX];
    bool folder_mode, resume_load;
    bool publication_warning; // Blocks accidental repeated publication until this warning is dismissed.
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
