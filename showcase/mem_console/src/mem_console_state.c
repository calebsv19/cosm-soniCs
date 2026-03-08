#include "mem_console_state.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

const char *k_mem_console_default_db_path =
    "shared/showcase/mem_console/demo/demo_mem_console.sqlite";

static int path_is_directory(const char *path) {
    struct stat st;
    if (!path || !path[0]) {
        return 0;
    }
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static const CoreThemePresetId k_mem_console_theme_cycle_order[] = {
    CORE_THEME_PRESET_DAW_DEFAULT,
    CORE_THEME_PRESET_MAP_FORGE_DEFAULT,
    CORE_THEME_PRESET_DARK_DEFAULT,
    CORE_THEME_PRESET_LIGHT_DEFAULT,
    CORE_THEME_PRESET_IDE_GRAY,
    CORE_THEME_PRESET_GREYSCALE
};

static const CoreFontPresetId k_mem_console_font_cycle_order[] = {
    CORE_FONT_PRESET_DAW_DEFAULT,
    CORE_FONT_PRESET_IDE
};

int resolve_default_db_path(char *out_path, size_t out_cap) {
    const char *env_db_path = 0;
    const char *home_path = 0;
    char data_dir[1024];
    char *base_path = 0;
    int written = 0;

    if (!out_path || out_cap == 0u) {
        return 0;
    }

    out_path[0] = '\0';
    env_db_path = getenv("CODEWORK_MEMDB_PATH");
    if (env_db_path && env_db_path[0]) {
        written = snprintf(out_path, out_cap, "%s", env_db_path);
        return written > 0 && (size_t)written < out_cap;
    }

    home_path = getenv("HOME");
    if (home_path && home_path[0]) {
        written = snprintf(data_dir, sizeof(data_dir), "%s/Desktop/CodeWork/data", home_path);
        if (written > 0 && (size_t)written < sizeof(data_dir) && path_is_directory(data_dir)) {
            written = snprintf(out_path, out_cap, "%s/codework_mem_console.sqlite", data_dir);
            if (written > 0 && (size_t)written < out_cap) {
                return 1;
            }
        }
    }

    base_path = SDL_GetBasePath();
    if (!base_path) {
        written = snprintf(out_path, out_cap, "%s", k_mem_console_default_db_path);
        return written > 0 && (size_t)written < out_cap;
    }

    written = snprintf(out_path, out_cap, "%s../demo/demo_mem_console.sqlite", base_path);
    SDL_free(base_path);
    if (written <= 0 || (size_t)written >= out_cap) {
        return 0;
    }

    return 1;
}

void print_usage(const char *argv0) {
    fprintf(stderr, "usage: %s [--db <path>] [--kernel-bridge]\n", argv0);
}

const char *find_flag_value(int argc, char **argv, const char *flag) {
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], flag) == 0) {
            if ((i + 1) >= argc) {
                return 0;
            }
            return argv[i + 1];
        }
    }

    return 0;
}

int has_flag(int argc, char **argv, const char *flag) {
    int i;

    if (!flag) {
        return 0;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], flag) == 0) {
            return 1;
        }
    }

    return 0;
}

int has_unknown_flag(int argc, char **argv) {
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--db") == 0) {
            if ((i + 1) < argc) {
                i += 1;
                continue;
            }
            return 1;
        }
        if (strcmp(argv[i], "--kernel-bridge") == 0) {
            continue;
        }
        return 1;
    }

    return 0;
}

void set_default_detail(MemConsoleState *state) {
    if (!state) {
        return;
    }

    state->selected_item_id = 0;
    state->selected_pinned = 0;
    state->selected_canonical = 0;
    (void)snprintf(state->selected_title,
                   sizeof(state->selected_title),
                   "No Matching Memory");
    (void)snprintf(state->selected_body,
                   sizeof(state->selected_body),
                   "Type to filter, or use mem_cli add to create records first.");
}

void copy_core_str(CoreStr value, char *out_text, size_t out_cap) {
    size_t copy_len;

    if (!out_text || out_cap == 0u) {
        return;
    }

    out_text[0] = '\0';
    if (!value.data || value.len == 0u) {
        return;
    }

    copy_len = value.len;
    if (copy_len >= out_cap) {
        copy_len = out_cap - 1u;
    }

    memcpy(out_text, value.data, copy_len);
    out_text[copy_len] = '\0';
}

