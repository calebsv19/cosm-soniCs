/*
 * mem_console.c
 * Part of the CodeWork Shared Libraries
 * Copyright (c) 2026 Caleb S. V.
 * Licensed under the Apache License, Version 2.0
 */

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "kit_render.h"
#include "kit_ui.h"
#include "mem_console_db.h"
#include "mem_console_kernel_bridge.h"
#include "mem_console_prefs.h"
#include "mem_console_runtime.h"
#include "mem_console_state.h"
#include "mem_console_ui.h"
#include "vk_renderer.h"

static void set_action_error_status(MemConsoleState *state,
                                    const char *prefix,
                                    CoreResult result) {
    const char *message;

    if (!state || !prefix) {
        return;
    }

    message = result.message ? result.message : "error";
    (void)snprintf(state->status_line,
                   sizeof(state->status_line),
                   "%s: %s",
                   prefix,
                   message);
}

static void refresh_and_report(CoreMemDb *db,
                               MemConsoleState *state,
                               const char *error_prefix) {
    CoreResult result;

    if (!db || !state || !error_prefix) {
        return;
    }

    result = refresh_state_from_db(db, state);
    if (result.code != CORE_OK) {
        set_action_error_status(state, error_prefix, result);
        return;
    }
    sync_edit_buffers_from_selection(state);
}

static int handle_theme_shortcut(KitRenderContext *render_ctx,
                                 KitUiContext *ui_ctx,
                                 MemConsoleState *state,
                                 const char *prefs_path,
                                 SDL_Keycode keycode) {
    int direction;
    CoreResult result;

    if (!render_ctx || !ui_ctx || !state) {
        return 0;
    }

    if (keycode == SDLK_t) {
        direction = 1;
    } else if (keycode == SDLK_y) {
        direction = -1;
    } else {
        return 0;
    }

    if (!cycle_theme_preset(state, direction)) {
        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Theme switch failed.");
        return 1;
    }

    result = kit_render_set_theme_preset(render_ctx, state->theme_preset_id);
    if (result.code != CORE_OK) {
        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Theme switch failed.");
        return 1;
    }

    (void)kit_ui_style_apply_theme_scale(ui_ctx);
    if (prefs_path && prefs_path[0]) {
        result = mem_console_prefs_save(prefs_path, state);
        if (result.code != CORE_OK) {
            (void)snprintf(state->status_line,
                           sizeof(state->status_line),
                           "Theme switched; prefs save failed.");
            return 1;
        }
    }

    (void)snprintf(state->status_line, sizeof(state->status_line), "Theme switched to %s.", state->theme_name);
    return 1;
}

static int handle_font_shortcut(KitRenderContext *render_ctx,
                                MemConsoleState *state,
                                const char *prefs_path,
                                SDL_Keycode keycode) {
    int direction;
    CoreResult result;

    if (!render_ctx || !state) {
        return 0;
    }

    if (keycode == SDLK_u) {
        direction = 1;
    } else if (keycode == SDLK_i) {
        direction = -1;
    } else {
        return 0;
    }

    if (!cycle_font_preset(state, direction)) {
        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Font switch failed.");
        return 1;
    }

    result = kit_render_set_font_preset(render_ctx, state->font_preset_id);
    if (result.code != CORE_OK) {
        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Font switch failed.");
        return 1;
    }

    if (prefs_path && prefs_path[0]) {
        result = mem_console_prefs_save(prefs_path, state);
        if (result.code != CORE_OK) {
            (void)snprintf(state->status_line,
                           sizeof(state->status_line),
                           "Font switched; prefs save failed.");
            return 1;
        }
    }

    (void)snprintf(state->status_line, sizeof(state->status_line), "Font switched to %s.", state->font_name);
    return 1;
}

