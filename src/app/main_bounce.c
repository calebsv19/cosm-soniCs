#include "app/media_import.h"
#include "app/main_bounce.h"

#include "app/bounce_region.h"
#include "export/daw_pack_export.h"
#include "ui/library_browser.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Finds the final authored region boundary used by the default exact-range bounce.
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
            uint64_t end = len > UINT64_MAX - clip->timeline_start_frames ? UINT64_MAX :
                           clip->timeline_start_frames + len;
            if (end > max_end) {
                max_end = end;
            }
        }
    }
    return max_end;
}

// Holds progress presentation state while the independent export snapshot renders.
typedef struct {
    AppState* state;
    AppContext* ctx;
    void (*handle_render)(AppContext* ctx);
    Uint32 last_render_ms;
    Uint32 render_interval_ms;
    DawPackEnvelope envelope;
    bool cancelled;
} BounceProgressCtx;

// Updates progress and redraws without processing project input events.
static bool bounce_progress_cb(uint64_t done_frames, uint64_t total_frames, void* user) {
    BounceProgressCtx* prog = (BounceProgressCtx*)user;
    if (!prog || !prog->state) {
        return false;
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
    SDL_PumpEvents();
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    prog->cancelled = keys[SDL_SCANCODE_ESCAPE] || SDL_HasEvent(SDL_QUIT);
    return !prog->cancelled;
}

// Builds the optional pack overview from normalized chunks without retaining full bounce audio.
static void bounce_samples_cb(const float* samples, uint64_t first, uint32_t frames, int channels, void* user) {
    BounceProgressCtx* prog = user;
    daw_pack_envelope_append(&prog->envelope, samples, first, frames, channels);
}

// Exports the current selected/project range and reports optional pack/insertion outcomes separately.
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
    bool have_envelope = daw_pack_envelope_init(&prog.envelope, end_frame - start_frame,
                                                engine_get_config(state->engine)->sample_rate, 2);
    EngineBounceStreamCallbacks callbacks = {.progress = bounce_progress_cb, .samples = bounce_samples_cb, .user = &prog};
    DawSaveResult saved = engine_bounce_range_to_wav(state->engine, start_frame, end_frame, NULL, path,
                                                    ENGINE_BOUNCE_WAV_PCM16, &callbacks);
    bool ok = saved == DAW_SAVE_SYNCED;
    if (saved == DAW_SAVE_PUBLISHED) SDL_Log("Bounce WAV published, but directory durability is uncertain: %s", path);
    if (ok) {
        uint64_t project_duration_frames = find_project_end_frame(state->engine);
        char pack_path[512];
        if (daw_pack_path_from_wav(path, pack_path, sizeof(pack_path))) {
            if (have_envelope && daw_pack_export_envelope(pack_path,
                                            state,
                                            &prog.envelope,
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
        library_browser_scan(&state->library, &state->media_registry);
        (void)daw_media_import_submit(state, path, NULL, engine_get_track_count(state->engine),
                                      start_frame, AUDIO_MEDIA_JOB_BOUNCE);
    }
    daw_pack_envelope_free(&prog.envelope);

    state->bounce_active = false;
    state->bounce_requested = false;
    state->bounce_progress_frames = 0;
    state->bounce_total_frames = 0;
    state->bounce_start_frame = 0;
    state->bounce_end_frame = 0;

    if (ok) {
        SDL_Log("Bounce completed: %s (insertion asynchronous)", path);
    } else {
        SDL_Log("%s", prog.cancelled ? "Bounce cancelled before publication" : "Bounce failed");
    }
}
