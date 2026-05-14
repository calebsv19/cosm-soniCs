#include "app/main_bounce.h"

#include "app/bounce_region.h"
#include "audio/wav_writer.h"
#include "core_time.h"
#include "export/daw_pack_export.h"
#include "ui/library_browser.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static uint64_t find_project_end_frame(const Engine* engine) {
    if (!engine) {
        return 0;
    }
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
    void (*handle_render)(AppContext* ctx);
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
    if (prog->ctx && prog->ctx->renderer && prog->handle_render) {
        Uint32 now = SDL_GetTicks();
        if (now - prog->last_render_ms >= prog->render_interval_ms) {
            prog->last_render_ms = now;
            App_RenderOnce(prog->ctx, prog->handle_render);
        }
    }
}

void perform_bounce(AppContext* ctx, AppState* state, void (*handle_render)(AppContext* ctx)) {
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
    if (!daw_bounce_next_path_for_state(state, path, sizeof(path))) {
        SDL_Log("Bounce aborted: unable to allocate output filename under library root %s.",
                daw_bounce_library_root(state));
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
        .handle_render = handle_render,
        .last_render_ms = SDL_GetTicks(),
        .render_interval_ms = 50
    };
    EngineBounceBuffer bounce = {0};
    bool ok = engine_bounce_range_to_buffer(state->engine,
                                            start_frame,
                                            end_frame,
                                            bounce_progress_cb,
                                            &prog,
                                            &bounce);
    if (ok) {
        char float_path[512];
        snprintf(float_path, sizeof(float_path), "%s.f32.wav", path);
        bool ok_float = wav_write_f32(float_path,
                                      bounce.data,
                                      bounce.frame_count,
                                      bounce.channels,
                                      bounce.sample_rate);
        uint32_t dither_seed = (uint32_t)(core_time_now_ns() & 0xffffffffu);
        ok = wav_write_pcm16_dithered(path,
                                      bounce.data,
                                      bounce.frame_count,
                                      bounce.channels,
                                      bounce.sample_rate,
                                      dither_seed);
        (void)ok_float;

        if (ok) {
            uint64_t project_duration_frames = find_project_end_frame(state->engine);
            char pack_path[512];
            if (daw_pack_path_from_wav(path, pack_path, sizeof(pack_path))) {
                if (daw_pack_export_from_bounce(pack_path,
                                                state,
                                                &bounce,
                                                start_frame,
                                                end_frame,
                                                project_duration_frames)) {
                    SDL_Log("Bounce pack exported: %s", pack_path);
                } else {
                    SDL_Log("Bounce pack export warning: failed to write %s", pack_path);
                }
            } else {
                SDL_Log("Bounce pack export warning: failed to build pack path for %s", path);
            }
            int bounce_track = -1;
            int bounce_clip = -1;
            if (daw_bounce_insert_audio_track(state, path, start_frame, &bounce_track, &bounce_clip)) {
                SDL_Log("Bounce region inserted: track=%d clip=%d", bounce_track, bounce_clip);
            } else {
                SDL_Log("Bounce region insert skipped for %s", path);
            }
        }
    }
    engine_bounce_buffer_free(&bounce);

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
