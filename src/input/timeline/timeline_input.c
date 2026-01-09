#include "input/timeline_input.h"

#include "app_state.h"
#include "engine/engine.h"
#include "input/input_manager.h"
#include "input/library_input.h"
#include "input/timeline/timeline_input_keyboard.h"
#include "input/timeline/timeline_input_mouse.h"
#include "ui/effects_panel.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

void track_name_editor_stop(AppState* state, bool commit) {
    if (!state) {
        return;
    }
    TrackNameEditor* editor = &state->track_name_editor;
    if (!editor->editing) {
        return;
    }
    SDL_Log("track_name_editor_stop (commit=%d, track=%d)", commit ? 1 : 0, editor->track_index);
    if (commit && state->engine && editor->track_index >= 0) {
        int track_count = engine_get_track_count(state->engine);
        if (editor->track_index < track_count) {
            engine_track_set_name(state->engine, editor->track_index, editor->buffer);
            effects_panel_sync_from_engine(state);
        }
    }
    editor->editing = false;
    editor->track_index = -1;
    editor->buffer[0] = '\0';
    editor->cursor = 0;
    SDL_StopTextInput();
}

void track_name_editor_start(AppState* state, int track_index) {
    if (!state || !state->engine || track_index < 0) {
        return;
    }
    track_name_editor_stop(state, true);
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || track_index >= track_count) {
        return;
    }
    const EngineTrack* track = &tracks[track_index];
    TrackNameEditor* editor = &state->track_name_editor;
    editor->editing = true;
    editor->track_index = track_index;
    const char* source = track->name[0] ? track->name : NULL;
    char temp[ENGINE_CLIP_NAME_MAX];
    if (!source) {
        snprintf(temp, sizeof(temp), "Track %d", track_index + 1);
        source = temp;
    }
    strncpy(editor->buffer, source, sizeof(editor->buffer) - 1);
    editor->buffer[sizeof(editor->buffer) - 1] = '\0';
    editor->cursor = (int)strlen(editor->buffer);
    SDL_StartTextInput();
}

void timeline_input_init(InputManager* manager) {
    if (!manager) {
        return;
    }
    manager->last_click_ticks = 0;
    manager->last_click_clip = -1;
    manager->last_click_track = -1;
    manager->last_header_click_ticks = 0;
    manager->last_header_click_track = -1;
}

void timeline_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    if (!manager || !state || !event || !state->engine) {
        return;
    }

    if (library_input_handle_event(manager, state, event)) {
        return;
    }

    if (timeline_input_keyboard_handle_event(manager, state, event)) {
        return;
    }

    timeline_input_mouse_handle_event(manager, state, event);
}

void timeline_input_update(InputManager* manager, AppState* state, bool was_down, bool is_down) {
    timeline_input_mouse_update(manager, state, was_down, is_down);
}
