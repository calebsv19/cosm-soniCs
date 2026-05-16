#include "session_io_read_internal.h"

#include <stdlib.h>
#include <string.h>

static SessionClip* session_track_append_clip(SessionTrack* track) {
    int new_count = track->clip_count + 1;
    SessionClip* resized = (SessionClip*)realloc(track->clips, (size_t)new_count * sizeof(SessionClip));
    if (!resized) {
        return NULL;
    }
    track->clips = resized;
    SessionClip* clip = &track->clips[new_count - 1];
    memset(clip, 0, sizeof(*clip));
    clip->instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    clip->instrument_params = engine_instrument_default_params(clip->instrument_preset);
    clip->instrument_inherits_track = false;
    track->clip_count = new_count;
    return clip;
}

static bool parse_session_clip_kind(JsonReader* r, SessionClip* clip) {
    if (!r || !clip) {
        return false;
    }
    json_skip_whitespace(r);
    if (r->pos < r->length && r->data[r->pos] == '"') {
        char kind[16];
        if (!json_parse_string(r, kind, sizeof(kind))) {
            return false;
        }
        if (strcmp(kind, "midi") == 0) {
            clip->kind = ENGINE_CLIP_KIND_MIDI;
        } else {
            clip->kind = ENGINE_CLIP_KIND_AUDIO;
        }
        return true;
    }
    double val;
    if (!json_parse_number(r, &val)) {
        return false;
    }
    clip->kind = ((int)val == (int)ENGINE_CLIP_KIND_MIDI) ? ENGINE_CLIP_KIND_MIDI : ENGINE_CLIP_KIND_AUDIO;
    return true;
}

static bool parse_session_instrument_preset(JsonReader* r, SessionClip* clip) {
    if (!r || !clip) {
        return false;
    }
    json_skip_whitespace(r);
    if (r->pos < r->length && r->data[r->pos] == '"') {
        char preset_id[48];
        if (!json_parse_string(r, preset_id, sizeof(preset_id))) {
            return false;
        }
        EngineInstrumentPresetId preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
        if (engine_instrument_preset_from_id_string(preset_id, &preset)) {
            clip->instrument_preset = preset;
            clip->instrument_params = engine_instrument_default_params(preset);
        }
        return true;
    }
    double val;
    if (!json_parse_number(r, &val)) {
        return false;
    }
    clip->instrument_preset = engine_instrument_preset_clamp((EngineInstrumentPresetId)(int)val);
    clip->instrument_params = engine_instrument_default_params(clip->instrument_preset);
    return true;
}

static bool parse_session_instrument_params(JsonReader* r, SessionClip* clip) {
    if (!r || !clip || !json_expect(r, '{')) {
        return false;
    }
    EngineInstrumentParams params = clip->instrument_params;
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char param_key[48];
        if (!json_parse_string(r, param_key, sizeof(param_key)) || !json_expect(r, ':')) {
            return false;
        }
        EngineInstrumentParamId param = ENGINE_INSTRUMENT_PARAM_LEVEL;
        if (engine_instrument_param_from_id_string(param_key, &param)) {
            double val = 0.0;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            params = engine_instrument_params_set(clip->instrument_preset, params, param, (float)val);
        } else if (!json_skip_value(r)) {
            return false;
        }
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ',') {
            ++r->pos;
            continue;
        }
    }
    clip->instrument_params = engine_instrument_params_sanitize(clip->instrument_preset, params);
    return true;
}

