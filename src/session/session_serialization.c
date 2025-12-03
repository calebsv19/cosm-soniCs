#include "session.h"

#include "app_state.h"
#include "engine/engine.h"

#include <SDL2/SDL.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
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
            doc->tracks[i].clip_count = 0;
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

static void session_set_error(char* buffer, size_t buffer_len, const char* fmt, ...) {
    if (!buffer || buffer_len == 0 || !fmt) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, buffer_len, fmt, args);
    va_end(args);
}

bool session_document_validate(const SessionDocument* doc, char* error_message, size_t error_message_len) {
    if (!doc) {
        session_set_error(error_message, error_message_len, "document is null");
        return false;
    }
    if (doc->version == 0) {
        session_set_error(error_message, error_message_len, "version is 0");
        return false;
    }
    if (doc->engine.sample_rate <= 0) {
        session_set_error(error_message, error_message_len, "invalid sample rate: %d", doc->engine.sample_rate);
        return false;
    }
    if (doc->engine.block_size <= 0) {
        session_set_error(error_message, error_message_len, "invalid block size: %d", doc->engine.block_size);
        return false;
    }
    if (doc->engine.default_fade_in_ms < 0.0f || doc->engine.default_fade_out_ms < 0.0f) {
        session_set_error(error_message, error_message_len, "default fades must be non-negative");
        return false;
    }
    if (doc->engine.fade_preset_count < 0 || doc->engine.fade_preset_count > CONFIG_FADE_PRESET_MAX) {
        session_set_error(error_message, error_message_len, "invalid fade preset count: %d", doc->engine.fade_preset_count);
        return false;
    }
    for (int i = 0; i < doc->engine.fade_preset_count; ++i) {
        if (doc->engine.fade_preset_ms[i] < 0.0f) {
            session_set_error(error_message, error_message_len, "fade preset %d negative", i);
            return false;
        }
    }
    if (doc->loop.enabled && doc->loop.end_frame <= doc->loop.start_frame) {
        session_set_error(error_message, error_message_len, "loop end <= start");
        return false;
    }
    if (doc->track_count < 0) {
        session_set_error(error_message, error_message_len, "negative track count");
        return false;
    }
    for (int t = 0; t < doc->track_count; ++t) {
        const SessionTrack* track = &doc->tracks[t];
        if (!track) {
            session_set_error(error_message, error_message_len, "track %d is null", t);
            return false;
        }
        if (track->clip_count < 0) {
            session_set_error(error_message, error_message_len, "track %d has negative clip count", t);
            return false;
        }
        if (track->clip_count > 0 && !track->clips) {
            session_set_error(error_message, error_message_len, "track %d clips missing", t);
            return false;
        }
        for (int c = 0; c < track->clip_count; ++c) {
            const SessionClip* clip = &track->clips[c];
            if (!clip) {
                session_set_error(error_message, error_message_len, "track %d clip %d is null", t, c);
                return false;
            }
            if (clip->media_path[0] == '\0') {
                session_set_error(error_message, error_message_len, "track %d clip %d missing media path", t, c);
                return false;
            }
            if (clip->duration_frames == 0) {
                session_set_error(error_message, error_message_len, "track %d clip %d has zero duration", t, c);
                return false;
            }
            if (clip->fade_in_frames > clip->duration_frames) {
                session_set_error(error_message, error_message_len, "track %d clip %d fade-in exceeds duration", t, c);
                return false;
            }
            if (clip->fade_out_frames > clip->duration_frames) {
                session_set_error(error_message, error_message_len, "track %d clip %d fade-out exceeds duration", t, c);
                return false;
            }
        }
    }
    if (doc->master_fx_count < 0 || doc->master_fx_count > FX_MASTER_MAX) {
        session_set_error(error_message, error_message_len, "invalid master fx count %d", doc->master_fx_count);
        return false;
    }
    if (doc->master_fx_count > 0 && !doc->master_fx) {
        session_set_error(error_message, error_message_len, "master fx array missing");
        return false;
    }
    for (int i = 0; i < doc->master_fx_count; ++i) {
        const SessionFxInstance* fx = &doc->master_fx[i];
        if (fx->param_count > FX_MAX_PARAMS) {
            session_set_error(error_message, error_message_len, "master fx %d param count too large", i);
            return false;
        }
    }
    return true;
}

static void json_write_indent(FILE* file, int level) {
    for (int i = 0; i < level; ++i) {
        fputs("  ", file);
    }
}

static void json_write_string(FILE* file, const char* value) {
    fputc('"', file);
    if (value) {
        for (const unsigned char* ptr = (const unsigned char*)value; *ptr; ++ptr) {
            unsigned char ch = *ptr;
            switch (ch) {
                case '\\':
                    fputs("\\\\", file);
                    break;
                case '"':
                    fputs("\\\"", file);
                    break;
                case '\b':
                    fputs("\\b", file);
                    break;
                case '\f':
                    fputs("\\f", file);
                    break;
                case '\n':
                    fputs("\\n", file);
                    break;
                case '\r':
                    fputs("\\r", file);
                    break;
                case '\t':
                    fputs("\\t", file);
                    break;
                default:
                    if (ch < 0x20) {
                        fprintf(file, "\\u%04x", ch);
                    } else {
                        fputc(ch, file);
                    }
                    break;
            }
        }
    }
    fputc('"', file);
}

