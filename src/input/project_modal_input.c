#include "input/project_modal_input.h"

#include "app_state.h"
#include "ui/text_edit.h"
#include "ui/project_modal_controls.h"
#include "ui/transport_controls.h"
#include "session/project_manager.h"

#include <SDL2/SDL.h>
#include <string.h>

bool project_modal_input_active(const AppState* state) {
    return state && (state->project_prompt.active || state->project_load.active);
}

void project_modal_input_open_save_prompt(AppState* state) {
    if (!state) {
        return;
    }
    if (project_modal_input_active(state)) return;
    daw_text_cancel_composition(state);
    state->project_prompt.active = true;
    state->project_prompt.error[0] = '\0';
    state->project_prompt.buffer[0] = '\0';
    state->project_prompt.cursor = 0;
    daw_text_edit_begin(&state->project_prompt.text_edit, state->project_prompt.buffer,
        sizeof(state->project_prompt.buffer), &state->project_prompt.cursor, KIT_UI_TEXT_SINGLE_LINE);
    daw_transport_controls_sync(&state->transport_ui, state);
    SDL_StartTextInput();
}

void project_modal_input_close_save_prompt(AppState* state) {
    if (!state) {
        return;
    }
    state->project_prompt.active = false;
    kit_ui_text_cancel_composition(&state->project_prompt.text_edit);
    state->project_prompt.buffer[0] = '\0';
    state->project_prompt.cursor = 0;
    daw_transport_controls_sync(&state->transport_ui, state);
    daw_text_resume_input(state);
}

static bool project_prompt_handle_event(AppState* state, const SDL_Event* event) {
    if (!state || !event || !state->project_prompt.active) {
        return false;
    }
    ProjectSavePrompt* prompt = &state->project_prompt;
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int width = state->window_width > 0 ? state->window_width : 800;
        int height = state->window_height > 0 ? state->window_height : 600;
        SDL_Rect field = {(width - 480) / 2 + 16, (height - 180) / 2 + 60, 448, 44};
        SDL_Point point = {event->button.x,event->button.y};
        if (SDL_PointInRect(&point,&field)) {
            daw_text_edit_click(&prompt->text_edit,prompt->buffer,sizeof(prompt->buffer),
                &prompt->cursor,KIT_UI_TEXT_SINGLE_LINE,2,field.x + 8,field.w - 16,point.x);
            return true;
        }
    }
    KitUiTextEventResult result = daw_text_edit_event(&prompt->text_edit, prompt->buffer,
        sizeof(prompt->buffer), &prompt->cursor, KIT_UI_TEXT_SINGLE_LINE, event);
    if (result.cancel) project_modal_input_close_save_prompt(state);
    if (result.submit) {
        const char* name = prompt->buffer[0] ? prompt->buffer : "project";
        if (project_manager_save(state, name, true)) project_modal_input_close_save_prompt(state);
        else SDL_strlcpy(prompt->error, "Save failed. Retry or Esc to cancel.", sizeof(prompt->error));
    }
    return result.consumed != 0;
}

void project_modal_input_close_load_modal(AppState* state) {
    if (!state) {
        return;
    }
    state->project_load.active = false;
    state->project_load.count = 0;
    state->project_load.selected_index = -1;
    daw_project_modal_controls_sync(state);
    daw_transport_controls_sync(&state->transport_ui, state);
    daw_text_resume_input(state);
}

static void project_load_clamp_scroll(ProjectLoadModal* modal, int item_height, int view_height) {
    if (!modal) {
        return;
    }
    float max_scroll = (float)(modal->count * item_height - view_height);
    if (max_scroll < 0.0f) {
        max_scroll = 0.0f;
    }
    if (modal->scroll_offset < 0.0f) {
        modal->scroll_offset = 0.0f;
    }
    if (modal->scroll_offset > max_scroll) {
        modal->scroll_offset = max_scroll;
    }
}

