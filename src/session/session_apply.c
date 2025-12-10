#include "session.h"
#include "app_state.h"
#include "engine/engine.h"
#include "ui/library_browser.h"
#include "ui/timeline_view.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

static float clamp_ratio(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float clamp_float(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static float compute_total_seconds(const AppState* state) {
    if (!state || !state->engine) {
        return 0.0f;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : 0;
    if (sample_rate <= 0) {
        return 0.0f;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    uint64_t max_frames = 0;
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) continue;
        for (int i = 0; i < track->clip_count; ++i) {
            const EngineClip* clip = &track->clips[i];
            if (!clip) continue;
            uint64_t start = clip->timeline_start_frames;
            uint64_t length = clip->duration_frames;
            if (length == 0) {
                length = engine_clip_get_total_frames(state->engine, t, i);
            }
            uint64_t end = start + length;
            if (end > max_frames) {
                max_frames = end;
            }
        }
    }
    if (max_frames == 0) {
        return 0.0f;
    }
    return (float)max_frames / (float)sample_rate;
}

static void clear_pending_track_fx(AppState* state) {
    if (!state) {
        return;
    }
    if (state->pending_track_fx) {
        free(state->pending_track_fx);
        state->pending_track_fx = NULL;
    }
    state->pending_track_fx_count = 0;
    state->pending_track_fx_dirty = false;
}

bool session_apply_document(AppState* state, const SessionDocument* doc) {
    if (!state || !doc) {
        return false;
    }
    char error[256] = {0};
    if (!session_document_validate(doc, error, sizeof(error))) {
        SDL_Log("session_apply_document: document invalid: %s", error[0] ? error : "unknown error");
        return false;
    }

    if (state->engine) {
        engine_stop(state->engine);
        engine_destroy(state->engine);
        state->engine = NULL;
    }

    state->runtime_cfg = doc->engine;
    state->engine = engine_create(&state->runtime_cfg);
    if (!state->engine) {
        SDL_Log("session_apply_document: failed to create engine");
        return false;
    }

    int existing_tracks = engine_get_track_count(state->engine);
    while (existing_tracks > 0) {
        engine_remove_track(state->engine, existing_tracks - 1);
        existing_tracks = engine_get_track_count(state->engine);
    }

    state->timeline_visible_seconds = doc->timeline.visible_seconds;
    state->timeline_window_start_seconds = doc->timeline.window_start_seconds;
    state->timeline_vertical_scale = doc->timeline.vertical_scale;
    state->timeline_show_all_grid_lines = doc->timeline.show_all_grid_lines;

    state->layout_runtime.transport_ratio = clamp_ratio(doc->layout.transport_ratio);
    state->layout_runtime.library_ratio = clamp_ratio(doc->layout.library_ratio);
    state->layout_runtime.mixer_ratio = clamp_ratio(doc->layout.mixer_ratio);

    state->loop_enabled = doc->loop.enabled && doc->loop.end_frame > doc->loop.start_frame;
    state->loop_start_frame = doc->loop.start_frame;
    state->loop_end_frame = doc->loop.end_frame;
    state->loop_restart_pending = false;
    if (state->engine) {
        engine_transport_set_loop(state->engine, state->loop_enabled, state->loop_start_frame, state->loop_end_frame);
    }

    library_browser_init(&state->library, doc->library.directory[0] ? doc->library.directory : "assets/audio");
    library_browser_scan(&state->library);
    if (doc->library.selected_index >= 0 && doc->library.selected_index < state->library.count) {
        state->library.selected_index = doc->library.selected_index;
    } else {
        state->library.selected_index = state->library.count > 0 ? 0 : -1;
    }

    state->active_track_index = -1;
    state->selected_track_index = -1;
    state->selected_clip_index = -1;

    clear_pending_track_fx(state);
    if (doc->track_count > 0) {
        state->pending_track_fx = (PendingTrackFxEntry*)calloc((size_t)doc->track_count, sizeof(PendingTrackFxEntry));
        if (state->pending_track_fx) {
            state->pending_track_fx_count = doc->track_count;
        }
    }

    for (int t = 0; t < doc->track_count; ++t) {
        const SessionTrack* track_doc = &doc->tracks[t];
        int track_index = engine_add_track(state->engine);
        if (track_index < 0) {
            SDL_Log("session_apply_document: failed to add track %d", t);
            continue;
        }
        engine_track_set_name(state->engine, track_index, track_doc->name);
        engine_track_set_gain(state->engine, track_index, track_doc->gain == 0.0f ? 1.0f : track_doc->gain);
        engine_track_set_muted(state->engine, track_index, track_doc->muted);
        engine_track_set_solo(state->engine, track_index, track_doc->solo);

        for (int c = 0; c < track_doc->clip_count; ++c) {
            const SessionClip* clip_doc = &track_doc->clips[c];
            if (clip_doc->media_path[0] == '\0') {
                SDL_Log("session_apply_document: track %d clip %d missing media path", t, c);
                continue;
            }
            int clip_index = -1;
            if (!engine_add_clip_to_track(state->engine, track_index, clip_doc->media_path, clip_doc->start_frame, &clip_index)) {
                SDL_Log("session_apply_document: failed to load clip %s", clip_doc->media_path);
                continue;
            }
            engine_clip_set_region(state->engine, track_index, clip_index, clip_doc->offset_frames, clip_doc->duration_frames);
            engine_clip_set_gain(state->engine, track_index, clip_index, clip_doc->gain == 0.0f ? 1.0f : clip_doc->gain);
            engine_clip_set_name(state->engine, track_index, clip_index, clip_doc->name);
            engine_clip_set_fades(state->engine, track_index, clip_index, clip_doc->fade_in_frames, clip_doc->fade_out_frames);
            if (clip_doc->selected && state->selected_track_index == -1) {
                state->selected_track_index = track_index;
                state->selected_clip_index = clip_index;
                state->active_track_index = track_index;
            }
        }

        if (state->pending_track_fx && t < state->pending_track_fx_count) {
            PendingTrackFxEntry* pending = &state->pending_track_fx[t];
            int count = track_doc->fx_count;
            if (count > FX_MASTER_MAX) count = FX_MASTER_MAX;
            pending->fx_count = count;
            for (int f = 0; f < count; ++f) {
                pending->fx[f] = track_doc->fx[f];
            }
        }
    }

    if (state->selected_track_index == -1 && doc->track_count > 0) {
        state->active_track_index = 0;
        state->selected_track_index = 0;
        state->selected_clip_index = doc->tracks[0].clip_count > 0 ? 0 : -1;
    }

    memset(state->pending_master_fx, 0, sizeof(state->pending_master_fx));
    state->pending_master_fx_count = 0;
    state->pending_master_fx_dirty = false;
    if (doc->master_fx_count > 0) {
        int count = doc->master_fx_count;
        if (count > FX_MASTER_MAX) {
            count = FX_MASTER_MAX;
        }
        state->pending_master_fx_count = count;
        for (int i = 0; i < count; ++i) {
            PendingMasterFx* dst = &state->pending_master_fx[i];
            const SessionFxInstance* src = &doc->master_fx[i];
            dst->type_id = src->type;
            dst->enabled = src->enabled;
            dst->param_count = src->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : src->param_count;
            for (uint32_t p = 0; p < dst->param_count; ++p) {
                dst->param_values[p] = src->params[p];
            }
        }
        state->pending_master_fx_dirty = true;
    }

    if (state->pending_track_fx_count > 0 && state->pending_track_fx) {
        state->pending_track_fx_dirty = true;
    }

    state->timeline_visible_seconds = clamp_float(state->timeline_visible_seconds,
                                                  TIMELINE_MIN_VISIBLE_SECONDS,
                                                  TIMELINE_MAX_VISIBLE_SECONDS);
    float total_seconds = compute_total_seconds(state);
    float max_start = total_seconds > state->timeline_visible_seconds
                          ? total_seconds - state->timeline_visible_seconds
                          : 0.0f;
    if (max_start < 0.0f) {
        max_start = 0.0f;
    }
    state->timeline_window_start_seconds = clamp_float(state->timeline_window_start_seconds, 0.0f, max_start);

    return true;
}

void session_apply_pending_master_fx(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    if (state->pending_master_fx_count <= 0) {
        state->pending_master_fx_dirty = false;
        return;
    }

    FxMasterSnapshot existing = {0};
    if (engine_fx_master_snapshot(state->engine, &existing)) {
        for (int i = 0; i < existing.count; ++i) {
            engine_fx_master_remove(state->engine, existing.items[i].id);
        }
    }

    for (int i = 0; i < state->pending_master_fx_count && i < FX_MASTER_MAX; ++i) {
        const PendingMasterFx* fx = &state->pending_master_fx[i];
        if (fx->type_id == 0) {
            continue;
        }
        FxInstId id = engine_fx_master_add(state->engine, fx->type_id);
        if (!id) {
            continue;
        }
        uint32_t count = fx->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : fx->param_count;
        for (uint32_t p = 0; p < count; ++p) {
            engine_fx_master_set_param(state->engine, id, p, fx->param_values[p]);
        }
        if (!fx->enabled) {
            engine_fx_master_set_enabled(state->engine, id, false);
        }
    }
    state->pending_master_fx_dirty = false;
}

void session_apply_pending_track_fx(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    if (!state->pending_track_fx_dirty || state->pending_track_fx_count <= 0 || !state->pending_track_fx) {
        state->pending_track_fx_dirty = false;
        return;
    }

    engine_fx_set_track_count(state->engine, engine_get_track_count(state->engine));
    int track_count = engine_get_track_count(state->engine);
    for (int t = 0; t < state->pending_track_fx_count && t < track_count; ++t) {
        const PendingTrackFxEntry* pending = &state->pending_track_fx[t];
        if (!pending || pending->fx_count <= 0) {
            continue;
        }
        for (int f = 0; f < pending->fx_count && f < FX_MASTER_MAX; ++f) {
            const SessionFxInstance* fx = &pending->fx[f];
            if (!fx || fx->type == 0) {
                continue;
            }
            FxInstId id = engine_fx_track_add(state->engine, t, fx->type);
            if (id == 0) {
                continue;
            }
            uint32_t pcount = fx->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : fx->param_count;
            for (uint32_t p = 0; p < pcount; ++p) {
                engine_fx_track_set_param(state->engine, t, id, p, fx->params[p]);
            }
            if (!fx->enabled) {
                engine_fx_track_set_enabled(state->engine, t, id, false);
            }
        }
    }
    state->pending_track_fx_dirty = false;
}

bool session_load_from_file(AppState* state, const char* path) {
    if (!state) {
        SDL_Log("session_load_from_file: app state is null");
        return false;
    }
    SessionDocument doc;
    session_document_init(&doc);
    if (!session_document_read_file(path, &doc)) {
        session_document_free(&doc);
        return false;
    }
    bool applied = session_apply_document(state, &doc);
    session_document_free(&doc);
    return applied;
}