static void apply_pending_action(CoreMemDb *db,
                                 MemConsoleState *state,
                                 MemConsoleRuntime *runtime,
                                 MemConsoleAction action) {
    CoreResult result;

    if (!db || !state) {
        return;
    }

    if (action == MEM_CONSOLE_ACTION_NONE) {
        return;
    }

    if (action == MEM_CONSOLE_ACTION_REFRESH) {
        refresh_and_report(db, state, "Refresh failed");
        return;
    }

    if (action == MEM_CONSOLE_ACTION_CREATE_FROM_SEARCH) {
        int64_t created_id = 0;
        result = create_item_from_search(db, state, &created_id);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Create failed", result);
            return;
        }

        state->selected_item_id = created_id;
        result = refresh_state_from_db(db, state);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Refresh failed", result);
            return;
        }
        sync_edit_buffers_from_selection(state);
        mem_console_runtime_note_local_write(runtime, SDL_GetTicks64());

        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Created memory %lld from search text.",
                       (long long)created_id);
        return;
    }

    if (action == MEM_CONSOLE_ACTION_BEGIN_TITLE_EDIT) {
        begin_title_edit_mode(state);
        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Title edit mode enabled.");
        return;
    }

    if (action == MEM_CONSOLE_ACTION_CANCEL_TITLE_EDIT) {
        cancel_title_edit_mode(state);
        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Title edit cancelled.");
        return;
    }

    if (action == MEM_CONSOLE_ACTION_SAVE_TITLE_EDIT) {
        int64_t edited_id = state->selected_item_id;
        result = rename_selected_from_title_buffer(db, state);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Save title failed", result);
            return;
        }

        result = refresh_state_from_db(db, state);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Refresh failed", result);
            return;
        }
        cancel_title_edit_mode(state);
        sync_edit_buffers_from_selection(state);
        mem_console_runtime_note_local_write(runtime, SDL_GetTicks64());

        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Saved title for memory %lld.",
                       (long long)edited_id);
        return;
    }

    if (action == MEM_CONSOLE_ACTION_BEGIN_BODY_EDIT) {
        begin_body_edit_mode(state);
        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Body edit mode enabled.");
        return;
    }

    if (action == MEM_CONSOLE_ACTION_CANCEL_BODY_EDIT) {
        cancel_body_edit_mode(state);
        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Body edit cancelled.");
        return;
    }

    if (action == MEM_CONSOLE_ACTION_SAVE_BODY_EDIT) {
        int64_t edited_id = state->selected_item_id;
        result = replace_selected_body_from_body_buffer(db, state);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Save body failed", result);
            return;
        }

        result = refresh_state_from_db(db, state);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Refresh failed", result);
            return;
        }
        cancel_body_edit_mode(state);
        sync_edit_buffers_from_selection(state);
        mem_console_runtime_note_local_write(runtime, SDL_GetTicks64());

        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Saved body for memory %lld.",
                       (long long)edited_id);
        return;
    }

    if (action == MEM_CONSOLE_ACTION_TOGGLE_PINNED) {
        int next_value = state->selected_pinned ? 0 : 1;
        int64_t toggled_id = state->selected_item_id;

        result = set_selected_flag(db, state, "pinned", next_value);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Pinned toggle failed", result);
            return;
        }

        result = refresh_state_from_db(db, state);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Refresh failed", result);
            return;
        }
        sync_edit_buffers_from_selection(state);
        mem_console_runtime_note_local_write(runtime, SDL_GetTicks64());

        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Pinned %s for memory %lld.",
                       next_value ? "enabled" : "disabled",
                       (long long)toggled_id);
        return;
    }

    if (action == MEM_CONSOLE_ACTION_TOGGLE_CANONICAL) {
        int next_value = state->selected_canonical ? 0 : 1;
        int64_t toggled_id = state->selected_item_id;

        result = set_selected_flag(db, state, "canonical", next_value);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Canonical toggle failed", result);
            return;
        }

        result = refresh_state_from_db(db, state);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Refresh failed", result);
            return;
        }
        sync_edit_buffers_from_selection(state);
        mem_console_runtime_note_local_write(runtime, SDL_GetTicks64());

        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Canonical %s for memory %lld.",
                       next_value ? "enabled" : "disabled",
                       (long long)toggled_id);
        return;
    }

    if (action == MEM_CONSOLE_ACTION_TOGGLE_GRAPH_MODE) {
        state->graph_mode_enabled = state->graph_mode_enabled ? 0 : 1;
        if (!state->graph_mode_enabled) {
            (void)snprintf(state->status_line,
                           sizeof(state->status_line),
                           "Graph mode disabled.");
            return;
        }

        result = load_graph_neighborhood(db, state);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Graph load failed", result);
            return;
        }

        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Graph mode enabled (%d nodes, %d edges, kind=%s, limit=%d, hops=%d).",
                       state->graph_node_count,
                       state->graph_edge_count,
                       state->graph_kind_filter[0] ? state->graph_kind_filter : "all",
                       state->graph_query_edge_limit,
                       state->graph_query_hops);
        return;
    }

    if (action == MEM_CONSOLE_ACTION_REFRESH_GRAPH) {
        result = load_graph_neighborhood(db, state);
        if (result.code != CORE_OK) {
            set_action_error_status(state, "Graph refresh failed", result);
            return;
        }

        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Graph refreshed (%d nodes, %d edges, kind=%s, limit=%d, hops=%d).",
                       state->graph_node_count,
                       state->graph_edge_count,
                       state->graph_kind_filter[0] ? state->graph_kind_filter : "all",
                       state->graph_query_edge_limit,
                       state->graph_query_hops);
    }
}

