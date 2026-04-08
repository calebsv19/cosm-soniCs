#include "input/library_input.h"

#include "app_state.h"
#include "daw/data_paths.h"
#include "ui/library_browser.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "undo/undo_manager.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
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

static bool library_input_pick_folder_macos(char* out_path, size_t out_cap) {
#if defined(__APPLE__)
    FILE* pipe = NULL;
    char line[LIBRARY_PATH_MAX];
    if (!out_path || out_cap == 0u) {
        return false;
    }
    pipe = popen("/usr/bin/osascript -e 'POSIX path of (choose folder with prompt \"Choose DAW Library Input Folder\")'",
                 "r");
    if (!pipe) {
        return false;
    }
    if (!fgets(line, sizeof(line), pipe)) {
        (void)pclose(pipe);
        return false;
    }
    (void)pclose(pipe);
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '\0') {
        return false;
    }
    snprintf(out_path, out_cap, "%s", line);
    return true;
#else
    (void)out_path;
    (void)out_cap;
    return false;
#endif
}

static bool library_input_apply_directory(AppState* state, const char* directory) {
    if (!state || !directory || directory[0] == '\0') {
        return false;
    }
    snprintf(state->data_paths.input_root, sizeof(state->data_paths.input_root), "%s", directory);
    daw_data_paths_apply_runtime_policy(&state->data_paths);
    snprintf(state->library.directory, sizeof(state->library.directory), "%s", state->data_paths.input_root);
    library_browser_scan(&state->library, &state->media_registry);
    library_browser_refresh_project_usage(&state->library, state->engine);
    if (!daw_data_paths_save_runtime(&state->data_paths)) {
        SDL_Log("library_input: failed to persist data_paths runtime config after folder update");
    }
    return true;
}

static bool library_input_is_supported_audio(const char* path) {
    const char* dot = NULL;
    if (!path) {
        return false;
    }
    dot = strrchr(path, '.');
    if (!dot) {
        return false;
    }
    return strcasecmp(dot, ".wav") == 0 || strcasecmp(dot, ".mp3") == 0;
}

static void library_input_path_stem_and_ext(const char* file_name,
                                            char* out_stem,
                                            size_t out_stem_cap,
                                            char* out_ext,
                                            size_t out_ext_cap) {
    const char* dot = NULL;
    if (!out_stem || out_stem_cap == 0 || !out_ext || out_ext_cap == 0) {
        return;
    }
    out_stem[0] = '\0';
    out_ext[0] = '\0';
    if (!file_name || file_name[0] == '\0') {
        return;
    }
    dot = strrchr(file_name, '.');
    if (dot) {
        size_t stem_len = (size_t)(dot - file_name);
        if (stem_len >= out_stem_cap) {
            stem_len = out_stem_cap - 1;
        }
        memcpy(out_stem, file_name, stem_len);
        out_stem[stem_len] = '\0';
        snprintf(out_ext, out_ext_cap, "%s", dot);
        return;
    }
    snprintf(out_stem, out_stem_cap, "%s", file_name);
}

static bool library_input_copy_file_binary(const char* source_path, const char* dest_path) {
    FILE* in = NULL;
    FILE* out = NULL;
    char buffer[64 * 1024];
    size_t n = 0;
    if (!source_path || !dest_path) {
        return false;
    }
    in = fopen(source_path, "rb");
    if (!in) {
        return false;
    }
    out = fopen(dest_path, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return false;
        }
    }
    fclose(in);
    if (fclose(out) != 0) {
        return false;
    }
    return true;
}

