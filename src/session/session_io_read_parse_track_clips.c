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
    track->clip_count = new_count;
    return clip;
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

// Parses automation lanes into a clip.
static bool parse_session_automation(JsonReader* r, SessionClip* clip) {
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
        int new_count = clip->automation_lane_count + 1;
        SessionAutomationLane* resized = (SessionAutomationLane*)realloc(clip->automation_lanes,
                                                                         (size_t)new_count * sizeof(SessionAutomationLane));
        if (!resized) {
            return false;
        }
        clip->automation_lanes = resized;
        SessionAutomationLane* lane = &clip->automation_lanes[new_count - 1];
        memset(lane, 0, sizeof(*lane));
        lane->target = ENGINE_AUTOMATION_TARGET_VOLUME;
        clip->automation_lane_count = new_count;
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
            } else if (strcmp(clip_key, "automation") == 0) {
                if (!parse_session_automation(r, clip)) {
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