static void sync_theme_name(MemConsoleState *state) {
    const char *name;

    if (!state) {
        return;
    }

    name = core_theme_preset_name(state->theme_preset_id);
    if (!name || !name[0]) {
        name = "unknown";
    }
    (void)snprintf(state->theme_name, sizeof(state->theme_name), "%s", name);
}

int cycle_theme_preset(MemConsoleState *state, int direction) {
    size_t i;
    size_t count;

    if (!state) {
        return 0;
    }

    count = sizeof(k_mem_console_theme_cycle_order) / sizeof(k_mem_console_theme_cycle_order[0]);
    if (count == 0u) {
        return 0;
    }

    for (i = 0; i < count; ++i) {
        if (k_mem_console_theme_cycle_order[i] == state->theme_preset_id) {
            if (direction >= 0) {
                state->theme_preset_id = k_mem_console_theme_cycle_order[(i + 1u) % count];
            } else {
                state->theme_preset_id = k_mem_console_theme_cycle_order[(i + count - 1u) % count];
            }
            sync_theme_name(state);
            return 1;
        }
    }

    state->theme_preset_id = k_mem_console_theme_cycle_order[0];
    sync_theme_name(state);
    return 1;
}

int state_set_theme_preset(MemConsoleState *state, CoreThemePresetId preset_id) {
    CoreThemePreset preset;
    CoreResult result;

    if (!state) {
        return 0;
    }

    result = core_theme_get_preset(preset_id, &preset);
    if (result.code != CORE_OK) {
        return 0;
    }

    state->theme_preset_id = preset_id;
    sync_theme_name(state);
    return 1;
}

static void sync_font_name(MemConsoleState *state) {
    const char *name;

    if (!state) {
        return;
    }

    name = core_font_preset_name(state->font_preset_id);
    if (!name || !name[0]) {
        name = "unknown";
    }
    (void)snprintf(state->font_name, sizeof(state->font_name), "%s", name);
}

int cycle_font_preset(MemConsoleState *state, int direction) {
    size_t i;
    size_t count;

    if (!state) {
        return 0;
    }

    count = sizeof(k_mem_console_font_cycle_order) / sizeof(k_mem_console_font_cycle_order[0]);
    if (count == 0u) {
        return 0;
    }

    for (i = 0; i < count; ++i) {
        if (k_mem_console_font_cycle_order[i] == state->font_preset_id) {
            if (direction >= 0) {
                state->font_preset_id = k_mem_console_font_cycle_order[(i + 1u) % count];
            } else {
                state->font_preset_id = k_mem_console_font_cycle_order[(i + count - 1u) % count];
            }
            sync_font_name(state);
            return 1;
        }
    }

    state->font_preset_id = k_mem_console_font_cycle_order[0];
    sync_font_name(state);
    return 1;
}

int state_set_font_preset(MemConsoleState *state, CoreFontPresetId preset_id) {
    CoreFontPreset preset;
    CoreResult result;

    if (!state) {
        return 0;
    }

    result = core_font_get_preset(preset_id, &preset);
    if (result.code != CORE_OK) {
        return 0;
    }

    state->font_preset_id = preset_id;
    sync_font_name(state);
    return 1;
}

void seed_state(MemConsoleState *state, const char *db_path) {
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->db_path = db_path;
    state->theme_preset_id = CORE_THEME_PRESET_DAW_DEFAULT;
    state->font_preset_id = CORE_FONT_PRESET_IDE;
    state->search_text[0] = '\0';
    state->graph_mode_enabled = 0;
    state->list_query_offset = 0;
    state->visible_start_index = 0;
    state->project_filter_scroll = 0.0f;
    state->title_edit_mode = 0;
    state->body_edit_mode = 0;
    state->input_target = MEM_CONSOLE_INPUT_SEARCH;
    state->search_cursor = 0;
    state->title_edit_cursor = 0;
    state->body_edit_cursor = 0;
    state->graph_edge_limit_cursor = 0;
    state->search_refresh_pending = 0;
    state->search_last_input_ms = 0u;
    state->project_filter_option_count = 0;
    state->selected_project_count = 0;
    state->graph_kind_filter[0] = '\0';
    mem_console_graph_edge_limit_set(state, MEM_CONSOLE_GRAPH_EDGE_LIMIT);
    state->graph_query_hops = MEM_CONSOLE_GRAPH_HOPS_MIN;
    sync_theme_name(state);
    sync_font_name(state);
    (void)snprintf(state->status_line,
                   sizeof(state->status_line),
                   "Type to search. Click a row to inspect it.");
    (void)snprintf(state->runtime_summary_line,
                   sizeof(state->runtime_summary_line),
                   "Async s0 a0 d0 e0 c0 | if=0 p=0");
    (void)snprintf(state->kernel_summary_line,
                   sizeof(state->kernel_summary_line),
                   "Kernel off");
    (void)snprintf(state->project_filter_summary_line,
                   sizeof(state->project_filter_summary_line),
                   "Projects: all");
    set_default_detail(state);
    state->title_edit_text[0] = '\0';
    state->body_edit_text[0] = '\0';
    kit_graph_struct_viewport_default(&state->graph_viewport);
}