static void json_write_float(FILE* file, float value) {
    float corrected = fabsf(value) < 1e-6f ? 0.0f : value;
    fprintf(file, "%.6f", corrected);
}

bool session_document_write_file(const SessionDocument* doc, const char* path) {
    char error[256] = {0};
    if (!session_document_validate(doc, error, sizeof(error))) {
        SDL_Log("session_document_write_file: invalid document: %s", error[0] ? error : "unknown error");
        return false;
    }
    if (!path || path[0] == '\0') {
        SDL_Log("session_document_write_file: path is empty");
        return false;
    }

    FILE* file = fopen(path, "wb");
    if (!file) {
        SDL_Log("session_document_write_file: failed to open %s: %s", path, strerror(errno));
        return false;
    }

    fprintf(file, "{\n");

    json_write_indent(file, 1);
    fprintf(file, "\"version\": %u,\n", doc->version);

    json_write_indent(file, 1);
    fprintf(file, "\"engine\": {\n");
    json_write_indent(file, 2);
    fprintf(file, "\"sample_rate\": %d,\n", doc->engine.sample_rate);
    json_write_indent(file, 2);
    fprintf(file, "\"block_size\": %d,\n", doc->engine.block_size);
    json_write_indent(file, 2);
    fprintf(file, "\"default_fade_in_ms\": ");
    json_write_float(file, doc->engine.default_fade_in_ms);
    fprintf(file, ",\n");
    json_write_indent(file, 2);
    fprintf(file, "\"default_fade_out_ms\": ");
    json_write_float(file, doc->engine.default_fade_out_ms);
    fprintf(file, ",\n");
    json_write_indent(file, 2);
    fprintf(file, "\"fade_presets_ms\": [");
    int preset_count = doc->engine.fade_preset_count;
    if (preset_count < 0) {
        preset_count = 0;
    }
    if (preset_count > CONFIG_FADE_PRESET_MAX) {
        preset_count = CONFIG_FADE_PRESET_MAX;
    }
    for (int i = 0; i < preset_count; ++i) {
        if (i > 0) {
            fprintf(file, ", ");
        }
        json_write_float(file, doc->engine.fade_preset_ms[i]);
    }
    fprintf(file, "],\n");
    json_write_indent(file, 2);
    fprintf(file, "\"enable_engine_logs\": %s,\n", doc->engine.enable_engine_logs ? "true" : "false");
    json_write_indent(file, 2);
    fprintf(file, "\"enable_cache_logs\": %s,\n", doc->engine.enable_cache_logs ? "true" : "false");
    json_write_indent(file, 2);
    fprintf(file, "\"enable_timing_logs\": %s\n", doc->engine.enable_timing_logs ? "true" : "false");
    json_write_indent(file, 1);
    fprintf(file, "},\n");

    json_write_indent(file, 1);
    fprintf(file, "\"transport_playing\": %s,\n", doc->transport_playing ? "true" : "false");
    json_write_indent(file, 1);
    fprintf(file, "\"transport_frame\": %" PRIu64 ",\n", doc->transport_frame);

    json_write_indent(file, 1);
    fprintf(file, "\"loop\": {\n");
    json_write_indent(file, 2);
    fprintf(file, "\"enabled\": %s,\n", doc->loop.enabled ? "true" : "false");
    json_write_indent(file, 2);
    fprintf(file, "\"start_frame\": %" PRIu64 ",\n", doc->loop.start_frame);
    json_write_indent(file, 2);
    fprintf(file, "\"end_frame\": %" PRIu64 "\n", doc->loop.end_frame);
    json_write_indent(file, 1);
    fprintf(file, "},\n");

    json_write_indent(file, 1);
    fprintf(file, "\"timeline\": {\n");
    json_write_indent(file, 2);
    fprintf(file, "\"visible_seconds\": ");
    json_write_float(file, doc->timeline.visible_seconds);
    fprintf(file, ",\n");
    json_write_indent(file, 2);
    fprintf(file, "\"vertical_scale\": ");
    json_write_float(file, doc->timeline.vertical_scale);
    fprintf(file, ",\n");
    json_write_indent(file, 2);
    fprintf(file, "\"show_all_grid_lines\": %s,\n", doc->timeline.show_all_grid_lines ? "true" : "false");
    json_write_indent(file, 2);
    fprintf(file, "\"playhead_frame\": %" PRIu64 "\n", doc->timeline.playhead_frame);
    json_write_indent(file, 1);
    fprintf(file, "},\n");

    json_write_indent(file, 1);
    fprintf(file, "\"layout\": {\n");
    json_write_indent(file, 2);
    fprintf(file, "\"transport_ratio\": ");
    json_write_float(file, doc->layout.transport_ratio);
    fprintf(file, ",\n");
    json_write_indent(file, 2);
    fprintf(file, "\"library_ratio\": ");
    json_write_float(file, doc->layout.library_ratio);
    fprintf(file, ",\n");
    json_write_indent(file, 2);
    fprintf(file, "\"mixer_ratio\": ");
    json_write_float(file, doc->layout.mixer_ratio);
    fprintf(file, "\n");
    json_write_indent(file, 1);
    fprintf(file, "},\n");

    json_write_indent(file, 1);
    fprintf(file, "\"library\": {\n");
    json_write_indent(file, 2);
    fprintf(file, "\"directory\": ");
    json_write_string(file, doc->library.directory);
    fprintf(file, ",\n");
    json_write_indent(file, 2);
    fprintf(file, "\"selected_index\": %d\n", doc->library.selected_index);
    json_write_indent(file, 1);
    fprintf(file, "},\n");

    json_write_indent(file, 1);
    fprintf(file, "\"master_fx\": [\n");
    for (int i = 0; i < doc->master_fx_count; ++i) {
        const SessionFxInstance* fx = &doc->master_fx[i];
        json_write_indent(file, 2);
        fprintf(file, "{\n");
        json_write_indent(file, 3);
        fprintf(file, "\"type\": %u,\n", fx->type);
        if (fx->name[0] != '\0') {
            json_write_indent(file, 3);
            fprintf(file, "\"name\": ");
            json_write_string(file, fx->name);
            fprintf(file, ",\n");
        }
        json_write_indent(file, 3);
        fprintf(file, "\"enabled\": %s,\n", fx->enabled ? "true" : "false");
        json_write_indent(file, 3);
        fprintf(file, "\"params\": [");
        for (uint32_t p = 0; p < fx->param_count; ++p) {
            if (p > 0) {
                fprintf(file, ", ");
            }
            json_write_float(file, fx->params[p]);
        }
        fprintf(file, "],\n");
        json_write_indent(file, 3);
        fprintf(file, "\"param_count\": %u\n", fx->param_count);
        json_write_indent(file, 2);
        fprintf(file, "}");
        if (i + 1 < doc->master_fx_count) {
            fprintf(file, ",");
        }
        fprintf(file, "\n");
    }
    json_write_indent(file, 1);
    fprintf(file, "],\n");

    json_write_indent(file, 1);
    fprintf(file, "\"tracks\": [\n");
    for (int t = 0; t < doc->track_count; ++t) {
        const SessionTrack* track = &doc->tracks[t];
        json_write_indent(file, 2);
        fprintf(file, "{\n");
        json_write_indent(file, 3);
        fprintf(file, "\"name\": ");
        json_write_string(file, track->name);
        fprintf(file, ",\n");
        json_write_indent(file, 3);
        fprintf(file, "\"gain\": ");
        json_write_float(file, track->gain);
        fprintf(file, ",\n");
        json_write_indent(file, 3);
        fprintf(file, "\"muted\": %s,\n", track->muted ? "true" : "false");
        json_write_indent(file, 3);
        fprintf(file, "\"solo\": %s,\n", track->solo ? "true" : "false");
        json_write_indent(file, 3);
        fprintf(file, "\"clips\": [\n");
        for (int c = 0; c < track->clip_count; ++c) {
            const SessionClip* clip = &track->clips[c];
            json_write_indent(file, 4);
            fprintf(file, "{\n");
            json_write_indent(file, 5);
            fprintf(file, "\"name\": ");
            json_write_string(file, clip->name);
            fprintf(file, ",\n");
            json_write_indent(file, 5);
            fprintf(file, "\"media_path\": ");
            json_write_string(file, clip->media_path);
            fprintf(file, ",\n");
            json_write_indent(file, 5);
            fprintf(file, "\"start_frame\": %" PRIu64 ",\n", clip->start_frame);
            json_write_indent(file, 5);
            fprintf(file, "\"duration_frames\": %" PRIu64 ",\n", clip->duration_frames);
            json_write_indent(file, 5);
            fprintf(file, "\"offset_frames\": %" PRIu64 ",\n", clip->offset_frames);
            json_write_indent(file, 5);
            fprintf(file, "\"fade_in_frames\": %" PRIu64 ",\n", clip->fade_in_frames);
            json_write_indent(file, 5);
            fprintf(file, "\"fade_out_frames\": %" PRIu64 ",\n", clip->fade_out_frames);
            json_write_indent(file, 5);
            fprintf(file, "\"gain\": ");
            json_write_float(file, clip->gain);
            fprintf(file, ",\n");
            json_write_indent(file, 5);
            fprintf(file, "\"selected\": %s\n", clip->selected ? "true" : "false");
            json_write_indent(file, 4);
            fprintf(file, "}");
            if (c + 1 < track->clip_count) {
                fprintf(file, ",");
            }
            fprintf(file, "\n");
        }
        json_write_indent(file, 3);
        fprintf(file, "]\n");
        json_write_indent(file, 2);
        fprintf(file, "}");
        if (t + 1 < doc->track_count) {
            fprintf(file, ",");
        }
        fprintf(file, "\n");
    }
    json_write_indent(file, 1);
    fprintf(file, "]\n");

    fprintf(file, "}\n");

    bool ok = fflush(file) == 0 && ferror(file) == 0;
    if (fclose(file) != 0) {
        SDL_Log("session_document_write_file: failed to close %s: %s", path, strerror(errno));
        ok = false;
    }
    if (!ok) {
        SDL_Log("session_document_write_file: error writing %s", path);
    }
    return ok;
}