static void mark_search_input_changed(MemConsoleState *state) {
    if (!state) {
        return;
    }
    state->search_refresh_pending = 1;
    state->search_last_input_ms = SDL_GetTicks64();
    state->list_scroll = 0.0f;
    state->list_query_offset = 0;
}

static void append_graph_edge_limit_digits(MemConsoleState *state, const char *text) {
    int i;
    char digit_text[2];

    if (!state || !text) {
        return;
    }

    digit_text[1] = '\0';
    for (i = 0; text[i] != '\0'; ++i) {
        if (text[i] >= '0' && text[i] <= '9') {
            digit_text[0] = text[i];
            append_active_input_text(state, digit_text);
        }
    }
}

static void commit_graph_edge_limit_input(MemConsoleState *state,
                                          MemConsoleAction *keyboard_action) {
    int parsed_limit;
    int changed;

    if (!state || !keyboard_action) {
        return;
    }

    parsed_limit = mem_console_graph_edge_limit_parse(state->graph_edge_limit_text,
                                                      state->graph_query_edge_limit);
    changed = parsed_limit != state->graph_query_edge_limit;
    mem_console_graph_edge_limit_set(state, parsed_limit);

    if (state->graph_mode_enabled) {
        *keyboard_action = MEM_CONSOLE_ACTION_REFRESH_GRAPH;
    }
    if (changed) {
        (void)snprintf(state->status_line,
                       sizeof(state->status_line),
                       "Graph edge limit set to %d.",
                       parsed_limit);
    }
}