void compute_layout(MemConsoleState *state, int frame_width, int frame_height) {
    float left_width;
    float pane_height;

    if (!state) {
        return;
    }

    if (frame_width < 1100) {
        frame_width = 1100;
    }
    if (frame_height < 760) {
        frame_height = 760;
    }

    left_width = 380.0f;
    pane_height = (float)frame_height - 48.0f;
    state->left_pane = (KitRenderRect){24.0f, 24.0f, left_width, pane_height};
    state->right_pane = (KitRenderRect){
        state->left_pane.x + state->left_pane.width + 24.0f,
        24.0f,
        (float)frame_width - (state->left_pane.x + state->left_pane.width + 48.0f),
        pane_height
    };
}

void build_like_pattern(const char *search_text, char *out_pattern, size_t out_cap) {
    if (!out_pattern || out_cap == 0u) {
        return;
    }

    if (!search_text || search_text[0] == '\0') {
        out_pattern[0] = '\0';
        return;
    }

    (void)snprintf(out_pattern, out_cap, "%%%s%%", search_text);
}

int mem_console_graph_edge_limit_clamp(int value) {
    if (value < MEM_CONSOLE_GRAPH_EDGE_LIMIT_MIN) {
        return MEM_CONSOLE_GRAPH_EDGE_LIMIT_MIN;
    }
    if (value > MEM_CONSOLE_GRAPH_EDGE_LIMIT) {
        return MEM_CONSOLE_GRAPH_EDGE_LIMIT;
    }
    return value;
}

int mem_console_graph_hops_clamp(int value) {
    if (value < MEM_CONSOLE_GRAPH_HOPS_MIN) {
        return MEM_CONSOLE_GRAPH_HOPS_MIN;
    }
    if (value > MEM_CONSOLE_GRAPH_HOPS_MAX) {
        return MEM_CONSOLE_GRAPH_HOPS_MAX;
    }
    return value;
}

int mem_console_graph_edge_limit_parse(const char *text, int fallback) {
    int parsed = 0;
    int i = 0;

    if (!text || text[0] == '\0') {
        return mem_console_graph_edge_limit_clamp(fallback);
    }

    while (text[i] != '\0') {
        if (text[i] < '0' || text[i] > '9') {
            return mem_console_graph_edge_limit_clamp(fallback);
        }
        parsed = (parsed * 10) + (text[i] - '0');
        if (parsed > MEM_CONSOLE_GRAPH_EDGE_LIMIT) {
            parsed = MEM_CONSOLE_GRAPH_EDGE_LIMIT;
            break;
        }
        i += 1;
    }

    return mem_console_graph_edge_limit_clamp(parsed);
}

void mem_console_graph_edge_limit_set(MemConsoleState *state, int value) {
    if (!state) {
        return;
    }

    state->graph_query_edge_limit = mem_console_graph_edge_limit_clamp(value);
    (void)snprintf(state->graph_edge_limit_text,
                   sizeof(state->graph_edge_limit_text),
                   "%d",
                   state->graph_query_edge_limit);
    state->graph_edge_limit_cursor = (int)strlen(state->graph_edge_limit_text);
}

