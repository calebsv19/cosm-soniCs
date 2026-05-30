#include "session_io_read_internal.h"

#include <stdlib.h>
#include <string.h>

static bool session_document_append_tempo_event(SessionDocument* doc, float beat, float bpm) {
    if (!doc) {
        return false;
    }
    int new_count = doc->tempo_event_count + 1;
    SessionTempoEvent* resized = (SessionTempoEvent*)realloc(doc->tempo_events,
                                                             (size_t)new_count * sizeof(SessionTempoEvent));
    if (!resized) {
        return false;
    }
    doc->tempo_events = resized;
    doc->tempo_events[new_count - 1].beat = beat;
    doc->tempo_events[new_count - 1].bpm = bpm;
    doc->tempo_event_count = new_count;
    return true;
}

static bool session_document_append_time_signature_event(SessionDocument* doc, float beat, int ts_num, int ts_den) {
    if (!doc) {
        return false;
    }
    int new_count = doc->time_signature_event_count + 1;
    SessionTimeSignatureEvent* resized =
        (SessionTimeSignatureEvent*)realloc(doc->time_signature_events,
                                            (size_t)new_count * sizeof(SessionTimeSignatureEvent));
    if (!resized) {
        return false;
    }
    doc->time_signature_events = resized;
    doc->time_signature_events[new_count - 1].beat = beat;
    doc->time_signature_events[new_count - 1].ts_num = ts_num;
    doc->time_signature_events[new_count - 1].ts_den = ts_den;
    doc->time_signature_event_count = new_count;
    return true;
}