static bool library_input_file_exists(const char* path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool library_input_build_collision_safe_path(const char* root,
                                                    const char* source_path,
                                                    char* out_path,
                                                    size_t out_cap) {
    const char* file_name = NULL;
    char stem[LIBRARY_NAME_MAX];
    char ext[32];
    if (!root || !source_path || !out_path || out_cap == 0) {
        return false;
    }
    file_name = strrchr(source_path, '/');
#if defined(_WIN32)
    {
        const char* back = strrchr(source_path, '\\');
        if (!file_name || (back && back > file_name)) {
            file_name = back;
        }
    }
#endif
    file_name = file_name ? file_name + 1 : source_path;
    if (!file_name || file_name[0] == '\0') {
        return false;
    }
    library_input_path_stem_and_ext(file_name, stem, sizeof(stem), ext, sizeof(ext));
    if (snprintf(out_path, out_cap, "%s/%s%s", root, stem, ext) >= (int)out_cap) {
        return false;
    }
    if (!library_input_file_exists(out_path)) {
        return true;
    }
    for (int i = 1; i < 10000; ++i) {
        if (snprintf(out_path, out_cap, "%s/%s_%d%s", root, stem, i, ext) >= (int)out_cap) {
            return false;
        }
        if (!library_input_file_exists(out_path)) {
            return true;
        }
    }
    return false;
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
    SDL_Rect content_rect = ui_layout_pane_content_rect(library_pane);
    int start_x = content_rect.x + 12;
    lib->edit_cursor = library_cursor_from_x(lib->edit_buffer, 1.0f, start_x, mouse_x);
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
        SDL_Rect content_rect = ui_layout_pane_content_rect(library_pane);
        int hit = library_browser_hit_test(lib, &content_rect, event->button.x, event->button.y);
        if (hit == lib->edit_index && event->button.button == SDL_BUTTON_LEFT) {
            int start_x = content_rect.x + 16;
            if (state->library.panel_mode == LIBRARY_PANEL_MODE_SOURCE) {
                start_x = content_rect.x + 12;
            }
            lib->edit_cursor = library_cursor_from_x(lib->edit_buffer, 1.0f, start_x, event->button.x);
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

bool library_input_open_folder_dialog(AppState* state) {
    char folder[LIBRARY_PATH_MAX];
    if (!state) {
        return false;
    }
    folder[0] = '\0';
    if (!library_input_pick_folder_macos(folder, sizeof(folder))) {
        SDL_Log("library_input: folder dialog canceled/unavailable");
        return false;
    }
    if (!library_input_apply_directory(state, folder)) {
        SDL_Log("library_input: failed to apply folder %s", folder);
        return false;
    }
    SDL_Log("library_input: input root set to %s", state->library.directory);
    return true;
}

bool library_input_handle_primary_click(AppState* state, int mouse_x, int mouse_y) {
    const Pane* library_pane = NULL;
    SDL_Rect header_rect;
    SDL_Rect content_rect;
    LibraryPanelMode mode = LIBRARY_PANEL_MODE_SOURCE;
    int hit = -1;
    if (!state) {
        return false;
    }
    library_pane = ui_layout_get_pane(state, 3);
    if (!library_pane) {
        return false;
    }
    header_rect = library_pane->rect;
    {
        int header_h = ui_layout_pane_header_height(library_pane);
        if (header_h < 0) {
            header_h = 0;
        }
        if (header_h < header_rect.h) {
            header_rect.h = header_h;
        }
    }
    content_rect = ui_layout_pane_content_rect(library_pane);

    if (header_rect.h > 0 &&
        library_browser_hit_test_mode_button(&state->library, &header_rect, mouse_x, mouse_y, &mode)) {
        library_browser_set_mode(&state->library, mode);
        library_browser_refresh_project_usage(&state->library, state->engine);
        return true;
    }

    if (!SDL_PointInRect(&(SDL_Point){mouse_x, mouse_y}, &content_rect)) {
        return false;
    }

    hit = library_browser_hit_test(&state->library, &content_rect, mouse_x, mouse_y);
    if (hit < 0) {
        return false;
    }
    if (state->library.panel_mode == LIBRARY_PANEL_MODE_IN_PROJECT) {
        state->library.selected_project_index = hit;
        return true;
    }
    return false;
}

bool library_input_handle_drop_file(AppState* state, const char* source_path, int drop_x, int drop_y) {
    const Pane* library_pane = NULL;
    SDL_Point p;
    char destination[LIBRARY_PATH_MAX];
    const char* copy_root = NULL;
    if (!state || !source_path || source_path[0] == '\0') {
        return false;
    }
    library_pane = ui_layout_get_pane(state, 3);
    if (!library_pane || !library_pane->visible) {
        return false;
    }
    p.x = drop_x;
    p.y = drop_y;
    if (!SDL_PointInRect(&p, &library_pane->rect)) {
        return false;
    }
    if (!library_input_is_supported_audio(source_path)) {
        SDL_Log("library_input: drop skipped (unsupported extension): %s", source_path);
        return true;
    }

    daw_data_paths_apply_runtime_policy(&state->data_paths);
    copy_root = state->data_paths.library_copy_root[0] != '\0'
                    ? state->data_paths.library_copy_root
                    : state->library.directory;
    if (!copy_root || copy_root[0] == '\0') {
        SDL_Log("library_input: drop failed (missing copy root)");
        return true;
    }
    if (!library_input_build_collision_safe_path(copy_root, source_path, destination, sizeof(destination))) {
        SDL_Log("library_input: drop failed (destination path unavailable)");
        return true;
    }
    if (!library_input_copy_file_binary(source_path, destination)) {
        SDL_Log("library_input: failed copying %s -> %s (errno=%d)", source_path, destination, errno);
        return true;
    }

    SDL_Log("library_input: imported %s -> %s", source_path, destination);
    library_browser_scan(&state->library, &state->media_registry);
    library_browser_refresh_project_usage(&state->library, state->engine);
    return true;
}