static int estimate_char_width_px(CoreFontTextSizeTier text_tier) {
    switch (text_tier) {
        case CORE_FONT_TEXT_SIZE_HEADER:
            return 13;
        case CORE_FONT_TEXT_SIZE_TITLE:
            return 11;
        case CORE_FONT_TEXT_SIZE_PARAGRAPH:
            return 9;
        case CORE_FONT_TEXT_SIZE_CAPTION:
            return 8;
        case CORE_FONT_TEXT_SIZE_BASIC:
        default:
            return 9;
    }
}

void format_text_for_width(char *out_text,
                           size_t out_cap,
                           const char *source_text,
                           float width_px,
                           CoreFontTextSizeTier text_tier) {
    size_t source_len;
    int char_width;
    int max_chars;
    size_t keep_len;

    if (!out_text || out_cap == 0u) {
        return;
    }
    out_text[0] = '\0';

    if (!source_text) {
        return;
    }

    char_width = estimate_char_width_px(text_tier);
    if (char_width < 1) {
        char_width = 8;
    }
    max_chars = (int)(width_px / (float)char_width);
    if (max_chars < 4) {
        max_chars = 4;
    }

    source_len = strlen(source_text);
    if ((int)source_len <= max_chars) {
        (void)snprintf(out_text, out_cap, "%s", source_text);
        return;
    }

    keep_len = (size_t)(max_chars - 3);
    if (keep_len >= out_cap) {
        keep_len = out_cap - 1u;
    }

    if (keep_len > 0u) {
        memcpy(out_text, source_text, keep_len);
    }

    if (keep_len + 3u < out_cap) {
        memcpy(out_text + keep_len, "...", 3u);
        out_text[keep_len + 3u] = '\0';
        return;
    }

    out_text[out_cap - 1u] = '\0';
}

int mem_console_project_filter_is_selected(const MemConsoleState *state, const char *project_key) {
    int i;

    if (!state || !project_key || project_key[0] == '\0') {
        return 0;
    }

    for (i = 0; i < state->selected_project_count; ++i) {
        if (strcmp(state->selected_project_keys[i], project_key) == 0) {
            return 1;
        }
    }
    return 0;
}

void mem_console_project_filter_clear(MemConsoleState *state) {
    int i;

    if (!state) {
        return;
    }
    for (i = 0; i < MEM_CONSOLE_SCOPE_FILTER_LIMIT; ++i) {
        state->selected_project_keys[i][0] = '\0';
    }
    state->selected_project_count = 0;
}

int mem_console_project_filter_toggle(MemConsoleState *state, const char *project_key) {
    int i;

    if (!state || !project_key || project_key[0] == '\0') {
        return 0;
    }

    for (i = 0; i < state->selected_project_count; ++i) {
        if (strcmp(state->selected_project_keys[i], project_key) == 0) {
            int j;
            for (j = i; j < (state->selected_project_count - 1); ++j) {
                (void)snprintf(state->selected_project_keys[j],
                               sizeof(state->selected_project_keys[j]),
                               "%s",
                               state->selected_project_keys[j + 1]);
            }
            state->selected_project_keys[state->selected_project_count - 1][0] = '\0';
            state->selected_project_count -= 1;
            return 1;
        }
    }

    if (state->selected_project_count >= MEM_CONSOLE_SCOPE_FILTER_LIMIT) {
        return 0;
    }
    (void)snprintf(state->selected_project_keys[state->selected_project_count],
                   sizeof(state->selected_project_keys[state->selected_project_count]),
                   "%s",
                   project_key);
    state->selected_project_count += 1;
    return 1;
}