bool session_save_to_file(const AppState* state, const char* path) {
    if (!state) {
        SDL_Log("session_save_to_file: app state is null");
        return false;
    }
    SessionDocument doc;
    session_document_init(&doc);
    bool captured = session_document_capture(state, &doc);
    if (!captured) {
        session_document_free(&doc);
        return false;
    }
    bool wrote = session_document_write_file(&doc, path);
    session_document_free(&doc);
    return wrote;
}

typedef struct {
    const char* data;
    size_t length;
    size_t pos;
} JsonReader;

static void json_reader_init(JsonReader* r, const char* data, size_t length) {
    r->data = data;
    r->length = length;
    r->pos = 0;
}

static void json_skip_whitespace(JsonReader* r) {
    while (r->pos < r->length) {
        char ch = r->data[r->pos];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            ++r->pos;
        } else {
            break;
        }
    }
}

static bool json_expect(JsonReader* r, char expected) {
    json_skip_whitespace(r);
    if (r->pos >= r->length || r->data[r->pos] != expected) {
        return false;
    }
    ++r->pos;
    return true;
}

static bool json_match_literal(JsonReader* r, const char* literal) {
    size_t len = strlen(literal);
    if (r->pos + len > r->length) {
        return false;
    }
    if (strncmp(&r->data[r->pos], literal, len) != 0) {
        return false;
    }
    r->pos += len;
    return true;
}