static void process_sdl_event(const SDL_Event *event,
                              bool *running,
                              KitRenderContext *render_ctx,
                              KitUiContext *ui_ctx,
                              MemConsoleState *state,
                              const char *prefs_path,
                              KitUiInputState *input,
                              int *wheel_y,
                              MemConsoleAction *keyboard_action) {
    if (!event || !running || !render_ctx || !ui_ctx || !state || !input || !wheel_y || !keyboard_action) {
        return;
    }

    switch (event->type) {
        case SDL_QUIT:
            *running = false;
            break;
        case SDL_KEYDOWN:
            {
                Uint16 mod = event->key.keysym.mod;
                int ctrl_or_cmd = (mod & (KMOD_CTRL | KMOD_GUI)) != 0;
                int shift = (mod & KMOD_SHIFT) != 0;
                int handled_shortcut = 0;

                if (ctrl_or_cmd && shift) {
                    handled_shortcut = handle_theme_shortcut(render_ctx,
                                                             ui_ctx,
                                                             state,
                                                             prefs_path,
                                                             event->key.keysym.sym);
                    if (!handled_shortcut) {
                        handled_shortcut = handle_font_shortcut(render_ctx,
                                                                state,
                                                                prefs_path,
                                                                event->key.keysym.sym);
                    }
                }

                if (handled_shortcut) {
                    break;
                }

                if (event->key.keysym.sym == SDLK_ESCAPE) {
                    *running = false;
                    break;
                }
                if (ctrl_or_cmd && event->key.keysym.sym == SDLK_v) {
                    char *clipboard_text = SDL_GetClipboardText();
                    if (clipboard_text && clipboard_text[0] != '\0') {
                        if (state->input_target == MEM_CONSOLE_INPUT_GRAPH_EDGE_LIMIT) {
                            append_graph_edge_limit_digits(state, clipboard_text);
                        } else {
                            append_active_input_text(state, clipboard_text);
                        }
                        if (active_input_is_search(state)) {
                            mark_search_input_changed(state);
                        }
                    }
                    if (clipboard_text) {
                        SDL_free(clipboard_text);
                    }
                    break;
                }
                if (event->key.keysym.sym == SDLK_BACKSPACE) {
                    erase_active_input_char(state);
                    if (active_input_is_search(state)) {
                        mark_search_input_changed(state);
                    }
                    break;
                }
                if (event->key.keysym.sym == SDLK_DELETE) {
                    delete_active_input_char(state);
                    if (active_input_is_search(state)) {
                        mark_search_input_changed(state);
                    }
                    break;
                }
                if (event->key.keysym.sym == SDLK_LEFT) {
                    move_active_input_cursor(state, -1);
                    break;
                }
                if (event->key.keysym.sym == SDLK_RIGHT) {
                    move_active_input_cursor(state, 1);
                    break;
                }
                if (event->key.keysym.sym == SDLK_HOME) {
                    move_active_input_cursor_home(state);
                    break;
                }
                if (event->key.keysym.sym == SDLK_END) {
                    move_active_input_cursor_end(state);
                    break;
                }
                if (event->key.keysym.sym == SDLK_RETURN ||
                    event->key.keysym.sym == SDLK_KP_ENTER) {
                    if (state->input_target == MEM_CONSOLE_INPUT_TITLE_EDIT) {
                        *keyboard_action = MEM_CONSOLE_ACTION_SAVE_TITLE_EDIT;
                    } else if (state->input_target == MEM_CONSOLE_INPUT_BODY_EDIT) {
                        if (ctrl_or_cmd && !shift) {
                            *keyboard_action = MEM_CONSOLE_ACTION_SAVE_BODY_EDIT;
                        } else {
                            append_active_input_text(state, "\n");
                        }
                    } else if (state->input_target == MEM_CONSOLE_INPUT_GRAPH_EDGE_LIMIT) {
                        commit_graph_edge_limit_input(state, keyboard_action);
                    } else {
                        state->search_refresh_pending = 0;
                        *keyboard_action = MEM_CONSOLE_ACTION_REFRESH;
                    }
                }
            }
            break;
        case SDL_TEXTINPUT:
            if (state->input_target == MEM_CONSOLE_INPUT_GRAPH_EDGE_LIMIT) {
                append_graph_edge_limit_digits(state, event->text.text);
            } else {
                append_active_input_text(state, event->text.text);
            }
            if (active_input_is_search(state)) {
                mark_search_input_changed(state);
            }
            break;
        case SDL_MOUSEMOTION:
            input->mouse_x = (float)event->motion.x;
            input->mouse_y = (float)event->motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event->button.button == SDL_BUTTON_LEFT) {
                input->mouse_down = 1;
                input->mouse_pressed = 1;
                input->mouse_x = (float)event->button.x;
                input->mouse_y = (float)event->button.y;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event->button.button == SDL_BUTTON_LEFT) {
                input->mouse_down = 0;
                input->mouse_released = 1;
                input->mouse_x = (float)event->button.x;
                input->mouse_y = (float)event->button.y;
            }
            break;
        case SDL_MOUSEWHEEL:
            *wheel_y = event->wheel.y;
            break;
        default:
            break;
    }
}

