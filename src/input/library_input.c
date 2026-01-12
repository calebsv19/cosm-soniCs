#include "input/library_input.h"

#include "app_state.h"
#include "ui/library_browser.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "undo/undo_manager.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int library_cursor_from_x(const char* text, float scale, int start_x, int mouse_x) {
    if (!text) return 0;
    int len = (int)strlen(text);
    int rel = mouse_x - start_x;
    if (rel <= 0) return 0;
    int cursor = len;
    char temp[LIBRARY_NAME_MAX];
    for (int i = 0; i <= len; ++i) {
        snprintf(temp, sizeof(temp), "%.*s", i, text);
        int w = ui_measure_text_width(temp, scale);
        if (w >= rel) {
            cursor = i;
            break;
        }
    }
    if (cursor < 0) cursor = 0;
    if (cursor > len) cursor = len;
    return cursor;
}

static void library_edit_stop(LibraryBrowser* lib) {
    if (!lib) return;
    lib->editing = false;
    lib->edit_index = -1;
    lib->edit_buffer[0] = '\0';
    lib->edit_cursor = 0;
    SDL_StopTextInput();
}

static void library_edit_commit(AppState* state) {
    if (!state || !state->library.editing) return;
    LibraryBrowser* lib = &state->library;
    if (lib->edit_index < 0 || lib->edit_index >= lib->count) {
        library_edit_stop(lib);
        return;
    }
    const char* old_name = lib->items[lib->edit_index].name;
    const char* new_name = lib->edit_buffer;
    char old_name_copy[LIBRARY_NAME_MAX];
    char new_name_copy[LIBRARY_NAME_MAX];
    strncpy(old_name_copy, old_name, sizeof(old_name_copy) - 1);
    old_name_copy[sizeof(old_name_copy) - 1] = '\0';
    strncpy(new_name_copy, new_name, sizeof(new_name_copy) - 1);
    new_name_copy[sizeof(new_name_copy) - 1] = '\0';
    if (!new_name || new_name[0] == '\0' || strcmp(old_name, new_name) == 0) {
        library_edit_stop(lib);
        return;
    }
    char old_path[512];
    char new_path[512];
    snprintf(old_path, sizeof(old_path), "%s/%s", lib->directory, old_name);
    snprintf(new_path, sizeof(new_path), "%s/%s", lib->directory, new_name);
    struct stat st;
    if (stat(new_path, &st) == 0) {
        SDL_Log("Rename failed: target exists (%s)", new_name);
        library_edit_stop(lib);
        return;
    }
    if (rename(old_path, new_path) != 0) {
        SDL_Log("Rename failed from %s to %s", old_name, new_name);
        library_edit_stop(lib);
        return;
    }
    SDL_Log("Renamed %s -> %s", old_name_copy, new_name_copy);
    UndoCommand cmd = {0};
    cmd.type = UNDO_CMD_LIBRARY_RENAME;
    strncpy(cmd.data.library_rename.directory, lib->directory,
            sizeof(cmd.data.library_rename.directory) - 1);
    cmd.data.library_rename.directory[sizeof(cmd.data.library_rename.directory) - 1] = '\0';
    strncpy(cmd.data.library_rename.before_name, old_name_copy,
            sizeof(cmd.data.library_rename.before_name) - 1);
    cmd.data.library_rename.before_name[sizeof(cmd.data.library_rename.before_name) - 1] = '\0';
    strncpy(cmd.data.library_rename.after_name, new_name_copy,
            sizeof(cmd.data.library_rename.after_name) - 1);
    cmd.data.library_rename.after_name[sizeof(cmd.data.library_rename.after_name) - 1] = '\0';
    undo_manager_push(&state->undo, &cmd);
    if (lib->edit_index >= 0 && lib->edit_index < lib->count) {
        const char* id = lib->items[lib->edit_index].media_id;
        if (id && id[0] != '\0') {
            media_registry_update_path(&state->media_registry, id, new_path, new_name_copy);
            media_registry_save(&state->media_registry);
        }
    }
    library_edit_stop(lib);
    library_browser_scan(lib, &state->media_registry);
}