static bool json_parse_bool(JsonReader* r, bool* out_value) {
    json_skip_whitespace(r);
    if (json_match_literal(r, "true")) {
        if (out_value) *out_value = true;
        return true;
    }
    if (json_match_literal(r, "false")) {
        if (out_value) *out_value = false;
        return true;
    }
    return false;
}

static bool json_parse_null(JsonReader* r) {
    return json_match_literal(r, "null");
}

static bool json_parse_number(JsonReader* r, double* out_value) {
    json_skip_whitespace(r);
    size_t start = r->pos;
    if (start >= r->length) {
        return false;
    }
    if (r->data[r->pos] == '-' || r->data[r->pos] == '+') {
        ++r->pos;
    }
    bool has_digits = false;
    while (r->pos < r->length && isdigit((unsigned char)r->data[r->pos])) {
        has_digits = true;
        ++r->pos;
    }
    if (r->pos < r->length && r->data[r->pos] == '.') {
        ++r->pos;
        while (r->pos < r->length && isdigit((unsigned char)r->data[r->pos])) {
            has_digits = true;
            ++r->pos;
        }
    }
    if (r->pos < r->length && (r->data[r->pos] == 'e' || r->data[r->pos] == 'E')) {
        ++r->pos;
        if (r->pos < r->length && (r->data[r->pos] == '+' || r->data[r->pos] == '-')) {
            ++r->pos;
        }
        while (r->pos < r->length && isdigit((unsigned char)r->data[r->pos])) {
            has_digits = true;
            ++r->pos;
        }
    }
    if (!has_digits) {
        return false;
    }
    errno = 0;
    double value = strtod(&r->data[start], NULL);
    if (errno != 0) {
        return false;
    }
    if (out_value) {
        *out_value = value;
    }
    return true;
}

static int json_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool json_parse_string(JsonReader* r, char* dst, size_t dst_len) {
    if (!json_expect(r, '"')) {
        return false;
    }
    size_t out_pos = 0;
    while (r->pos < r->length) {
        char ch = r->data[r->pos++];
        if (ch == '"') {
            break;
        }
        if (ch == '\\') {
            if (r->pos >= r->length) {
                return false;
            }
            char esc = r->data[r->pos++];
            switch (esc) {
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u': {
                    if (r->pos + 4 > r->length) {
                        return false;
                    }
                    int code = 0;
                    for (int i = 0; i < 4; ++i) {
                        int v = json_hex_value(r->data[r->pos + i]);
                        if (v < 0) {
                            return false;
                        }
                        code = (code << 4) | v;
                    }
                    r->pos += 4;
                    if (code <= 0x7F) {
                        ch = (char)code;
                    } else {
                        ch = '?';
                    }
                } break;
                default:
                    return false;
            }
        }
        if (dst && out_pos + 1 < dst_len) {
            dst[out_pos++] = ch;
        }
    }
    if (dst && dst_len > 0) {
        dst[out_pos] = '\0';
    }
    return true;
}

static bool json_skip_string(JsonReader* r) {
    return json_parse_string(r, NULL, 0);
}

static bool json_skip_value(JsonReader* r);

