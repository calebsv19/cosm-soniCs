#include "session.h"
#include "app_state.h"

#include <SDL2/SDL.h>

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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
    fprintf(file, "\"tempo\": {\n");
    json_write_indent(file, 2);
    fprintf(file, "\"bpm\": ");
    json_write_float(file, doc->tempo.bpm);
    fprintf(file, ",\n");
    json_write_indent(file, 2);
    fprintf(file, "\"ts_num\": %d,\n", doc->tempo.ts_num);
    json_write_indent(file, 2);
    fprintf(file, "\"ts_den\": %d\n", doc->tempo.ts_den);
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
    fprintf(file, "\"window_start_seconds\": ");
    json_write_float(file, doc->timeline.window_start_seconds);
    fprintf(file, ",\n");
    json_write_indent(file, 2);
    fprintf(file, "\"vertical_scale\": ");
    json_write_float(file, doc->timeline.vertical_scale);
    fprintf(file, ",\n");
    json_write_indent(file, 2);
    fprintf(file, "\"show_all_grid_lines\": %s,\n", doc->timeline.show_all_grid_lines ? "true" : "false");
    json_write_indent(file, 2);
    fprintf(file, "\"view_in_beats\": %s,\n", doc->timeline.view_in_beats ? "true" : "false");
    json_write_indent(file, 2);
    fprintf(file, "\"follow_mode\": %d,\n", doc->timeline.follow_mode);
    json_write_indent(file, 2);
    fprintf(file, "\"playhead_frame\": %" PRIu64 "\n", doc->timeline.playhead_frame);
    json_write_indent(file, 1);
    fprintf(file, "},\n");

    json_write_indent(file, 1);
    fprintf(file, "\"effects_panel\": {\n");
    json_write_indent(file, 2);
    fprintf(file, "\"view_mode\": %d,\n", doc->effects_panel.view_mode);
    json_write_indent(file, 2);
    fprintf(file, "\"selected_index\": %d,\n", doc->effects_panel.selected_index);
    json_write_indent(file, 2);
    fprintf(file, "\"open_index\": %d\n", doc->effects_panel.open_index);
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
        fprintf(file, "\"param_modes\": [");
        for (uint32_t p = 0; p < fx->param_count; ++p) {
            if (p > 0) {
                fprintf(file, ", ");
            }
            fprintf(file, "%d", (int)fx->param_mode[p]);
        }
        fprintf(file, "],\n");
        json_write_indent(file, 3);
        fprintf(file, "\"param_beats\": [");
        for (uint32_t p = 0; p < fx->param_count; ++p) {
            if (p > 0) {
                fprintf(file, ", ");
            }
            json_write_float(file, fx->param_beats[p]);
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
        fprintf(file, "],\n");
        json_write_indent(file, 3);
        fprintf(file, "\"fx\": [\n");
        for (int f = 0; f < track->fx_count; ++f) {
            const SessionFxInstance* fx = &track->fx[f];
            json_write_indent(file, 4);
            fprintf(file, "{\n");
            json_write_indent(file, 5);
            fprintf(file, "\"type\": %u,\n", fx->type);
            if (fx->name[0] != '\0') {
                json_write_indent(file, 5);
                fprintf(file, "\"name\": ");
                json_write_string(file, fx->name);
                fprintf(file, ",\n");
            }
            json_write_indent(file, 5);
            fprintf(file, "\"enabled\": %s,\n", fx->enabled ? "true" : "false");
            json_write_indent(file, 5);
            fprintf(file, "\"params\": [");
            for (uint32_t p = 0; p < fx->param_count; ++p) {
                if (p > 0) {
                    fprintf(file, ", ");
                }
                json_write_float(file, fx->params[p]);
            }
            fprintf(file, "],\n");
            json_write_indent(file, 5);
            fprintf(file, "\"param_modes\": [");
            for (uint32_t p = 0; p < fx->param_count; ++p) {
                if (p > 0) {
                    fprintf(file, ", ");
                }
                fprintf(file, "%d", (int)fx->param_mode[p]);
            }
            fprintf(file, "],\n");
            json_write_indent(file, 5);
            fprintf(file, "\"param_beats\": [");
            for (uint32_t p = 0; p < fx->param_count; ++p) {
                if (p > 0) {
                    fprintf(file, ", ");
                }
                json_write_float(file, fx->param_beats[p]);
            }
            fprintf(file, "],\n");
            json_write_indent(file, 5);
            fprintf(file, "\"param_count\": %u\n", fx->param_count);
            json_write_indent(file, 4);
            fprintf(file, "}");
            if (f + 1 < track->fx_count) {
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
