#include "app_state.h"
#include "config.h"
#include "engine.h"
#include "sdl_app_framework.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/transport.h"

#include <SDL2/SDL.h>
#include <stdio.h>

static void handle_input(AppContext* ctx) {
    (void)ctx;
}

static void handle_update(AppContext* ctx) {
    AppState* state = (AppState*)ctx->userData;
    if (!state) {
        return;
    }
    ui_ensure_layout(state, ctx->renderer);

    int mouse_x = 0;
    int mouse_y = 0;
    Uint32 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    ui_layout_handle_pointer(state, state->mouse_buttons, buttons, mouse_x, mouse_y);

    state->mouse_x = mouse_x;
    state->mouse_y = mouse_y;

    pane_manager_update_hover(&state->pane_manager, mouse_x, mouse_y);
    transport_ui_update_hover(&state->transport_ui, mouse_x, mouse_y);

    bool left_was_down = (state->mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    bool left_is_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    if (!left_was_down && left_is_down && state->engine) {
        if (transport_ui_click_play(&state->transport_ui, mouse_x, mouse_y)) {
            engine_transport_play(state->engine);
        } else if (transport_ui_click_stop(&state->transport_ui, mouse_x, mouse_y)) {
            engine_transport_stop(state->engine);
        }
    }
    state->mouse_buttons = buttons;

    const Uint8* keys = SDL_GetKeyboardState(NULL);
    bool space_now = keys[SDL_SCANCODE_SPACE] != 0;
    if (space_now && !state->space_down && state->engine) {
        if (engine_transport_is_playing(state->engine)) {
            engine_transport_stop(state->engine);
        } else {
            engine_transport_play(state->engine);
        }
    }
    state->space_down = space_now;
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