static bool json_skip_array(JsonReader* r) {
    if (!json_expect(r, '[')) {
        return false;
    }
    json_skip_whitespace(r);
    if (r->pos < r->length && r->data[r->pos] == ']') {
        ++r->pos;
        return true;
    }
    while (r->pos < r->length) {
        if (!json_skip_value(r)) {
            return false;
        }
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ',') {
            ++r->pos;
            continue;
        }
        if (r->pos < r->length && r->data[r->pos] == ']') {
            ++r->pos;
            return true;
        }
        return false;
    }
    return false;
}

static bool json_skip_object(JsonReader* r) {
    if (!json_expect(r, '{')) {
        return false;
    }
    json_skip_whitespace(r);
    if (r->pos < r->length && r->data[r->pos] == '}') {
        ++r->pos;
        return true;
    }
    while (r->pos < r->length) {
        if (!json_skip_string(r)) {
            return false;
        }
        if (!json_expect(r, ':')) {
            return false;
        }
        if (!json_skip_value(r)) {
            return false;
        }
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ',') {
            ++r->pos;
            continue;
        }
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            return true;
        }
        return false;
    }
    return false;
}

static bool json_skip_value(JsonReader* r) {
    json_skip_whitespace(r);
    if (r->pos >= r->length) {
        return false;
    }
    char ch = r->data[r->pos];
    switch (ch) {
        case '{':
            return json_skip_object(r);
        case '[':
            return json_skip_array(r);
        case '"':
            return json_skip_string(r);
        case 't':
        case 'f':
            return json_parse_bool(r, NULL);
        case 'n':
            return json_parse_null(r);
        default:
            return json_parse_number(r, NULL);
    }
}

static SessionTrack* session_document_append_track(SessionDocument* doc) {
    int new_count = doc->track_count + 1;
    SessionTrack* resized = (SessionTrack*)realloc(doc->tracks, (size_t)new_count * sizeof(SessionTrack));
    if (!resized) {
        return NULL;
    }
    doc->tracks = resized;
    SessionTrack* track = &doc->tracks[new_count - 1];
    memset(track, 0, sizeof(*track));
    doc->track_count = new_count;
    return track;
}

static SessionClip* session_track_append_clip(SessionTrack* track) {
    int new_count = track->clip_count + 1;
    SessionClip* resized = (SessionClip*)realloc(track->clips, (size_t)new_count * sizeof(SessionClip));
    if (!resized) {
        return NULL;
    }
    track->clips = resized;
    SessionClip* clip = &track->clips[new_count - 1];
    memset(clip, 0, sizeof(*clip));
    track->clip_count = new_count;
    return clip;
}

static bool parse_session_tracks(JsonReader* r, SessionDocument* doc);
static SessionFxInstance* session_document_append_master_fx(SessionDocument* doc) {
    int new_count = doc->master_fx_count + 1;
    SessionFxInstance* resized = (SessionFxInstance*)realloc(doc->master_fx, (size_t)new_count * sizeof(SessionFxInstance));
    if (!resized) {
        return NULL;
    }
    doc->master_fx = resized;
    SessionFxInstance* fx = &doc->master_fx[new_count - 1];
    memset(fx, 0, sizeof(*fx));
    fx->enabled = true;
    doc->master_fx_count = new_count;
    return fx;
}

static bool parse_master_fx(JsonReader* r, SessionDocument* doc);

