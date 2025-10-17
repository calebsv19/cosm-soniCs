#include "app_state.h"
#include "config.h"
#include "engine/engine.h"
#include "session.h"
#include "sdl_app_framework.h"
#include "input/inspector_input.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/library_browser.h"
#include "ui/transport.h"

#include <SDL2/SDL.h>
#include <stdio.h>

static void handle_input(AppContext* ctx) {
    if (!ctx || !ctx->userData || !ctx->has_event) {
        return;
    }
    AppState* state = (AppState*)ctx->userData;
    input_manager_handle_event(&state->input_manager, state, &ctx->current_event);
}

static void handle_update(AppContext* ctx) {
    AppState* state = (AppState*)ctx->userData;
    if (!state) {
        return;
    }
    ui_ensure_layout(state, ctx->renderer);
    input_manager_update(&state->input_manager, state);
}

static void handle_render(AppContext* ctx) {
    AppState* state = (AppState*)ctx->userData;
    if (!state) {
        return;
    }
    SDL_Renderer* renderer = ctx->renderer;
    SDL_SetRenderDrawColor(renderer, 18, 18, 22, 255);
    SDL_RenderClear(renderer);

    ui_render_panes(renderer, state);
    ui_render_controls(renderer, state);
    ui_render_overlays(renderer, state);

    SDL_RenderPresent(renderer);
}

int main(void) {
    const int window_width = 1280;
    const int window_height = 720;
    const char* last_session_path = "config/last_session.json";

    AppState state = {0};
    if (!config_load_file("config/engine.cfg", &state.runtime_cfg)) {
        config_set_defaults(&state.runtime_cfg);
        SDL_Log("Using default audio config: sample_rate=%d block_size=%d",
                state.runtime_cfg.sample_rate, state.runtime_cfg.block_size);
    } else {
        SDL_Log("Loaded audio config: sample_rate=%d block_size=%d",
                state.runtime_cfg.sample_rate, state.runtime_cfg.block_size);
    }

    state.engine_logging_enabled = state.runtime_cfg.enable_engine_logs;
    state.cache_logging_enabled = state.runtime_cfg.enable_cache_logs;
    state.timing_logging_enabled = state.runtime_cfg.enable_timing_logs;

    ui_init_panes(&state);

    bool loaded_session = false;
    if (session_load_from_file(&state, last_session_path)) {
        SDL_Log("Session restored from %s", last_session_path);
        loaded_session = true;
    }

    if (!loaded_session) {
        SDL_Log("No previous session found; starting fresh");
        state.engine = engine_create(&state.runtime_cfg);
        if (!state.engine) {
            SDL_Log("Failed to create audio engine");
        }
        library_browser_init(&state.library, "assets/audio");
        library_browser_scan(&state.library);
        state.timeline_visible_seconds = TIMELINE_DEFAULT_VISIBLE_SECONDS;
        state.timeline_vertical_scale = 1.0f;
        state.timeline_show_all_grid_lines = false;
        state.loop_enabled = false;
        state.loop_start_frame = 0;
        state.loop_end_frame = state.runtime_cfg.sample_rate > 0 ? (uint64_t)state.runtime_cfg.sample_rate : 48000;
    }

    state.engine_logging_enabled = state.runtime_cfg.enable_engine_logs;
    state.cache_logging_enabled = state.runtime_cfg.enable_cache_logs;
    state.timing_logging_enabled = state.runtime_cfg.enable_timing_logs;

    if (state.engine) {
        engine_set_logging(state.engine,
                           state.engine_logging_enabled,
                           state.cache_logging_enabled,
                           state.timing_logging_enabled);
    }

    AppContext ctx = {0};
    if (!App_Init(&ctx, "Minimal DAW UI", window_width, window_height, true)) {
        if (state.engine) {
            engine_destroy(state.engine);
            state.engine = NULL;
        }
        return 1;
    }

    ui_layout_panes(&state, window_width, window_height);
    pane_manager_init(&state.pane_manager, state.panes, state.pane_count);
    state.drag_library_index = -1;
    state.dragging_library = false;
    input_manager_init(&state.input_manager);
    if (!loaded_session) {
        state.active_track_index = -1;
        state.selected_track_index = -1;
        state.selected_clip_index = -1;
    }
    state.timeline_drag.active = false;
    state.timeline_drag.trimming_left = false;
    state.timeline_drag.trimming_right = false;
    inspector_input_init(&state);
    state.timeline_drop_track_index = state.active_track_index >= 0 ? state.active_track_index : 0;

    ctx.userData = &state;

    AppCallbacks callbacks = {
        .handleInput = handle_input,
        .handleUpdate = handle_update,
        .handleRender = handle_render,
    };

    App_SetRenderMode(&ctx, RENDER_THROTTLED, 1.0f / 60.0f);

    bool engine_started = false;
    if (state.engine) {
        if (!engine_start(state.engine)) {
            SDL_Log("Audio engine failed to start; continuing without audio.");
        } else {
            engine_started = true;
        }
    }

    if (engine_started) {
        engine_transport_stop(state.engine);
        engine_transport_seek(state.engine, 0);
    }

    App_Run(&ctx, &callbacks);

    if (!session_save_to_file(&state, last_session_path)) {
        SDL_Log("Failed to save session to %s", last_session_path);
    }

    App_Shutdown(&ctx);

    if (state.engine) {
        engine_destroy(state.engine);
        state.engine = NULL;
    }
    return 0;
}