void mem_console_project_filter_prune_to_options(MemConsoleState *state) {
    int write_index = 0;
    int i;

    if (!state) {
        return;
    }

    for (i = 0; i < state->selected_project_count; ++i) {
        int option_index;
        int found = 0;
        const char *selected_key = state->selected_project_keys[i];

        if (!selected_key[0]) {
            continue;
        }
        for (option_index = 0; option_index < state->project_filter_option_count; ++option_index) {
            if (strcmp(selected_key, state->project_filter_keys[option_index]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            continue;
        }
        if (write_index != i) {
            (void)snprintf(state->selected_project_keys[write_index],
                           sizeof(state->selected_project_keys[write_index]),
                           "%s",
                           selected_key);
        }
        write_index += 1;
    }

    for (i = write_index; i < MEM_CONSOLE_SCOPE_FILTER_LIMIT; ++i) {
        state->selected_project_keys[i][0] = '\0';
    }
    state->selected_project_count = write_index;
}

int selected_id_in_visible_items(const MemConsoleState *state) {
    int i;

    if (!state || state->selected_item_id == 0) {
        return 0;
    }

    for (i = 0; i < state->visible_count; ++i) {
        if (state->visible_items[i].id == state->selected_item_id) {
            return 1;
        }
    }

    return 0;
}

static int clamp_cursor_to_text(const char *text, int cursor) {
    int len;

    if (!text) {
        return 0;
    }

    len = (int)strlen(text);
    if (cursor < 0) return 0;
    if (cursor > len) return len;
    return cursor;
}

static void resolve_active_input_buffer(MemConsoleState *state,
                                        char **out_text,
                                        size_t *out_cap,
                                        int **out_cursor) {
    if (!state || !out_text || !out_cap || !out_cursor) {
        return;
    }

    if (state->input_target == MEM_CONSOLE_INPUT_TITLE_EDIT) {
        *out_text = state->title_edit_text;
        *out_cap = sizeof(state->title_edit_text);
        *out_cursor = &state->title_edit_cursor;
    } else if (state->input_target == MEM_CONSOLE_INPUT_BODY_EDIT) {
        *out_text = state->body_edit_text;
        *out_cap = sizeof(state->body_edit_text);
        *out_cursor = &state->body_edit_cursor;
    } else if (state->input_target == MEM_CONSOLE_INPUT_GRAPH_EDGE_LIMIT) {
        *out_text = state->graph_edge_limit_text;
        *out_cap = sizeof(state->graph_edge_limit_text);
        *out_cursor = &state->graph_edge_limit_cursor;
    } else {
        *out_text = state->search_text;
        *out_cap = sizeof(state->search_text);
        *out_cursor = &state->search_cursor;
    }
}

void sync_edit_buffers_from_selection(MemConsoleState *state) {
    if (!state) {
        return;
    }
    if (!state->title_edit_mode) {
        (void)snprintf(state->title_edit_text,
                       sizeof(state->title_edit_text),
                       "%s",
                       state->selected_title);
        state->title_edit_cursor = (int)strlen(state->title_edit_text);
    }
    if (!state->body_edit_mode) {
        (void)snprintf(state->body_edit_text,
                       sizeof(state->body_edit_text),
                       "%s",
                       state->selected_body);
        state->body_edit_cursor = (int)strlen(state->body_edit_text);
    }
    state->search_cursor = clamp_cursor_to_text(state->search_text, state->search_cursor);
    state->graph_edge_limit_cursor = clamp_cursor_to_text(state->graph_edge_limit_text,
                                                          state->graph_edge_limit_cursor);
}

void begin_title_edit_mode(MemConsoleState *state) {
    if (!state || state->selected_item_id == 0) {
        return;
    }
    state->body_edit_mode = 0;
    state->title_edit_mode = 1;
    state->input_target = MEM_CONSOLE_INPUT_TITLE_EDIT;
    (void)snprintf(state->title_edit_text,
                   sizeof(state->title_edit_text),
                   "%s",
                   state->selected_title);
    state->title_edit_cursor = (int)strlen(state->title_edit_text);
}

void cancel_title_edit_mode(MemConsoleState *state) {
    if (!state) {
        return;
    }
    state->title_edit_mode = 0;
    state->input_target = MEM_CONSOLE_INPUT_SEARCH;
    (void)snprintf(state->title_edit_text,
                   sizeof(state->title_edit_text),
                   "%s",
                   state->selected_title);
    state->title_edit_cursor = (int)strlen(state->title_edit_text);
}

void begin_body_edit_mode(MemConsoleState *state) {
    if (!state || state->selected_item_id == 0) {
        return;
    }
    state->title_edit_mode = 0;
    state->body_edit_mode = 1;
    state->input_target = MEM_CONSOLE_INPUT_BODY_EDIT;
    (void)snprintf(state->body_edit_text,
                   sizeof(state->body_edit_text),
                   "%s",
                   state->selected_body);
    state->body_edit_cursor = (int)strlen(state->body_edit_text);
}

void cancel_body_edit_mode(MemConsoleState *state) {
    if (!state) {
        return;
    }
    state->body_edit_mode = 0;
    state->input_target = MEM_CONSOLE_INPUT_SEARCH;
    (void)snprintf(state->body_edit_text,
                   sizeof(state->body_edit_text),
                   "%s",
                   state->selected_body);
    state->body_edit_cursor = (int)strlen(state->body_edit_text);
}

int active_input_is_search(const MemConsoleState *state) {
    if (!state) {
        return 1;
    }
    return state->input_target == MEM_CONSOLE_INPUT_SEARCH;
}

void append_active_input_text(MemConsoleState *state, const char *text) {
    char *target = 0;
    size_t target_cap = 0;
    int *cursor_ptr = 0;
    size_t current_len;
    size_t append_len;
    size_t available;
    int cursor;

    if (!state || !text) {
        return;
    }

    resolve_active_input_buffer(state, &target, &target_cap, &cursor_ptr);
    if (!target || !cursor_ptr || target_cap == 0u) {
        return;
    }

    current_len = strlen(target);
    if (current_len >= target_cap - 1u) {
        return;
    }
    cursor = clamp_cursor_to_text(target, *cursor_ptr);

    append_len = strlen(text);
    available = (target_cap - 1u) - current_len;
    if (append_len > available) {
        append_len = available;
    }
    if (append_len == 0u) {
        return;
    }

    memmove(target + cursor + (int)append_len,
            target + cursor,
            current_len - (size_t)cursor + 1u);
    memcpy(target + cursor, text, append_len);
    *cursor_ptr = cursor + (int)append_len;
}

void erase_active_input_char(MemConsoleState *state) {
    char *target = 0;
    size_t target_cap = 0;
    int *cursor_ptr = 0;
    size_t current_len;
    int cursor;

    if (!state) {
        return;
    }

    resolve_active_input_buffer(state, &target, &target_cap, &cursor_ptr);
    if (!target || !cursor_ptr || target_cap == 0u) {
        return;
    }

    current_len = strlen(target);
    if (current_len == 0u) {
        return;
    }
    cursor = clamp_cursor_to_text(target, *cursor_ptr);
    if (cursor <= 0) {
        return;
    }

    memmove(target + cursor - 1,
            target + cursor,
            current_len - (size_t)cursor + 1u);
    *cursor_ptr = cursor - 1;
}

void delete_active_input_char(MemConsoleState *state) {
    char *target = 0;
    size_t target_cap = 0;
    int *cursor_ptr = 0;
    size_t current_len;
    int cursor;

    if (!state) {
        return;
    }

    resolve_active_input_buffer(state, &target, &target_cap, &cursor_ptr);
    if (!target || !cursor_ptr || target_cap == 0u) {
        return;
    }

    current_len = strlen(target);
    if (current_len == 0u) {
        return;
    }
    cursor = clamp_cursor_to_text(target, *cursor_ptr);
    if (cursor >= (int)current_len) {
        return;
    }

    memmove(target + cursor,
            target + cursor + 1,
            current_len - (size_t)cursor);
}

void move_active_input_cursor(MemConsoleState *state, int delta) {
    char *target = 0;
    size_t target_cap = 0;
    int *cursor_ptr = 0;

    if (!state) {
        return;
    }

    resolve_active_input_buffer(state, &target, &target_cap, &cursor_ptr);
    if (!target || !cursor_ptr || target_cap == 0u) {
        return;
    }

    *cursor_ptr = clamp_cursor_to_text(target, *cursor_ptr + delta);
}

void move_active_input_cursor_home(MemConsoleState *state) {
    char *target = 0;
    size_t target_cap = 0;
    int *cursor_ptr = 0;

    if (!state) {
        return;
    }
    resolve_active_input_buffer(state, &target, &target_cap, &cursor_ptr);
    if (!target || !cursor_ptr || target_cap == 0u) {
        return;
    }
    *cursor_ptr = 0;
}

void move_active_input_cursor_end(MemConsoleState *state) {
    char *target = 0;
    size_t target_cap = 0;
    int *cursor_ptr = 0;

    if (!state) {
        return;
    }
    resolve_active_input_buffer(state, &target, &target_cap, &cursor_ptr);
    if (!target || !cursor_ptr || target_cap == 0u) {
        return;
    }
    *cursor_ptr = (int)strlen(target);
}
