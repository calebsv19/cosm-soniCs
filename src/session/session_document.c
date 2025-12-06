#include "session.h"
#include "app_state.h"
#include "engine/engine.h"

#include <SDL2/SDL.h>

#include <stdlib.h>
#include <string.h>

static void copy_string(char* dst, size_t dst_len, const char* src) {
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

void session_document_init(SessionDocument* doc) {
    if (!doc) {
        return;
    }
    memset(doc, 0, sizeof(*doc));
    doc->version = SESSION_DOCUMENT_VERSION;
    config_set_defaults(&doc->engine);
    doc->master_fx = NULL;
    doc->master_fx_count = 0;
}

void session_document_free(SessionDocument* doc) {
    if (!doc) {
        return;
    }
    if (doc->tracks) {
        for (int i = 0; i < doc->track_count; ++i) {
            if (doc->tracks[i].clips) {
                free(doc->tracks[i].clips);
                doc->tracks[i].clips = NULL;
            }
            if (doc->tracks[i].fx) {
                free(doc->tracks[i].fx);
                doc->tracks[i].fx = NULL;
            }
            doc->tracks[i].clip_count = 0;
            doc->tracks[i].fx_count = 0;
        }
        free(doc->tracks);
    }
    doc->tracks = NULL;
    doc->track_count = 0;
    if (doc->master_fx) {
        free(doc->master_fx);
        doc->master_fx = NULL;
    }
    doc->master_fx_count = 0;
}

void session_document_reset(SessionDocument* doc) {
    if (!doc) {
        return;
    }
    session_document_free(doc);
    session_document_init(doc);
}

static int count_active_tracks(const EngineTrack* tracks, int track_count) {
    if (!tracks || track_count <= 0) {
        return 0;
    }
    int active = 0;
    for (int i = 0; i < track_count; ++i) {
        if (tracks[i].active) {
            ++active;
        }
    }
    return active;
}

static int count_active_clips(const EngineTrack* track) {
    if (!track || track->clip_count <= 0 || !track->clips) {
        return 0;
    }
    int active = 0;
    for (int i = 0; i < track->clip_count; ++i) {
        const EngineClip* clip = &track->clips[i];
        if (clip->active && clip->media) {
            ++active;
        }
    }
    return active;
}

bool session_document_capture(const AppState* state, SessionDocument* out_doc) {
    if (!state || !out_doc) {
        return false;
    }

    session_document_reset(out_doc);
    out_doc->version = SESSION_DOCUMENT_VERSION;

    const EngineRuntimeConfig* runtime_cfg = NULL;
    if (state->engine) {
        runtime_cfg = engine_get_config(state->engine);
    }
    if (runtime_cfg) {
        out_doc->engine = *runtime_cfg;
    } else {
        out_doc->engine = state->runtime_cfg;
    }

    out_doc->transport_playing = state->engine ? engine_transport_is_playing(state->engine) : false;
    out_doc->transport_frame = state->engine ? engine_get_transport_frame(state->engine) : 0;

    out_doc->loop.enabled = state->loop_enabled && state->loop_end_frame > state->loop_start_frame;
    out_doc->loop.start_frame = state->loop_start_frame;
    out_doc->loop.end_frame = state->loop_end_frame;

    out_doc->timeline.visible_seconds = state->timeline_visible_seconds;
    out_doc->timeline.vertical_scale = state->timeline_vertical_scale;
    out_doc->timeline.show_all_grid_lines = state->timeline_show_all_grid_lines;
    out_doc->timeline.playhead_frame = out_doc->transport_frame;

    out_doc->layout.transport_ratio = state->layout_runtime.transport_ratio;
    out_doc->layout.library_ratio = state->layout_runtime.library_ratio;
    out_doc->layout.mixer_ratio = state->layout_runtime.mixer_ratio;

    copy_string(out_doc->library.directory, sizeof(out_doc->library.directory), state->library.directory);
    out_doc->library.selected_index = state->library.selected_index;

    int engine_track_count = state->engine ? engine_get_track_count(state->engine) : 0;
    const EngineTrack* engine_tracks = state->engine ? engine_get_tracks(state->engine) : NULL;
    int active_tracks = count_active_tracks(engine_tracks, engine_track_count);
    if (active_tracks > 0) {
        out_doc->tracks = (SessionTrack*)calloc((size_t)active_tracks, sizeof(SessionTrack));
        if (!out_doc->tracks) {
            SDL_Log("session_document_capture: failed to allocate %d tracks", active_tracks);
            session_document_reset(out_doc);
            return false;
        }
    }
    out_doc->track_count = active_tracks;

    int track_out = 0;
    for (int t = 0; t < engine_track_count; ++t) {
        const EngineTrack* src_track = &engine_tracks[t];
        if (!src_track->active) {
            continue;
        }
        SessionTrack* dst_track = &out_doc->tracks[track_out++];
        copy_string(dst_track->name, sizeof(dst_track->name), src_track->name);
        dst_track->gain = src_track->gain;
        dst_track->muted = src_track->muted;
        dst_track->solo = src_track->solo;
        dst_track->fx = NULL;
        dst_track->fx_count = 0;

        int active_clips = count_active_clips(src_track);
        dst_track->clip_count = active_clips;
        if (active_clips > 0) {
            dst_track->clips = (SessionClip*)calloc((size_t)active_clips, sizeof(SessionClip));
            if (!dst_track->clips) {
                SDL_Log("session_document_capture: failed to allocate %d clips for track %d", active_clips, t);
                session_document_reset(out_doc);
                return false;
            }
        }

        int clip_out = 0;
        for (int c = 0; c < src_track->clip_count; ++c) {
            const EngineClip* src_clip = &src_track->clips[c];
            if (!src_clip->active || !src_clip->media) {
                continue;
            }
            SessionClip* dst_clip = &dst_track->clips[clip_out++];
            copy_string(dst_clip->name, sizeof(dst_clip->name), src_clip->name);
            copy_string(dst_clip->media_path, sizeof(dst_clip->media_path), src_clip->media_path);
            dst_clip->start_frame = src_clip->timeline_start_frames;
            dst_clip->duration_frames = src_clip->duration_frames;
            dst_clip->offset_frames = src_clip->offset_frames;
            dst_clip->fade_in_frames = src_clip->fade_in_frames;
            dst_clip->fade_out_frames = src_clip->fade_out_frames;
            dst_clip->gain = src_clip->gain;
            dst_clip->selected = src_clip->selected;
        }

        FxMasterSnapshot track_fx = {0};
        if (state->engine && engine_fx_track_snapshot(state->engine, t, &track_fx) && track_fx.count > 0) {
            int fx_count = track_fx.count;
            if (fx_count > FX_MASTER_MAX) {
                fx_count = FX_MASTER_MAX;
            }
            SessionFxInstance* fx = (SessionFxInstance*)calloc((size_t)fx_count, sizeof(SessionFxInstance));
            if (!fx) {
                SDL_Log("session_document_capture: failed to allocate fx for track %d", t);
                session_document_reset(out_doc);
                return false;
            }
            dst_track->fx = fx;
            dst_track->fx_count = fx_count;
            for (int i = 0; i < fx_count; ++i) {
                SessionFxInstance* dst_fx = &fx[i];
                const FxMasterInstanceInfo* src = &track_fx.items[i];
                dst_fx->type = src->type;
                dst_fx->enabled = src->enabled;
                dst_fx->param_count = src->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : src->param_count;
                for (uint32_t p = 0; p < dst_fx->param_count; ++p) {
                    dst_fx->params[p] = src->params[p];
                }
                dst_fx->name[0] = '\0';
                if (state->engine) {
                    FxDesc desc = {0};
                    if (engine_fx_registry_get_desc(state->engine, dst_fx->type, &desc) && desc.name) {
                        strncpy(dst_fx->name, desc.name, SESSION_FX_NAME_MAX - 1);
                        dst_fx->name[SESSION_FX_NAME_MAX - 1] = '\0';
                    }
                }
            }
        }
    }

    out_doc->master_fx_count = 0;
    out_doc->master_fx = NULL;
    FxMasterSnapshot snap = {0};
    if (state->engine && engine_fx_master_snapshot(state->engine, &snap) && snap.count > 0) {
        int count = snap.count;
        if (count > FX_MASTER_MAX) {
            count = FX_MASTER_MAX;
        }
        SessionFxInstance* fx = (SessionFxInstance*)calloc((size_t)count, sizeof(SessionFxInstance));
        if (!fx) {
            SDL_Log("session_document_capture: failed to allocate master fx array");
            session_document_reset(out_doc);
            return false;
        }
        out_doc->master_fx = fx;
        out_doc->master_fx_count = count;
        for (int i = 0; i < count; ++i) {
            SessionFxInstance* dst = &fx[i];
            const FxMasterInstanceInfo* src = &snap.items[i];
            dst->type = src->type;
            dst->enabled = src->enabled;
            dst->param_count = src->param_count > FX_MAX_PARAMS ? FX_MAX_PARAMS : src->param_count;
            for (uint32_t p = 0; p < dst->param_count; ++p) {
                dst->params[p] = src->params[p];
            }
            dst->name[0] = '\0';
            if (state->engine) {
                FxDesc desc = {0};
                if (engine_fx_registry_get_desc(state->engine, dst->type, &desc) && desc.name) {
                    strncpy(dst->name, desc.name, SESSION_FX_NAME_MAX - 1);
                    dst->name[SESSION_FX_NAME_MAX - 1] = '\0';
                }
            }
        }
    }

    return true;
}