int main(int argc, char **argv) {
    const char *db_path = k_mem_console_default_db_path;
    const char *db_flag = 0;
    char default_db_path[1024];
    char prefs_path[1200];
    SDL_Window *window = 0;
    SDL_Event event;
    bool running = true;
    VkRenderer renderer;
    VkRendererConfig config;
    KitRenderContext render_ctx;
    KitUiContext ui_ctx;
    KitUiInputState input = {0};
    CoreMemDb db = {0};
    CoreResult result;
    MemConsoleState state;
    MemConsoleRuntime runtime;
    MemConsoleKernelBridge kernel_bridge;
    int kernel_bridge_requested = 0;
    int frame_width = 0;
    int frame_height = 0;
    int wheel_y = 0;
    int exit_code = 1;
    int prefs_path_valid = 0;

    if (has_unknown_flag(argc, argv)) {
        print_usage(argv[0]);
        return 1;
    }

    db_flag = find_flag_value(argc, argv, "--db");
    kernel_bridge_requested = has_flag(argc, argv, "--kernel-bridge");
    if (db_flag) {
        db_path = db_flag;
    } else if (resolve_default_db_path(default_db_path, sizeof(default_db_path))) {
        db_path = default_db_path;
    }

    seed_state(&state, db_path);
    prefs_path_valid = mem_console_build_prefs_path(db_path, prefs_path, sizeof(prefs_path));
    if (prefs_path_valid) {
        result = mem_console_prefs_load(prefs_path, &state);
        if (result.code != CORE_OK) {
            (void)snprintf(state.status_line, sizeof(state.status_line), "UI prefs load failed.");
        } else if (result.message && strcmp(result.message, "prefs loaded") == 0) {
            (void)snprintf(state.status_line, sizeof(state.status_line), "UI prefs restored.");
        }
    }
    memset(&runtime, 0, sizeof(runtime));
    memset(&kernel_bridge, 0, sizeof(kernel_bridge));
    state.kernel_bridge_enabled = kernel_bridge_requested ? 1 : 0;

    result = core_memdb_open(state.db_path, &db);
    if (result.code != CORE_OK) {
        fprintf(stderr, "mem_console: open failed: %s\n", result.message);
        return 1;
    }

    result = refresh_state_from_db(&db, &state);
    if (result.code != CORE_OK) {
        fprintf(stderr, "mem_console: refresh failed: %s\n", result.message);
        goto cleanup_db;
    }
    sync_edit_buffers_from_selection(&state);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "mem_console: SDL_Init failed: %s\n", SDL_GetError());
        goto cleanup_db;
    }

    window = SDL_CreateWindow("mem_console",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              1440,
                              900,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "mem_console: SDL_CreateWindow failed: %s\n", SDL_GetError());
        goto cleanup_sdl;
    }

    vk_renderer_config_set_defaults(&config);
    config.enable_validation = VK_FALSE;
    if (vk_renderer_init(&renderer, window, &config) != VK_SUCCESS) {
        fprintf(stderr, "mem_console: vk_renderer_init failed\n");
        goto cleanup_window;
    }

    result = kit_render_context_init(&render_ctx,
                                     KIT_RENDER_BACKEND_VULKAN,
                                     state.theme_preset_id,
                                     state.font_preset_id);
    if (result.code != CORE_OK) {
        fprintf(stderr, "mem_console: kit_render_context_init failed: %d\n", (int)result.code);
        goto cleanup_renderer;
    }

    result = kit_render_attach_external_backend(&render_ctx, &renderer);
    if (result.code != CORE_OK) {
        fprintf(stderr, "mem_console: kit_render_attach_external_backend failed: %d\n", (int)result.code);
        goto cleanup_render_ctx;
    }

    result = kit_ui_context_init(&ui_ctx, &render_ctx);
    if (result.code != CORE_OK) {
        fprintf(stderr, "mem_console: kit_ui_context_init failed: %d\n", (int)result.code);
        goto cleanup_render_ctx;
    }

    result = mem_console_runtime_init(&runtime, SDL_GetTicks64());
    if (result.code != CORE_OK) {
        fprintf(stderr, "mem_console: runtime init failed: %s\n", result.message);
        goto cleanup_runtime;
    }

    if (kernel_bridge_requested) {
        result = mem_console_kernel_bridge_init(&kernel_bridge);
        if (result.code != CORE_OK) {
            fprintf(stderr, "mem_console: kernel bridge init failed: %s\n", result.message);
            goto cleanup_kernel_bridge;
        }
    } else {
        (void)snprintf(state.kernel_summary_line,
                       sizeof(state.kernel_summary_line),
                       "Kernel off");
    }

    SDL_StartTextInput();

    while (running) {
        MemConsoleAction keyboard_action = MEM_CONSOLE_ACTION_NONE;
        MemConsoleAction ui_action = MEM_CONSOLE_ACTION_NONE;
        MemConsoleAction pending_action = MEM_CONSOLE_ACTION_NONE;
        uint32_t idle_wait_ms = 0u;
        int waited_event = 0;

        input.mouse_pressed = 0;
        input.mouse_released = 0;
        wheel_y = 0;

        idle_wait_ms = mem_console_runtime_idle_wait_ms(&runtime, &state, SDL_GetTicks64());
        if (idle_wait_ms > 0u) {
            if (SDL_WaitEventTimeout(&event, (int)idle_wait_ms)) {
                waited_event = 1;
                process_sdl_event(&event,
                                  &running,
                                  &render_ctx,
                                  &ui_ctx,
                                  &state,
                                  prefs_path_valid ? prefs_path : "",
                                  &input,
                                  &wheel_y,
                                  &keyboard_action);
            }
        }

        if (!running) {
            break;
        }

        while (SDL_PollEvent(&event)) {
            process_sdl_event(&event,
                              &running,
                              &render_ctx,
                              &ui_ctx,
                              &state,
                              prefs_path_valid ? prefs_path : "",
                              &input,
                              &wheel_y,
                              &keyboard_action);
        }

        if (!running) {
            break;
        }

        if (state.search_refresh_pending) {
            Uint64 now_ms = SDL_GetTicks64();
            if (now_ms >= state.search_last_input_ms &&
                (now_ms - state.search_last_input_ms) >= 150u) {
                state.search_refresh_pending = 0;
                refresh_and_report(&db, &state, "Search refresh failed");
            }
        }

        mem_console_runtime_tick(&runtime, &state, SDL_GetTicks64());
        if (kernel_bridge_requested) {
            uint64_t now_ns = (uint64_t)SDL_GetTicks64() * 1000000ULL;
            mem_console_kernel_bridge_tick(&kernel_bridge, &state, now_ns);
        }

        SDL_GetWindowSize(window, &frame_width, &frame_height);
        if (run_frame(&render_ctx,
                      &ui_ctx,
                      &state,
                      &input,
                      frame_width,
                      frame_height,
                      wheel_y,
                      &ui_action) != 0) {
            goto cleanup_text_input;
        }

        if (keyboard_action != MEM_CONSOLE_ACTION_NONE) {
            pending_action = keyboard_action;
        } else {
            pending_action = ui_action;
        }
        apply_pending_action(&db, &state, &runtime, pending_action);

        if (waited_event == 0 && pending_action == MEM_CONSOLE_ACTION_NONE) {
            SDL_Delay(1u);
        }
    }

    exit_code = 0;

cleanup_text_input:
    SDL_StopTextInput();
cleanup_kernel_bridge:
    mem_console_kernel_bridge_shutdown(&kernel_bridge);
cleanup_runtime:
    mem_console_runtime_shutdown(&runtime);
cleanup_render_ctx:
    kit_render_context_shutdown(&render_ctx);
cleanup_renderer:
    vk_renderer_shutdown(&renderer);
cleanup_window:
    SDL_DestroyWindow(window);
cleanup_sdl:
    SDL_Quit();
cleanup_db:
    (void)core_memdb_close(&db);
    return exit_code;
}