static bool session_clip_append_midi_note(SessionClip* clip, EngineMidiNote note) {
    if (!clip || !engine_midi_note_is_valid(&note)) {
        return false;
    }
    int new_count = clip->midi_note_count + 1;
    if (new_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    EngineMidiNote* resized = (EngineMidiNote*)realloc(clip->midi_notes,
                                                       (size_t)new_count * sizeof(EngineMidiNote));
    if (!resized) {
        return false;
    }
    clip->midi_notes = resized;
    clip->midi_notes[new_count - 1] = note;
    clip->midi_note_count = new_count;
    return true;
}

static bool parse_session_midi_notes(JsonReader* r, SessionClip* clip) {
    if (!r || !clip) {
        return false;
    }
    if (!json_expect(r, '[')) {
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
        EngineMidiNote note = {0};
        while (true) {
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == '}') {
                ++r->pos;
                break;
            }
            char note_key[32];
            if (!json_parse_string(r, note_key, sizeof(note_key))) {
                return false;
            }
            if (!json_expect(r, ':')) {
                return false;
            }
            if (strcmp(note_key, "start_frame") == 0) {
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                note.start_frame = (uint64_t)(val < 0 ? 0 : val);
            } else if (strcmp(note_key, "duration_frames") == 0) {
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                note.duration_frames = (uint64_t)(val < 0 ? 0 : val);
            } else if (strcmp(note_key, "note") == 0) {
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                if (val < 0) {
                    val = 0;
                } else if (val > ENGINE_MIDI_NOTE_MAX) {
                    val = ENGINE_MIDI_NOTE_MAX;
                }
                note.note = (uint8_t)val;
            } else if (strcmp(note_key, "velocity") == 0) {
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                note.velocity = (float)val;
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
        if (!session_clip_append_midi_note(clip, note)) {
            return false;
        }
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ',') {
            ++r->pos;
            continue;
        }
        if (r->pos < r->length && r->data[r->pos] == ']') {
            continue;
        }
    }
    return true;
}

// Appends an automation point into a session lane.
static bool session_lane_append_point(SessionAutomationLane* lane, uint64_t frame, float value) {
    if (!lane) {
        return false;
    }
    int new_count = lane->point_count + 1;
    SessionAutomationPoint* resized = (SessionAutomationPoint*)realloc(lane->points,
                                                                       (size_t)new_count * sizeof(SessionAutomationPoint));
    if (!resized) {
        return false;
    }
    lane->points = resized;
    lane->points[new_count - 1].frame = frame;
    lane->points[new_count - 1].value = value;
    lane->point_count = new_count;
    return true;
}

// Parses a points array for an automation lane.
static bool parse_session_automation_points(JsonReader* r, SessionAutomationLane* lane) {
    if (!r || !lane) {
        return false;
    }
    if (!json_expect(r, '[')) {
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
        uint64_t frame = 0;
        float value = 0.0f;
        while (true) {
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == '}') {
                ++r->pos;
                break;
            }
            char point_key[32];
            if (!json_parse_string(r, point_key, sizeof(point_key))) {
                return false;
            }
            if (!json_expect(r, ':')) {
                return false;
            }
            if (strcmp(point_key, "frame") == 0) {
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                frame = (uint64_t)(val < 0 ? 0 : val);
            } else if (strcmp(point_key, "value") == 0) {
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                value = (float)val;
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
        if (!session_lane_append_point(lane, frame, value)) {
            return false;
        }
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ',') {
            ++r->pos;
            continue;
        }
        if (r->pos < r->length && r->data[r->pos] == ']') {
            continue;
        }
    }
    return true;
}

bool parse_session_automation_lanes(JsonReader* r, SessionAutomationLane** out_lanes, int* out_lane_count) {
    if (!r || !out_lanes || !out_lane_count) {
        return false;
    }
    if (!json_expect(r, '[')) {
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
        int new_count = *out_lane_count + 1;
        SessionAutomationLane* resized = (SessionAutomationLane*)realloc(*out_lanes,
                                                                         (size_t)new_count * sizeof(SessionAutomationLane));
        if (!resized) {
            return false;
        }
        *out_lanes = resized;
        SessionAutomationLane* lane = &(*out_lanes)[new_count - 1];
        memset(lane, 0, sizeof(*lane));
        lane->target = ENGINE_AUTOMATION_TARGET_VOLUME;
        *out_lane_count = new_count;
        while (true) {
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == '}') {
                ++r->pos;
                break;
            }
            char lane_key[32];
            if (!json_parse_string(r, lane_key, sizeof(lane_key))) {
                return false;
            }
            if (!json_expect(r, ':')) {
                return false;
            }
            if (strcmp(lane_key, "target") == 0) {
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                lane->target = (EngineAutomationTarget)val;
            } else if (strcmp(lane_key, "points") == 0) {
                if (!parse_session_automation_points(r, lane)) {
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
            continue;
        }
    }
    return true;
}

// Parses automation lanes into a clip.
static bool parse_session_automation(JsonReader* r, SessionClip* clip) {
    if (!r || !clip) {
        return false;
    }
    return parse_session_automation_lanes(r, &clip->automation_lanes, &clip->automation_lane_count);
}

bool parse_session_track_clips(JsonReader* r, SessionTrack* track) {
    if (!json_expect(r, '[')) {
        return false;
    }
    json_skip_whitespace(r);
    if (r->pos < r->length && r->data[r->pos] == ']') {
        ++r->pos;
        return true;
    }
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
            } else if (strcmp(clip_key, "kind") == 0) {
                if (!parse_session_clip_kind(r, clip)) {
                    return false;
                }
            } else if (strcmp(clip_key, "media_id") == 0) {
                if (!json_parse_string(r, clip->media_id, sizeof(clip->media_id))) {
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
            } else if (strcmp(clip_key, "fade_in_curve") == 0) {
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                int curve = (int)val;
                if (curve < 0 || curve >= ENGINE_FADE_CURVE_COUNT) {
                    curve = ENGINE_FADE_CURVE_LINEAR;
                }
                clip->fade_in_curve = (EngineFadeCurve)curve;
            } else if (strcmp(clip_key, "fade_out_curve") == 0) {
                double val;
                if (!json_parse_number(r, &val)) {
                    return false;
                }
                int curve = (int)val;
                if (curve < 0 || curve >= ENGINE_FADE_CURVE_COUNT) {
                    curve = ENGINE_FADE_CURVE_LINEAR;
                }
                clip->fade_out_curve = (EngineFadeCurve)curve;
            } else if (strcmp(clip_key, "instrument_preset") == 0) {
                if (!parse_session_instrument_preset(r, clip)) {
                    return false;
                }
            } else if (strcmp(clip_key, "instrument_inherits_track") == 0) {
                if (!json_parse_bool(r, &clip->instrument_inherits_track)) {
                    return false;
                }
            } else if (strcmp(clip_key, "instrument_params") == 0) {
                if (!parse_session_instrument_params(r, clip)) {
                    return false;
                }
            } else if (strcmp(clip_key, "automation") == 0) {
                if (!parse_session_automation(r, clip)) {
                    return false;
                }
            } else if (strcmp(clip_key, "midi_notes") == 0) {
                if (!parse_session_midi_notes(r, clip)) {
                    return false;
                }
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
    return true;
}
