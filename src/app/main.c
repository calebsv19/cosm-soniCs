#include "app_state.h"
#include "config.h"
#include "engine.h"
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

    AppState state = {0};
    if (!config_load_file("config/engine.cfg", &state.runtime_cfg)) {
        config_set_defaults(&state.runtime_cfg);
        SDL_Log("Using default audio config: sample_rate=%d block_size=%d",
                state.runtime_cfg.sample_rate, state.runtime_cfg.block_size);
    } else {
        SDL_Log("Loaded audio config: sample_rate=%d block_size=%d",
                state.runtime_cfg.sample_rate, state.runtime_cfg.block_size);
    }

    state.engine = engine_create(&state.runtime_cfg);
    if (!state.engine) {
        SDL_Log("Failed to create audio engine");
    }

    AppContext ctx = {0};
    if (!App_Init(&ctx, "Minimal DAW UI", window_width, window_height, true)) {
        if (state.engine) {
            engine_destroy(state.engine);
            state.engine = NULL;
        }
        return 1;
    }

    ui_init_panes(&state);
    ui_layout_panes(&state, window_width, window_height);
    pane_manager_init(&state.pane_manager, state.panes, state.pane_count);
    library_browser_init(&state.library, "assets/audio");
    library_browser_scan(&state.library);
    state.drag_library_index = -1;
    state.dragging_library = false;
    input_manager_init(&state.input_manager);
    state.active_track_index = -1;
    state.selected_track_index = -1;
    state.selected_clip_index = -1;
    state.timeline_drag.active = false;
    state.timeline_drag.trimming_left = false;
    state.timeline_drag.trimming_right = false;
    inspector_input_init(&state);
    state.timeline_visible_seconds = TIMELINE_DEFAULT_VISIBLE_SECONDS;
    state.timeline_vertical_scale = 1.0f;
    state.timeline_show_all_grid_lines = false;
    state.timeline_drop_track_index = 0;
    state.loop_enabled = false;
    state.loop_start_frame = 0;
    state.loop_end_frame = state.runtime_cfg.sample_rate > 0 ? (uint64_t)state.runtime_cfg.sample_rate : 48000;

    ctx.userData = &state;

    AppCallbacks callbacks = {
        .handleInput = handle_input,
        .handleUpdate = handle_update,
        .handleRender = handle_render,
    };

    App_SetRenderMode(&ctx, RENDER_THROTTLED, 1.0f / 60.0f);

    if (state.engine && !engine_start(state.engine)) {
        SDL_Log("Audio engine failed to start; continuing without audio.");
    }

    App_Run(&ctx, &callbacks);
    App_Shutdown(&ctx);

    if (state.engine) {
        engine_destroy(state.engine);
        state.engine = NULL;
    }
    return 0;
}