static bool parse_session_track(JsonReader* r, SessionTrack* track) {
    if (!json_expect(r, '{')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char key[64];
        if (!json_parse_string(r, key, sizeof(key))) {
            return false;
        }
        if (!json_expect(r, ':')) {
            return false;
        }
        if (strcmp(key, "name") == 0) {
            if (!json_parse_string(r, track->name, sizeof(track->name))) {
                return false;
            }
        } else if (strcmp(key, "gain") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->gain = (float)val;
        } else if (strcmp(key, "muted") == 0) {
            if (!json_parse_bool(r, &track->muted)) {
                return false;
            }
        } else if (strcmp(key, "solo") == 0) {
            if (!json_parse_bool(r, &track->solo)) {
                return false;
            }
        } else if (strcmp(key, "clips") == 0) {
            if (!json_expect(r, '[')) {
                return false;
            }
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == ']') {
                ++r->pos;
            } else {
                while (true) {
                    SessionClip* clip = session_track_append_clip(track);
                    if (!clip) {
                        return false;
                    }
                    if (!json_expect(r, '{')) {
                        return false;
                    }
                    while (true) {
                        json_skip_whitespace(r);
                        if (r->pos < r->length && r->data[r->pos] == '}') {
                            ++r->pos;
                            break;
                        }
                        char clip_key[64];
                        if (!json_parse_string(r, clip_key, sizeof(clip_key))) {
                            return false;
                        }
                        if (!json_expect(r, ':')) {
                            return false;
                        }
                        if (strcmp(clip_key, "name") == 0) {
                            if (!json_parse_string(r, clip->name, sizeof(clip->name))) {
                                return false;
                            }
                        } else if (strcmp(clip_key, "media_path") == 0) {
                            if (!json_parse_string(r, clip->media_path, sizeof(clip->media_path))) {
                                return false;
                            }
                        } else if (strcmp(clip_key, "start_frame") == 0) {
                            double val;
                            if (!json_parse_number(r, &val)) {
                                return false;
                            }
                            clip->start_frame = (uint64_t)(val < 0 ? 0 : val);
                        } else if (strcmp(clip_key, "duration_frames") == 0) {
                            double val;
                            if (!json_parse_number(r, &val)) {
                                return false;
                            }
                            clip->duration_frames = (uint64_t)(val < 0 ? 0 : val);
                        } else if (strcmp(clip_key, "offset_frames") == 0) {
                            double val;
                            if (!json_parse_number(r, &val)) {
                                return false;
                            }
                            clip->offset_frames = (uint64_t)(val < 0 ? 0 : val);
                        } else if (strcmp(clip_key, "fade_in_frames") == 0) {
                            double val;
                            if (!json_parse_number(r, &val)) {
                                return false;
                            }
                            clip->fade_in_frames = (uint64_t)(val < 0 ? 0 : val);
                        } else if (strcmp(clip_key, "fade_out_frames") == 0) {
                            double val;
                            if (!json_parse_number(r, &val)) {
                                return false;
                            }
                            clip->fade_out_frames = (uint64_t)(val < 0 ? 0 : val);
                        } else if (strcmp(clip_key, "gain") == 0) {
                            double val;
                            if (!json_parse_number(r, &val)) {
                                return false;
                            }
                            clip->gain = (float)val;
                        } else if (strcmp(clip_key, "selected") == 0) {
                            if (!json_parse_bool(r, &clip->selected)) {
                                return false;
                            }
                        } else {
                            if (!json_skip_value(r)) {
                                return false;
                            }
                        }
                        json_skip_whitespace(r);
                        if (r->pos < r->length && r->data[r->pos] == ',') {
                            ++r->pos;
                            continue;
                        }
                        if (r->pos < r->length && r->data[r->pos] == '}') {
                            continue;
                        }
                    }
                    json_skip_whitespace(r);
                    if (r->pos < r->length && r->data[r->pos] == ',') {
                        ++r->pos;
                        continue;
                    }
                    if (r->pos < r->length && r->data[r->pos] == ']') {
                        ++r->pos;
                        break;
                    }
                    return false;
                }
            }
        } else {
            if (!json_skip_value(r)) {
                return false;
            }
        }
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ',') {
            ++r->pos;
            continue;
        }
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        return false;
    }
    return true;
}

static bool parse_session_tracks(JsonReader* r, SessionDocument* doc) {
    if (!json_expect(r, '[')) {
        return false;
    }
    json_skip_whitespace(r);
    if (r->pos < r->length && r->data[r->pos] == ']') {
        ++r->pos;
        return true;
    }
    while (true) {
        SessionTrack* track = session_document_append_track(doc);
        if (!track) {
            return false;
        }
        if (!parse_session_track(r, track)) {
            return false;
        }
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ',') {
            ++r->pos;
            continue;
        }
        if (r->pos < r->length && r->data[r->pos] == ']') {
            ++r->pos;
            break;
        }
        return false;
    }
    return true;
}

static bool parse_master_fx(JsonReader* r, SessionDocument* doc) {
    if (!json_expect(r, '[')) {
        return false;
    }
    json_skip_whitespace(r);
    if (r->pos < r->length && r->data[r->pos] == ']') {
        ++r->pos;
        return true;
    }
    while (true) {
        SessionFxInstance* fx = session_document_append_master_fx(doc);
        if (!fx) {
            return false;
        }
        if (!json_expect(r, '{')) {
            return false;
        }
        uint32_t params_found = 0;
        bool params_set = false;
        while (true) {
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == '}') {
                ++r->pos;
                break;
            }
            char key[64];
            if (!json_parse_string(r, key, sizeof(key))) {
                return false;
            }
            if (!json_expect(r, ':')) {
                return false;
            }
            if (strcmp(key, "type") == 0) {
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                fx->type = (FxTypeId)(val < 0 ? 0 : val);
            } else if (strcmp(key, "name") == 0) {
                if (!json_parse_string(r, fx->name, sizeof(fx->name))) {
                    return false;
                }
            } else if (strcmp(key, "enabled") == 0) {
                if (!json_parse_bool(r, &fx->enabled)) {
                    return false;
                }
            } else if (strcmp(key, "params") == 0) {
                if (!json_expect(r, '[')) {
                    return false;
                }
                json_skip_whitespace(r);
                params_found = 0;
                if (r->pos < r->length && r->data[r->pos] == ']') {
                    ++r->pos;
                } else {
                    while (true) {
                        double val;
                        if (!json_parse_number(r, &val)) {
                            return false;
                        }
                        if (params_found < FX_MAX_PARAMS) {
                            fx->params[params_found] = (float)val;
                        }
                        ++params_found;
                        json_skip_whitespace(r);
                        if (r->pos < r->length && r->data[r->pos] == ',') {
                            ++r->pos;
                            continue;
                        }
                        if (r->pos < r->length && r->data[r->pos] == ']') {
                            ++r->pos;
                            break;
                        }
                        return false;
                    }
                }
                fx->param_count = params_found > FX_MAX_PARAMS ? FX_MAX_PARAMS : params_found;
                params_set = true;
            } else if (strcmp(key, "param_count") == 0) {
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                uint32_t count = (val < 0) ? 0u : (uint32_t)val;
                if (count > FX_MAX_PARAMS) {
                    count = FX_MAX_PARAMS;
                }
                fx->param_count = count;
                params_set = true;
            } else {
                if (!json_skip_value(r)) {
                    return false;
                }
            }
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == ',') {
                ++r->pos;
                continue;
            }
            if (r->pos < r->length && r->data[r->pos] == '}') {
                ++r->pos;
                break;
            }
            return false;
        }
        if (!params_set) {
            fx->param_count = params_found > FX_MAX_PARAMS ? FX_MAX_PARAMS : params_found;
        }
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ',') {
            ++r->pos;
            continue;
        }
        if (r->pos < r->length && r->data[r->pos] == ']') {
            ++r->pos;
            break;
        }
        return false;
    }
    return true;
}