bool project_modal_input_open_load_modal(AppState* state) {
    if (!state) {
        return false;
    }
    if (project_modal_input_active(state)) return false;
    daw_text_cancel_composition(state);
    SDL_StopTextInput();
    state->project_load.active = true;
    state->project_load.error[0] = '\0';
    state->project_load.scroll_offset = 0.0f;
    state->project_load.selected_index = -1;
    state->project_load.last_click_index = -1;
    state->project_load.last_click_ticks = 0;
    daw_project_modal_controls_sync(state);
    daw_transport_controls_sync(&state->transport_ui, state);

    int count = 0;
    project_manager_list(state,
                         state->project_load.entries,
                         (int)(sizeof(state->project_load.entries) / sizeof(state->project_load.entries[0])),
                         &count);
    state->project_load.count = count;
    if (count <= 0) {
        SDL_Log("No project to load in output-root project lanes");
        return false;
    }

    int match = -1;
    for (int i = 0; i < count; ++i) {
        if (state->project.path[0] && strcmp(state->project.path, state->project_load.entries[i].path) == 0) {
            match = i;
            break;
        }
    }
    state->project_load.selected_index = match >= 0 ? match : 0;
    return true;
}

static void project_load_selected(AppState* state, int selected_index) {
    if (!state || selected_index < 0 || selected_index >= state->project_load.count) {
        return;
    }
    if (project_manager_load(state, state->project_load.entries[selected_index].path)) {
        project_manager_post_load(state);
        project_modal_input_close_load_modal(state);
    } else {
        SDL_strlcpy(state->project_load.error, "Load failed. Choose another project or retry.", sizeof(state->project_load.error));
    }
}

static bool project_load_handle_event(AppState* state, const SDL_Event* event) {
    if (!state || !state->project_load.active || !event) {
        return false;
    }
    ProjectLoadModal* modal = &state->project_load;

    int width = state->window_width > 0 ? state->window_width : 800;
    int height = state->window_height > 0 ? state->window_height : 600;
    SDL_Rect box = {(width - 720) / 2, (height - 420) / 2, 720, 420};
    SDL_Rect list_rect = {box.x + 16, box.y + 56, box.w / 2 - 32, box.h - 96};

    int item_h = 28;
    project_load_clamp_scroll(modal, item_h, list_rect.h);

    if (!(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE)) {
        int action;
        if (daw_project_modal_controls_event(state, event, &action)) {
            if (action == 1) project_load_selected(state, modal->selected_index);
            if (action == 2) project_modal_input_close_load_modal(state);
            return true;
        }
    }

    switch (event->type) {
    case SDL_MOUSEWHEEL:
        modal->scroll_offset -= (float)event->wheel.y * (float)item_h * 2.0f;
        project_load_clamp_scroll(modal, item_h, list_rect.h);
        return true;
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            SDL_Point p = {event->button.x, event->button.y};
            Uint32 now = SDL_GetTicks();
            if (SDL_PointInRect(&p, &list_rect)) {
                int local_y = p.y - list_rect.y;
                int idx = (int)((local_y + (int)modal->scroll_offset) / item_h);
                if (idx >= 0 && idx < modal->count) {
                    if (modal->last_click_index == idx && (now - modal->last_click_ticks) <= 350) {
                        project_load_selected(state, idx);
                        return true;
                    }
                    modal->selected_index = idx;
                    modal->last_click_index = idx;
                    modal->last_click_ticks = now;
                }
                return true;
            }

        }
        break;
    case SDL_KEYDOWN:
        if (event->key.repeat) return true;
        if (event->key.keysym.sym == SDLK_ESCAPE) {
            project_modal_input_close_load_modal(state);
            return true;
        }
        if (event->key.keysym.sym == SDLK_RETURN || event->key.keysym.sym == SDLK_KP_ENTER) {
            project_load_selected(state, modal->selected_index);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

// Keeps every modal event isolated from timeline shortcuts, including unhandled letter keydowns.
bool project_modal_input_handle_event(AppState* state, const SDL_Event* event) {
    if (!state || !event) {
        return false;
    }
    if (state->project_load.active) {
        (void)project_load_handle_event(state, event);
        return true;
    }
    if (state->project_prompt.active) {
        (void)project_prompt_handle_event(state, event);
        return true;
    }
    return false;
}
