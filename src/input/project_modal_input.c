#include "input/project_modal_input.h"

#include "app_state.h"
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
    state->project_prompt.active = true;
    state->project_prompt.buffer[0] = '\0';
    state->project_prompt.cursor = 0;
    SDL_StartTextInput();
}

void project_modal_input_close_save_prompt(AppState* state) {
    if (!state) {
        return;
    }
    state->project_prompt.active = false;
    state->project_prompt.buffer[0] = '\0';
    state->project_prompt.cursor = 0;
    SDL_StopTextInput();
}

static bool project_prompt_handle_event(AppState* state, const SDL_Event* event) {
    if (!state || !event || !state->project_prompt.active) {
        return false;
    }
    ProjectSavePrompt* prompt = &state->project_prompt;
    switch (event->type) {
    case SDL_TEXTINPUT: {
        const char* txt = event->text.text;
        int len = (int)strlen(prompt->buffer);
        int cur = prompt->cursor;
        if (cur < 0) {
            cur = 0;
        }
        if (cur > len) {
            cur = len;
        }
        for (const char* p = txt; *p; ++p) {
            if ((int)strlen(prompt->buffer) >= (int)sizeof(prompt->buffer) - 1) {
                break;
            }
            memmove(prompt->buffer + cur + 1, prompt->buffer + cur, strlen(prompt->buffer + cur) + 1);
            prompt->buffer[cur] = *p;
            cur++;
        }
        prompt->cursor = cur;
        return true;
    }
    case SDL_KEYDOWN: {
        SDL_Keycode key = event->key.keysym.sym;
        if (key == SDLK_BACKSPACE) {
            int len = (int)strlen(prompt->buffer);
            int cur = prompt->cursor;
            if (cur > 0 && len > 0) {
                memmove(prompt->buffer + cur - 1, prompt->buffer + cur, (size_t)(len - cur + 1));
                prompt->cursor = cur - 1;
            }
            return true;
        }
        if (key == SDLK_LEFT) {
            if (prompt->cursor > 0) {
                prompt->cursor--;
            }
            return true;
        }
        if (key == SDLK_RIGHT) {
            int len = (int)strlen(prompt->buffer);
            if (prompt->cursor < len) {
                prompt->cursor++;
            }
            return true;
        }
        if (key == SDLK_ESCAPE) {
            project_modal_input_close_save_prompt(state);
            return true;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            const char* name = prompt->buffer[0] ? prompt->buffer : "project";
            project_manager_save(state, name, true);
            project_modal_input_close_save_prompt(state);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void project_modal_input_close_load_modal(AppState* state) {
    if (!state) {
        return;
    }
    state->project_load.active = false;
    state->project_load.count = 0;
    state->project_load.selected_index = -1;
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
    state->project_load.active = true;
    state->project_load.scroll_offset = 0.0f;
    state->project_load.selected_index = -1;
    state->project_load.last_click_index = -1;
    state->project_load.last_click_ticks = 0;

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
    }
    project_modal_input_close_load_modal(state);
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
    SDL_Rect info_rect = {box.x + box.w / 2 + 8, box.y + 56, box.w / 2 - 24, box.h - 126};
    SDL_Rect load_button = {info_rect.x, box.y + box.h - 52, 120, 36};
    SDL_Rect cancel_button = {load_button.x + load_button.w + 12, load_button.y, 120, 36};

    int item_h = 28;
    project_load_clamp_scroll(modal, item_h, list_rect.h);

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
            if (SDL_PointInRect(&p, &load_button)) {
                project_load_selected(state, modal->selected_index);
                return true;
            }
            if (SDL_PointInRect(&p, &cancel_button)) {
                project_modal_input_close_load_modal(state);
                return true;
            }
        }
        break;
    case SDL_KEYDOWN:
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

bool project_modal_input_handle_event(AppState* state, const SDL_Event* event) {
    if (!state || !event) {
        return false;
    }
    if (state->project_load.active) {
        return project_load_handle_event(state, event);
    }
    if (state->project_prompt.active) {
        return project_prompt_handle_event(state, event);
    }
    return false;
}