static bool parse_session_document(JsonReader* r, SessionDocument* doc) {
    if (!json_expect(r, '{')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char key[64];
        if (!json_parse_string(r, key, sizeof(key))) {
            return false;
        }
        if (!json_expect(r, ':')) {
            return false;
        }
        if (strcmp(key, "version") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->version = (uint32_t)(val < 0 ? 0 : val);
        } else if (strcmp(key, "engine") == 0) {
            if (!json_expect(r, '{')) {
                return false;
            }
            while (true) {
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                char eng_key[64];
                if (!json_parse_string(r, eng_key, sizeof(eng_key))) {
                    return false;
                }
                if (!json_expect(r, ':')) {
                    return false;
                }
                if (strcmp(eng_key, "sample_rate") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->engine.sample_rate = (int)val;
                } else if (strcmp(eng_key, "block_size") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->engine.block_size = (int)val;
                } else if (strcmp(eng_key, "default_fade_in_ms") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->engine.default_fade_in_ms = (float)(val < 0.0 ? 0.0 : val);
                } else if (strcmp(eng_key, "default_fade_out_ms") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->engine.default_fade_out_ms = (float)(val < 0.0 ? 0.0 : val);
                } else if (strcmp(eng_key, "fade_presets_ms") == 0) {
                    if (!json_expect(r, '[')) {
                        return false;
                    }
                    int stored = 0;
                    json_skip_whitespace(r);
                    if (r->pos < r->length && r->data[r->pos] == ']') {
                        ++r->pos;
                    } else {
                        while (true) {
                            double val;
                            if (!json_parse_number(r, &val)) {
                                return false;
                            }
                            if (stored < CONFIG_FADE_PRESET_MAX) {
                                float clamped = (float)(val < 0.0 ? 0.0 : val);
                                doc->engine.fade_preset_ms[stored] = clamped;
                                stored++;
                            }
                            json_skip_whitespace(r);
                            if (r->pos < r->length && r->data[r->pos] == ',') {
                                ++r->pos;
                                continue;
                            }
                            if (r->pos < r->length && r->data[r->pos] == ']') {
                                ++r->pos;
                                break;
                            }
                            return false;
                        }
                    }
                    for (int i = stored; i < CONFIG_FADE_PRESET_MAX; ++i) {
                        doc->engine.fade_preset_ms[i] = 0.0f;
                    }
                    doc->engine.fade_preset_count = stored;
                } else if (strcmp(eng_key, "enable_engine_logs") == 0) {
                    if (!json_parse_bool(r, &doc->engine.enable_engine_logs)) {
                        return false;
                    }
                } else if (strcmp(eng_key, "enable_cache_logs") == 0) {
                    if (!json_parse_bool(r, &doc->engine.enable_cache_logs)) {
                        return false;
                    }
                } else if (strcmp(eng_key, "enable_timing_logs") == 0) {
                    if (!json_parse_bool(r, &doc->engine.enable_timing_logs)) {
                        return false;
                    }
                } else {
                    if (!json_skip_value(r)) {
                        return false;
                    }
                }
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == ',') {
                    ++r->pos;
                    continue;
                }
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                return false;
            }
        } else if (strcmp(key, "transport_playing") == 0) {
            if (!json_parse_bool(r, &doc->transport_playing)) {
                return false;
            }
        } else if (strcmp(key, "transport_frame") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->transport_frame = (uint64_t)(val < 0 ? 0 : val);
        } else if (strcmp(key, "loop") == 0) {
            if (!json_expect(r, '{')) {
                return false;
            }
            while (true) {
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                char loop_key[64];
                if (!json_parse_string(r, loop_key, sizeof(loop_key))) {
                    return false;
                }
                if (!json_expect(r, ':')) {
                    return false;
                }
                if (strcmp(loop_key, "enabled") == 0) {
                    if (!json_parse_bool(r, &doc->loop.enabled)) {
                        return false;
                    }
                } else if (strcmp(loop_key, "start_frame") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->loop.start_frame = (uint64_t)(val < 0 ? 0 : val);
                } else if (strcmp(loop_key, "end_frame") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->loop.end_frame = (uint64_t)(val < 0 ? 0 : val);
                } else {
                    if (!json_skip_value(r)) {
                        return false;
                    }
                }
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == ',') {
                    ++r->pos;
                    continue;
                }
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                return false;
            }
        } else if (strcmp(key, "timeline") == 0) {
            if (!json_expect(r, '{')) {
                return false;
            }
            while (true) {
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                char timeline_key[64];
                if (!json_parse_string(r, timeline_key, sizeof(timeline_key))) {
                    return false;
                }
                if (!json_expect(r, ':')) {
                    return false;
                }
                double val;
                if (strcmp(timeline_key, "visible_seconds") == 0) {
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->timeline.visible_seconds = (float)val;
                } else if (strcmp(timeline_key, "vertical_scale") == 0) {
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->timeline.vertical_scale = (float)val;
                } else if (strcmp(timeline_key, "show_all_grid_lines") == 0) {
                    if (!json_parse_bool(r, &doc->timeline.show_all_grid_lines)) {
                        return false;
                    }
                } else if (strcmp(timeline_key, "playhead_frame") == 0) {
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->timeline.playhead_frame = (uint64_t)(val < 0 ? 0 : val);
                } else {
                    if (!json_skip_value(r)) {
                        return false;
                    }
                }
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == ',') {
                    ++r->pos;
                    continue;
                }
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                return false;
            }
        } else if (strcmp(key, "layout") == 0) {
            if (!json_expect(r, '{')) {
                return false;
            }
            while (true) {
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                char layout_key[64];
                if (!json_parse_string(r, layout_key, sizeof(layout_key))) {
                    return false;
                }
                if (!json_expect(r, ':')) {
                    return false;
                }
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                if (strcmp(layout_key, "transport_ratio") == 0) {
                    doc->layout.transport_ratio = (float)val;
                } else if (strcmp(layout_key, "library_ratio") == 0) {
                    doc->layout.library_ratio = (float)val;
                } else if (strcmp(layout_key, "mixer_ratio") == 0) {
                    doc->layout.mixer_ratio = (float)val;
                }
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == ',') {
                    ++r->pos;
                    continue;
                }
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                return false;
            }
        } else if (strcmp(key, "library") == 0) {
            if (!json_expect(r, '{')) {
                return false;
            }
            while (true) {
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                char lib_key[64];
                if (!json_parse_string(r, lib_key, sizeof(lib_key))) {
                    return false;
                }
                if (!json_expect(r, ':')) {
                    return false;
                }
                if (strcmp(lib_key, "directory") == 0) {
                    if (!json_parse_string(r, doc->library.directory, sizeof(doc->library.directory))) {
                        return false;
                    }
                } else if (strcmp(lib_key, "selected_index") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->library.selected_index = (int)val;
                } else {
                    if (!json_skip_value(r)) {
                        return false;
                    }
                }
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == ',') {
                    ++r->pos;
                    continue;
                }
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                return false;
            }
        } else if (strcmp(key, "master_fx") == 0) {
            if (!parse_master_fx(r, doc)) {
                return false;
            }
        } else if (strcmp(key, "tracks") == 0) {
            if (!parse_session_tracks(r, doc)) {
                return false;
            }
        } else {
            if (!json_skip_value(r)) {
                return false;
            }
        }
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ',') {
            ++r->pos;
            continue;
        }
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        return false;
    }
    return true;
}

