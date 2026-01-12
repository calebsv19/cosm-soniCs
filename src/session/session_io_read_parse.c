#include "session_io_read_internal.h"

#include <stdlib.h>
#include <string.h>

static SessionTrack* session_document_append_track(SessionDocument* doc) {
    int new_count = doc->track_count + 1;
    SessionTrack* resized = (SessionTrack*)realloc(doc->tracks, (size_t)new_count * sizeof(SessionTrack));
    if (!resized) {
        return NULL;
    }
    doc->tracks = resized;
    SessionTrack* track = &doc->tracks[new_count - 1];
    memset(track, 0, sizeof(*track));
    track->eq.low_cut.enabled = false;
    track->eq.low_cut.freq_hz = 80.0f;
    track->eq.low_cut.slope = 12.0f;
    track->eq.high_cut.enabled = false;
    track->eq.high_cut.freq_hz = 12000.0f;
    track->eq.high_cut.slope = 12.0f;
    for (int i = 0; i < 4; ++i) {
        track->eq.bands[i].enabled = true;
        track->eq.bands[i].gain_db = 0.0f;
        track->eq.bands[i].q_width = 1.0f;
    }
    track->eq.bands[0].freq_hz = 120.0f;
    track->eq.bands[1].freq_hz = 500.0f;
    track->eq.bands[2].freq_hz = 2000.0f;
    track->eq.bands[3].freq_hz = 8000.0f;
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

bool parse_session_tracks(JsonReader* r, SessionDocument* doc);
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

bool parse_master_fx(JsonReader* r, SessionDocument* doc);

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
        } else if (strcmp(key, "pan") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->pan = (float)val;
        } else if (strcmp(key, "muted") == 0) {
            if (!json_parse_bool(r, &track->muted)) {
                return false;
            }
        } else if (strcmp(key, "solo") == 0) {
            if (!json_parse_bool(r, &track->solo)) {
                return false;
            }
        } else if (strcmp(key, "eq_low_cut_enabled") == 0) {
            if (!json_parse_bool(r, &track->eq.low_cut.enabled)) {
                return false;
            }
        } else if (strcmp(key, "eq_low_cut_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.low_cut.freq_hz = (float)val;
        } else if (strcmp(key, "eq_low_cut_slope") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.low_cut.slope = (float)val;
        } else if (strcmp(key, "eq_high_cut_enabled") == 0) {
            if (!json_parse_bool(r, &track->eq.high_cut.enabled)) {
                return false;
            }
        } else if (strcmp(key, "eq_high_cut_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.high_cut.freq_hz = (float)val;
        } else if (strcmp(key, "eq_high_cut_slope") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.high_cut.slope = (float)val;
        } else if (strcmp(key, "eq_band_0_enabled") == 0) {
            if (!json_parse_bool(r, &track->eq.bands[0].enabled)) {
                return false;
            }
        } else if (strcmp(key, "eq_band_0_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[0].freq_hz = (float)val;
        } else if (strcmp(key, "eq_band_0_gain") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[0].gain_db = (float)val;
        } else if (strcmp(key, "eq_band_0_q") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[0].q_width = (float)val;
        } else if (strcmp(key, "eq_band_1_enabled") == 0) {
            if (!json_parse_bool(r, &track->eq.bands[1].enabled)) {
                return false;
            }
        } else if (strcmp(key, "eq_band_1_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[1].freq_hz = (float)val;
        } else if (strcmp(key, "eq_band_1_gain") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[1].gain_db = (float)val;
        } else if (strcmp(key, "eq_band_1_q") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[1].q_width = (float)val;
        } else if (strcmp(key, "eq_band_2_enabled") == 0) {
            if (!json_parse_bool(r, &track->eq.bands[2].enabled)) {
                return false;
            }
        } else if (strcmp(key, "eq_band_2_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[2].freq_hz = (float)val;
        } else if (strcmp(key, "eq_band_2_gain") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[2].gain_db = (float)val;
        } else if (strcmp(key, "eq_band_2_q") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[2].q_width = (float)val;
        } else if (strcmp(key, "eq_band_3_enabled") == 0) {
            if (!json_parse_bool(r, &track->eq.bands[3].enabled)) {
                return false;
            }
        } else if (strcmp(key, "eq_band_3_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[3].freq_hz = (float)val;
        } else if (strcmp(key, "eq_band_3_gain") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[3].gain_db = (float)val;
        } else if (strcmp(key, "eq_band_3_q") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            track->eq.bands[3].q_width = (float)val;
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
        } else if (strcmp(key, "fx") == 0) {
            if (!json_expect(r, '[')) {
                return false;
            }
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == ']') {
                ++r->pos;
            } else {
                while (true) {
                    int new_count = track->fx_count + 1;
                    SessionFxInstance* resized = (SessionFxInstance*)realloc(track->fx, (size_t)new_count * sizeof(SessionFxInstance));
                    if (!resized) {
                        return false;
                    }
                    track->fx = resized;
                    SessionFxInstance* fx = &track->fx[new_count - 1];
                    memset(fx, 0, sizeof(*fx));
                    fx->enabled = true;
                    track->fx_count = new_count;

                    if (!json_expect(r, '{')) {
                        return false;
                    }
                    while (true) {
                        json_skip_whitespace(r);
                        if (r->pos < r->length && r->data[r->pos] == '}') {
                            ++r->pos;
                            break;
                        }
                        char fx_key[64];
                        if (!json_parse_string(r, fx_key, sizeof(fx_key))) {
                            return false;
                        }
                        if (!json_expect(r, ':')) {
                            return false;
                        }
                        if (strcmp(fx_key, "type") == 0) {
                            double val;
                            if (!json_parse_number(r, &val)) {
                                return false;
                            }
                            fx->type = (FxTypeId)(val < 0 ? 0 : val);
                        } else if (strcmp(fx_key, "name") == 0) {
                            json_parse_string(r, fx->name, sizeof(fx->name));
                        } else if (strcmp(fx_key, "enabled") == 0) {
                            if (!json_parse_bool(r, &fx->enabled)) {
                                return false;
                            }
                        } else if (strcmp(fx_key, "params") == 0) {
                            if (!json_expect(r, '[')) {
                                return false;
                            }
                            json_skip_whitespace(r);
                            uint32_t pcount = 0;
                            if (r->pos < r->length && r->data[r->pos] == ']') {
                                ++r->pos;
                            } else {
                                while (true) {
                                    double val;
                                    if (!json_parse_number(r, &val)) {
                                        return false;
                                    }
                                    if (pcount < FX_MAX_PARAMS) {
                                        fx->params[pcount] = (float)val;
                                    }
                                    ++pcount;
                                    json_skip_whitespace(r);
                                    if (r->pos < r->length && r->data[r->pos] == ',') {
                                        ++r->pos;
                                        continue;
                                    }
                                    break;
                                }
                                if (!json_expect(r, ']')) {
                                    return false;
                                }
                            }
                            fx->param_count = pcount;
                        } else if (strcmp(fx_key, "param_modes") == 0) {
                            if (!json_expect(r, '[')) {
                                return false;
                            }
                            json_skip_whitespace(r);
                            uint32_t idx = 0;
                            if (r->pos < r->length && r->data[r->pos] == ']') {
                                ++r->pos;
                            } else {
                                while (true) {
                                    double val;
                                    if (!json_parse_number(r, &val)) {
                                        return false;
                                    }
                                    if (idx < FX_MAX_PARAMS) {
                                        fx->param_mode[idx] = (FxParamMode)((int)val);
                                    }
                                    ++idx;
                                    json_skip_whitespace(r);
                                    if (r->pos < r->length && r->data[r->pos] == ',') {
                                        ++r->pos;
                                        continue;
                                    }
                                    break;
                                }
                                if (!json_expect(r, ']')) {
                                    return false;
                                }
                            }
                        } else if (strcmp(fx_key, "param_beats") == 0) {
                            if (!json_expect(r, '[')) {
                                return false;
                            }
                            json_skip_whitespace(r);
                            uint32_t idx = 0;
                            if (r->pos < r->length && r->data[r->pos] == ']') {
                                ++r->pos;
                            } else {
                                while (true) {
                                    double val;
                                    if (!json_parse_number(r, &val)) {
                                        return false;
                                    }
                                    if (idx < FX_MAX_PARAMS) {
                                        fx->param_beats[idx] = (float)val;
                                    }
                                    ++idx;
                                    json_skip_whitespace(r);
                                    if (r->pos < r->length && r->data[r->pos] == ',') {
                                        ++r->pos;
                                        continue;
                                    }
                                    break;
                                }
                                if (!json_expect(r, ']')) {
                                    return false;
                                }
                            }
                        } else if (strcmp(fx_key, "param_count") == 0) {
                            double val;
                            if (!json_parse_number(r, &val)) {
                                return false;
                            }
                            fx->param_count = (uint32_t)(val < 0 ? 0 : val);
                            if (fx->param_count > FX_MAX_PARAMS) {
                                fx->param_count = FX_MAX_PARAMS;
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
                    if (r->pos >= r->length) {
                        return false;
                    }
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

bool parse_session_tracks(JsonReader* r, SessionDocument* doc) {
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

bool parse_master_fx(JsonReader* r, SessionDocument* doc) {
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
            } else if (strcmp(key, "param_modes") == 0) {
                if (!json_expect(r, '[')) {
                    return false;
                }
                json_skip_whitespace(r);
                uint32_t idx = 0;
                if (r->pos < r->length && r->data[r->pos] == ']') {
                    ++r->pos;
                } else {
                    while (true) {
                        double val;
                        if (!json_parse_number(r, &val)) {
                            return false;
                        }
                        if (idx < FX_MAX_PARAMS) {
                            fx->param_mode[idx] = (FxParamMode)((int)val);
                        }
                        ++idx;
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
            } else if (strcmp(key, "param_beats") == 0) {
                if (!json_expect(r, '[')) {
                    return false;
                }
                json_skip_whitespace(r);
                uint32_t idx = 0;
                if (r->pos < r->length && r->data[r->pos] == ']') {
                    ++r->pos;
                } else {
                    while (true) {
                        double val;
                        if (!json_parse_number(r, &val)) {
                            return false;
                        }
                        if (idx < FX_MAX_PARAMS) {
                            fx->param_beats[idx] = (float)val;
                        }
                        ++idx;
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

bool parse_session_document(JsonReader* r, SessionDocument* doc) {
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
        } else if (strcmp(key, "tempo") == 0) {
            if (!json_expect(r, '{')) {
                return false;
            }
            while (true) {
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                char tempo_key[64];
                if (!json_parse_string(r, tempo_key, sizeof(tempo_key))) {
                    return false;
                }
                if (!json_expect(r, ':')) {
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
        } else if (strcmp(key, "clip_inspector") == 0) {
            if (!json_expect(r, '{')) {
                return false;
            }
            while (true) {
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                char panel_key[64];
                if (!json_parse_string(r, panel_key, sizeof(panel_key))) {
                    return false;
                }
                if (!json_expect(r, ':')) {
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
        } else if (strcmp(key, "effects_panel") == 0) {
            if (!json_expect(r, '{')) {
                return false;
            }
            while (true) {
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                    break;
                }
                char panel_key[64];
                if (!json_parse_string(r, panel_key, sizeof(panel_key))) {
                    return false;
                }
                if (!json_expect(r, ':')) {
                    return false;
                }
                if (strcmp(panel_key, "view_mode") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.view_mode = (int)val;
                } else if (strcmp(panel_key, "selected_index") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.selected_index = (int)val;
                } else if (strcmp(panel_key, "open_index") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.open_index = (int)val;
                } else if (strcmp(panel_key, "list_detail_mode") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.list_detail_mode = (int)val;
                } else if (strcmp(panel_key, "eq_view_mode") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.eq_view_mode = (int)val;
                } else if (strcmp(panel_key, "low_cut_enabled") == 0) {
                    if (!json_parse_bool(r, &doc->effects_panel.low_cut.enabled)) {
                        return false;
                    }
                } else if (strcmp(panel_key, "low_cut_freq") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.low_cut.freq_hz = (float)val;
                } else if (strcmp(panel_key, "low_cut_slope") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.low_cut.slope = (float)val;
                } else if (strcmp(panel_key, "high_cut_enabled") == 0) {
                    if (!json_parse_bool(r, &doc->effects_panel.high_cut.enabled)) {
                        return false;
                    }
                } else if (strcmp(panel_key, "high_cut_freq") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.high_cut.freq_hz = (float)val;
                } else if (strcmp(panel_key, "high_cut_slope") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.high_cut.slope = (float)val;
                } else if (strcmp(panel_key, "band_0_enabled") == 0) {
                    if (!json_parse_bool(r, &doc->effects_panel.bands[0].enabled)) {
                        return false;
                    }
                } else if (strcmp(panel_key, "band_0_freq") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[0].freq_hz = (float)val;
                } else if (strcmp(panel_key, "band_0_gain") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[0].gain_db = (float)val;
                } else if (strcmp(panel_key, "band_0_q") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[0].q_width = (float)val;
                } else if (strcmp(panel_key, "band_1_enabled") == 0) {
                    if (!json_parse_bool(r, &doc->effects_panel.bands[1].enabled)) {
                        return false;
                    }
                } else if (strcmp(panel_key, "band_1_freq") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[1].freq_hz = (float)val;
                } else if (strcmp(panel_key, "band_1_gain") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[1].gain_db = (float)val;
                } else if (strcmp(panel_key, "band_1_q") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[1].q_width = (float)val;
                } else if (strcmp(panel_key, "band_2_enabled") == 0) {
                    if (!json_parse_bool(r, &doc->effects_panel.bands[2].enabled)) {
                        return false;
                    }
                } else if (strcmp(panel_key, "band_2_freq") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[2].freq_hz = (float)val;
                } else if (strcmp(panel_key, "band_2_gain") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[2].gain_db = (float)val;
                } else if (strcmp(panel_key, "band_2_q") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[2].q_width = (float)val;
                } else if (strcmp(panel_key, "band_3_enabled") == 0) {
                    if (!json_parse_bool(r, &doc->effects_panel.bands[3].enabled)) {
                        return false;
                    }
                } else if (strcmp(panel_key, "band_3_freq") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[3].freq_hz = (float)val;
                } else if (strcmp(panel_key, "band_3_gain") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[3].gain_db = (float)val;
                } else if (strcmp(panel_key, "band_3_q") == 0) {
                    double val;
                    if (!json_parse_number(r, &val)) {
                        return false;
                    }
                    doc->effects_panel.bands[3].q_width = (float)val;
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