bool parse_session_document_tempo(JsonReader* r, SessionDocument* doc) {
    if (!r || !doc || !json_expect(r, '{')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char tempo_key[64];
        if (!json_parse_string(r, tempo_key, sizeof(tempo_key)) || !json_expect(r, ':')) {
            return false;
        }
        double val;
        if (strcmp(tempo_key, "bpm") == 0) {
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->tempo.bpm = (float)val;
        } else if (strcmp(tempo_key, "ts_num") == 0) {
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->tempo.ts_num = (int)val;
        } else if (strcmp(tempo_key, "ts_den") == 0) {
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->tempo.ts_den = (int)val;
        } else if (!json_skip_value(r)) {
            return false;
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

bool parse_session_document_tempo_map(JsonReader* r, SessionDocument* doc) {
    if (!r || !doc || !json_expect(r, '[')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ']') {
            ++r->pos;
            break;
        }
        if (!json_expect(r, '{')) {
            return false;
        }
        float beat = 0.0f;
        float bpm = 0.0f;
        while (true) {
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == '}') {
                ++r->pos;
                break;
            }
            char tempo_key[64];
            if (!json_parse_string(r, tempo_key, sizeof(tempo_key)) || !json_expect(r, ':')) {
                return false;
            }
            double val;
            if (strcmp(tempo_key, "beat") == 0) {
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                beat = (float)val;
            } else if (strcmp(tempo_key, "bpm") == 0) {
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                bpm = (float)val;
            } else if (!json_skip_value(r)) {
                return false;
            }
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == ',') {
                ++r->pos;
                continue;
            }
            if (r->pos < r->length && r->data[r->pos] == '}') {
                continue;
            }
            return false;
        }
        if (!session_document_append_tempo_event(doc, beat, bpm)) {
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

bool parse_session_document_time_signature_map(JsonReader* r, SessionDocument* doc) {
    if (!r || !doc || !json_expect(r, '[')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ']') {
            ++r->pos;
            break;
        }
        if (!json_expect(r, '{')) {
            return false;
        }
        float beat = 0.0f;
        int ts_num = 0;
        int ts_den = 0;
        while (true) {
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == '}') {
                ++r->pos;
                break;
            }
            char ts_key[64];
            if (!json_parse_string(r, ts_key, sizeof(ts_key)) || !json_expect(r, ':')) {
                return false;
            }
            double val;
            if (strcmp(ts_key, "beat") == 0) {
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                beat = (float)val;
            } else if (strcmp(ts_key, "ts_num") == 0) {
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                ts_num = (int)val;
            } else if (strcmp(ts_key, "ts_den") == 0) {
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                ts_den = (int)val;
            } else if (!json_skip_value(r)) {
                return false;
            }
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == ',') {
                ++r->pos;
                continue;
            }
            if (r->pos < r->length && r->data[r->pos] == '}') {
                continue;
            }
            return false;
        }
        if (!session_document_append_time_signature_event(doc, beat, ts_num, ts_den)) {
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

bool parse_session_document_loop(JsonReader* r, SessionDocument* doc) {
    if (!r || !doc || !json_expect(r, '{')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char loop_key[64];
        if (!json_parse_string(r, loop_key, sizeof(loop_key)) || !json_expect(r, ':')) {
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
        } else if (!json_skip_value(r)) {
            return false;
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

bool parse_session_document_timeline(JsonReader* r, SessionDocument* doc) {
    if (!r || !doc || !json_expect(r, '{')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char timeline_key[64];
        if (!json_parse_string(r, timeline_key, sizeof(timeline_key)) || !json_expect(r, ':')) {
            return false;
        }
        double val;
        if (strcmp(timeline_key, "visible_seconds") == 0) {
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->timeline.visible_seconds = (float)val;
        } else if (strcmp(timeline_key, "window_start_seconds") == 0) {
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->timeline.window_start_seconds = (float)val;
        } else if (strcmp(timeline_key, "vertical_scale") == 0) {
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->timeline.vertical_scale = (float)val;
        } else if (strcmp(timeline_key, "show_all_grid_lines") == 0) {
            if (!json_parse_bool(r, &doc->timeline.show_all_grid_lines)) {
                return false;
            }
        } else if (strcmp(timeline_key, "view_in_beats") == 0) {
            if (!json_parse_bool(r, &doc->timeline.view_in_beats)) {
                return false;
            }
        } else if (strcmp(timeline_key, "follow_mode") == 0) {
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->timeline.follow_mode = (int)val;
        } else if (strcmp(timeline_key, "playhead_frame") == 0) {
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->timeline.playhead_frame = (uint64_t)(val < 0 ? 0 : val);
        } else if (!json_skip_value(r)) {
            return false;
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

bool parse_session_midi_editor(JsonReader* r, SessionDocument* doc) {
    if (!r || !doc || !json_expect(r, '{')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char panel_key[64];
        if (!json_parse_string(r, panel_key, sizeof(panel_key)) || !json_expect(r, ':')) {
            return false;
        }
        if (strcmp(panel_key, "panel_mode") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->midi_editor.panel_mode = (int)val;
        } else if (strcmp(panel_key, "instrument_active_group") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->midi_editor.instrument_active_group = (int)val;
        } else if (!json_skip_value(r)) {
            return false;
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

bool parse_session_document_clip_inspector(JsonReader* r, SessionDocument* doc) {
    if (!r || !doc || !json_expect(r, '{')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char panel_key[64];
        if (!json_parse_string(r, panel_key, sizeof(panel_key)) || !json_expect(r, ':')) {
            return false;
        }
        if (strcmp(panel_key, "visible") == 0) {
            if (!json_parse_bool(r, &doc->clip_inspector.visible)) {
                return false;
            }
        } else if (strcmp(panel_key, "track_index") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->clip_inspector.track_index = (int)val;
        } else if (strcmp(panel_key, "clip_index") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->clip_inspector.clip_index = (int)val;
        } else if (strcmp(panel_key, "view_source") == 0) {
            if (!json_parse_bool(r, &doc->clip_inspector.view_source)) {
                return false;
            }
        } else if (strcmp(panel_key, "zoom") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->clip_inspector.zoom = (float)val;
        } else if (strcmp(panel_key, "scroll") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->clip_inspector.scroll = (float)val;
        } else if (!json_skip_value(r)) {
            return false;
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

bool parse_session_document_layout(JsonReader* r, SessionDocument* doc) {
    if (!r || !doc || !json_expect(r, '{')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char layout_key[64];
        if (!json_parse_string(r, layout_key, sizeof(layout_key)) || !json_expect(r, ':')) {
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
    return true;
}

bool parse_session_document_library(JsonReader* r, SessionDocument* doc) {
    if (!r || !doc || !json_expect(r, '{')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char lib_key[64];
        if (!json_parse_string(r, lib_key, sizeof(lib_key)) || !json_expect(r, ':')) {
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
        } else if (!json_skip_value(r)) {
            return false;
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

bool parse_session_document_data_paths(JsonReader* r, SessionDocument* doc) {
    if (!r || !doc || !json_expect(r, '{')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char paths_key[64];
        if (!json_parse_string(r, paths_key, sizeof(paths_key)) || !json_expect(r, ':')) {
            return false;
        }
        if (strcmp(paths_key, "input_root") == 0) {
            if (!json_parse_string(r, doc->data_paths.input_root, sizeof(doc->data_paths.input_root))) {
                return false;
            }
        } else if (strcmp(paths_key, "output_root") == 0) {
            if (!json_parse_string(r, doc->data_paths.output_root, sizeof(doc->data_paths.output_root))) {
                return false;
            }
        } else if (strcmp(paths_key, "library_copy_root") == 0) {
            if (!json_parse_string(r,
                                   doc->data_paths.library_copy_root,
                                   sizeof(doc->data_paths.library_copy_root))) {
                return false;
            }
        } else if (!json_skip_value(r)) {
            return false;
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