bool session_document_read_file(const char* path, SessionDocument* out_doc) {
    if (!out_doc) {
        return false;
    }
    session_document_reset(out_doc);
    if (!path || path[0] == '\0') {
        SDL_Log("session_document_read_file: path is empty");
        return false;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        SDL_Log("session_document_read_file: failed to open %s: %s", path, strerror(errno));
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        SDL_Log("session_document_read_file: failed to seek %s", path);
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size < 0) {
        SDL_Log("session_document_read_file: failed to ftell %s", path);
        fclose(file);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        SDL_Log("session_document_read_file: failed to rewind %s", path);
        fclose(file);
        return false;
    }
    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        SDL_Log("session_document_read_file: out of memory (%ld bytes)", size);
        fclose(file);
        return false;
    }
    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        SDL_Log("session_document_read_file: read mismatch for %s", path);
        free(buffer);
        return false;
    }
    buffer[size] = '\0';

    JsonReader reader;
    json_reader_init(&reader, buffer, (size_t)size);
    bool ok = parse_session_document(&reader, out_doc);
    free(buffer);
    if (!ok) {
        SDL_Log("session_document_read_file: failed to parse %s", path);
        session_document_reset(out_doc);
        return false;
    }

    char error[256] = {0};
    if (!session_document_validate(out_doc, error, sizeof(error))) {
        SDL_Log("session_document_read_file: validation failed for %s: %s", path, error[0] ? error : "unknown error");
        session_document_reset(out_doc);
        return false;
    }
    return true;
}

static float clamp_ratio(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
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
