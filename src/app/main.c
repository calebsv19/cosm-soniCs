#include "app_state.h"
#include "config.h"
#include "engine/engine.h"
#include "session.h"
#include "sdl_app_framework.h"
#include "input/inspector_input.h"
#include "input/effects_panel_input.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/library_browser.h"
#include "ui/transport.h"
#include "ui/effects_panel.h"
#include "ui/font.h"
#include "session/project_manager.h"
#include "time/tempo.h"
#include "render/timer_hud_adapter.h"
#include "timer_hud/time_scope.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void handle_render(AppContext* ctx);
static void perform_bounce(AppContext* ctx, AppState* state);

static void handle_input(AppContext* ctx) {
    if (!ctx || !ctx->userData || !ctx->has_event) {
        return;
    }
    AppState* state = (AppState*)ctx->userData;
    if (state->bounce_active) {
        return;
    }
    input_manager_handle_event(&state->input_manager, state, &ctx->current_event);
}

static void handle_update(AppContext* ctx) {
    AppState* state = (AppState*)ctx->userData;
    if (!state) {
        return;
    }
    if (state->bounce_requested && !state->bounce_active) {
        perform_bounce(ctx, state);
        return;
    }
    if (state->bounce_active) {
        return;
    }
    ui_ensure_layout(state, ctx->window, ctx->renderer);
    input_manager_update(&state->input_manager, state);
}

static void handle_render(AppContext* ctx) {
    AppState* state = (AppState*)ctx->userData;
    if (!state) {
        return;
    }
    SDL_Renderer* renderer = ctx->renderer;

    ui_render_panes(renderer, state);
    ui_render_controls(renderer, state);
    ui_render_overlays(renderer, state);
    ts_render();
}

static bool path_exists(const char* path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static bool next_bounce_path(char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    const char* dir = "assets/audio";
    for (int i = 0; i < 10000; ++i) {
        char name[64];
        if (i == 0) {
            snprintf(name, sizeof(name), "bounce.wav");
        } else {
            snprintf(name, sizeof(name), "bounce%d.wav", i);
        }
        snprintf(out, len, "%s/%s", dir, name);
        if (!path_exists(out)) {
            return true;
        }
    }
    return false;
}

static uint64_t find_project_end_frame(const Engine* engine) {
    if (!engine) return 0;
    const EngineTrack* tracks = engine_get_tracks(engine);
    int track_count = engine_get_track_count(engine);
    uint64_t max_end = 0;
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track || track->clip_count <= 0) {
            continue;
        }
        for (int c = 0; c < track->clip_count; ++c) {
            const EngineClip* clip = &track->clips[c];
            if (!clip || !clip->active) {
                continue;
            }
            uint64_t len = engine_clip_get_total_frames(engine, t, c);
            uint64_t end = clip->timeline_start_frames + len;
            if (end > max_end) {
                max_end = end;
            }
        }
    }
    return max_end;
}

typedef struct {
    AppState* state;
    AppContext* ctx;
    Uint32 last_render_ms;
    Uint32 render_interval_ms;
} BounceProgressCtx;

static void bounce_progress_cb(uint64_t done_frames, uint64_t total_frames, void* user) {
    BounceProgressCtx* prog = (BounceProgressCtx*)user;
    if (!prog || !prog->state) {
        return;
    }
    prog->state->bounce_progress_frames = done_frames;
    prog->state->bounce_total_frames = total_frames;
    if (prog->ctx && prog->ctx->renderer) {
        Uint32 now = SDL_GetTicks();
        if (now - prog->last_render_ms >= prog->render_interval_ms) {
            prog->last_render_ms = now;
            App_RenderOnce(prog->ctx, handle_render);
        }
    }
}

static void perform_bounce(AppContext* ctx, AppState* state) {
    if (!ctx || !state || !state->engine) {
        return;
    }

    uint64_t start_frame = 0;
    uint64_t end_frame = 0;
    if (state->loop_enabled && state->loop_end_frame > state->loop_start_frame) {
        start_frame = state->loop_start_frame;
        end_frame = state->loop_end_frame;
    } else {
        start_frame = 0;
        end_frame = find_project_end_frame(state->engine);
        if (end_frame == 0) {
            SDL_Log("Bounce aborted: no clips found.");
            state->bounce_requested = false;
            return;
        }
    }

    char path[512];
    if (!next_bounce_path(path, sizeof(path))) {
        SDL_Log("Bounce aborted: unable to allocate output filename.");
        state->bounce_requested = false;
        return;
    }

    state->bounce_active = true;
    state->bounce_progress_frames = 0;
    state->bounce_total_frames = end_frame > start_frame ? end_frame - start_frame : 0;
    state->bounce_start_frame = start_frame;
    state->bounce_end_frame = end_frame;

    SDL_Log("Bounce started: %s", path);
    BounceProgressCtx prog = {
        .state = state,
        .ctx = ctx,
        .last_render_ms = SDL_GetTicks(),
        .render_interval_ms = 50
    };
    bool ok = engine_bounce_range(state->engine,
                                  start_frame,
                                  end_frame,
                                  path,
                                  bounce_progress_cb,
                                  &prog);

    state->bounce_active = false;
    state->bounce_requested = false;
    state->bounce_progress_frames = 0;
    state->bounce_total_frames = 0;
    state->bounce_start_frame = 0;
    state->bounce_end_frame = 0;

    if (ok) {
        library_browser_scan(&state->library, &state->media_registry);
        SDL_Log("Bounce completed: %s", path);
    } else {
        SDL_Log("Bounce failed");
    }
}