bool library_input_is_editing(const AppState* state) {
    return state && state->library.editing;
}

void library_input_stop_edit(AppState* state) {
    if (!state) return;
    library_edit_stop(&state->library);
}

void library_input_start_edit(AppState* state, const Pane* library_pane, int mouse_x) {
    if (!state || !library_pane) return;
    LibraryBrowser* lib = &state->library;
    if (lib->hovered_index < 0 || lib->hovered_index >= lib->count) {
        return;
    }
    lib->editing = true;
    lib->edit_index = lib->hovered_index;
    strncpy(lib->edit_buffer, lib->items[lib->edit_index].name, sizeof(lib->edit_buffer) - 1);
    lib->edit_buffer[sizeof(lib->edit_buffer) - 1] = '\0';
    int start_x = library_pane->rect.x + 16;
    lib->edit_cursor = library_cursor_from_x(lib->edit_buffer, 1.5f, start_x, mouse_x);
    SDL_StartTextInput();
}

bool library_input_handle_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    (void)manager;
    if (!state || !event) return false;
    LibraryBrowser* lib = &state->library;
    if (!lib->editing) return false;

    switch (event->type) {
    case SDL_TEXTINPUT: {
        const char* text = event->text.text;
        if (!text) break;
        int len = (int)strlen(lib->edit_buffer);
        int insert_len = (int)strlen(text);
        if (insert_len <= 0) break;
        if (len + insert_len >= (int)sizeof(lib->edit_buffer)) {
            insert_len = (int)sizeof(lib->edit_buffer) - 1 - len;
            if (insert_len <= 0) break;
        }
        memmove(lib->edit_buffer + lib->edit_cursor + insert_len,
                lib->edit_buffer + lib->edit_cursor,
                (size_t)(len - lib->edit_cursor + 1));
        memcpy(lib->edit_buffer + lib->edit_cursor, text, (size_t)insert_len);
        lib->edit_cursor += insert_len;
        return true;
    }
    case SDL_KEYDOWN: {
        SDL_Keycode key = event->key.keysym.sym;
        int len = (int)strlen(lib->edit_buffer);
        if (key == SDLK_LEFT) {
            if (lib->edit_cursor > 0) lib->edit_cursor--;
            return true;
        }
        if (key == SDLK_RIGHT) {
            if (lib->edit_cursor < len) lib->edit_cursor++;
            return true;
        }
        if (key == SDLK_BACKSPACE) {
            if (lib->edit_cursor > 0 && len > 0) {
                memmove(lib->edit_buffer + lib->edit_cursor - 1,
                        lib->edit_buffer + lib->edit_cursor,
                        (size_t)(len - lib->edit_cursor + 1));
                lib->edit_cursor--;
            }
            return true;
        }
        if (key == SDLK_DELETE) {
            if (lib->edit_cursor < len) {
                memmove(lib->edit_buffer + lib->edit_cursor,
                        lib->edit_buffer + lib->edit_cursor + 1,
                        (size_t)(len - lib->edit_cursor));
            }
            return true;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            library_edit_commit(state);
            return true;
        }
        if (key == SDLK_ESCAPE) {
            library_edit_stop(lib);
            return true;
        }
        break;
    }
    case SDL_MOUSEBUTTONDOWN: {
        const Pane* library_pane = ui_layout_get_pane(state, 3);
        if (!library_pane) break;
        int line_height = 20;
        int hit = library_browser_hit_test(lib, &library_pane->rect, event->button.x, event->button.y, line_height);
        if (hit == lib->edit_index && event->button.button == SDL_BUTTON_LEFT) {
            int start_x = library_pane->rect.x + 16;
            lib->edit_cursor = library_cursor_from_x(lib->edit_buffer, 1.5f, start_x, event->button.x);
            return true;
        } else if (event->button.button == SDL_BUTTON_LEFT) {
            library_edit_stop(lib);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}