int main(void) {
    const int window_width = 1280;
    const int window_height = 720;
    const char* last_session_path = "config/last_session.json";

    AppState state = {0};
    state.timeline_snap_enabled = true;
    state.automation_ui.target = ENGINE_AUTOMATION_TARGET_VOLUME;
    state.automation_ui.track_index = -1;
    state.automation_ui.clip_index = -1;
    state.automation_ui.point_index = -1;
    state.reset_meter_history_on_seek = true;
    waveform_cache_init(&state.waveform_cache);
    undo_manager_init(&state.undo);
    media_registry_init(&state.media_registry, "config/library_index.json");
    media_registry_load(&state.media_registry);
    if (!config_load_file("config/engine.cfg", &state.runtime_cfg)) {
        config_set_defaults(&state.runtime_cfg);
        SDL_Log("Using default audio config: sample_rate=%d block_size=%d",
                state.runtime_cfg.sample_rate, state.runtime_cfg.block_size);
    } else {
        SDL_Log("Loaded audio config: sample_rate=%d block_size=%d",
                state.runtime_cfg.sample_rate, state.runtime_cfg.block_size);
    }
    state.tempo = tempo_state_default(state.runtime_cfg.sample_rate);

    state.engine_logging_enabled = state.runtime_cfg.enable_engine_logs;
    state.cache_logging_enabled = state.runtime_cfg.enable_cache_logs;
    state.timing_logging_enabled = state.runtime_cfg.enable_timing_logs;

    ui_init_panes(&state);
    project_manager_init();

    bool loaded_session = false;
    bool engine_started = false;
    if (project_manager_load_last(&state)) {
        SDL_Log("Project restored from last saved project");
        loaded_session = true;
        engine_started = project_manager_post_load(&state);
    } else if (session_load_from_file(&state, "config/last_session.json")) {
        SDL_Log("Session restored from config/last_session.json");
        loaded_session = true;
        engine_started = project_manager_post_load(&state);
    }

    if (!loaded_session) {
        SDL_Log("No previous session found; starting fresh");
        state.engine = engine_create(&state.runtime_cfg);
        if (!state.engine) {
            SDL_Log("Failed to create audio engine");
        }
        library_browser_init(&state.library, "assets/audio");
        library_browser_scan(&state.library, &state.media_registry);
        state.timeline_visible_seconds = TIMELINE_DEFAULT_VISIBLE_SECONDS;
        state.timeline_window_start_seconds = 0.0f;
        state.timeline_vertical_scale = 1.0f;
        state.timeline_view_in_beats = false;
        state.timeline_show_all_grid_lines = false;
        state.timeline_snap_enabled = true;
        state.timeline_follow_mode = TIMELINE_FOLLOW_JUMP;
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

    if (TTF_Init() != 0) {
        SDL_Log("TTF_Init failed: %s", TTF_GetError());
        App_Shutdown(&ctx);
        if (state.engine) {
            engine_destroy(state.engine);
            state.engine = NULL;
        }
        return 1;
    }
    ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf", 9);
    timer_hud_register_backend();
    timer_hud_bind_context(&ctx);
    ts_init();

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
    if (!loaded_session) {
        inspector_input_init(&state);
    }
    effects_panel_input_init(&state);
    state.timeline_drop_track_index = state.active_track_index >= 0 ? state.active_track_index : 0;

    ctx.userData = &state;

    AppCallbacks callbacks = {
        .handleInput = handle_input,
        .handleUpdate = handle_update,
        .handleRender = handle_render,
    };

    App_SetRenderMode(&ctx, RENDER_THROTTLED, 1.0f / 60.0f);

    if (!engine_started && state.engine) {
        if (!engine_start(state.engine)) {
            SDL_Log("Audio engine failed to start; continuing without audio.");
        } else {
            engine_started = true;
            session_apply_pending_master_fx(&state);
            session_apply_pending_track_fx(&state);
            effects_panel_sync_from_engine(&state);
        }
    }

    if (engine_started) {
        engine_transport_stop(state.engine);
        engine_transport_seek(state.engine, 0);
    }

    App_Run(&ctx, &callbacks);

    if (state.project.has_name) {
        project_manager_save(&state, state.project.name, true);
    } else if (!session_save_to_file(&state, last_session_path)) {
        SDL_Log("Failed to save session to %s", last_session_path);
    }

    ui_font_shutdown();
    ts_shutdown();
    waveform_cache_shutdown(&state.waveform_cache);
    undo_manager_free(&state.undo);
    media_registry_shutdown(&state.media_registry);
    TTF_Quit();
    App_Shutdown(&ctx);

    if (state.engine) {
        engine_destroy(state.engine);
        state.engine = NULL;
    }
    return 0;
}
