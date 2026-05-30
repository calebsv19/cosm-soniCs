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
    track->midi_instrument_enabled = false;
    track->midi_instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    track->midi_instrument_params = engine_instrument_default_params(track->midi_instrument_preset);
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

static bool parse_session_track_midi_instrument_preset(JsonReader* r, SessionTrack* track) {
    if (!r || !track) {
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
            track->midi_instrument_preset = preset;
            track->midi_instrument_params = engine_instrument_default_params(preset);
        }
        return true;
    }
    double val;
    if (!json_parse_number(r, &val)) {
        return false;
    }
    track->midi_instrument_preset = engine_instrument_preset_clamp((EngineInstrumentPresetId)(int)val);
    track->midi_instrument_params = engine_instrument_default_params(track->midi_instrument_preset);
    return true;
}

static bool parse_session_track_midi_instrument_params(JsonReader* r, SessionTrack* track) {
    if (!r || !track || !json_expect(r, '{')) {
        return false;
    }
    EngineInstrumentParams params = track->midi_instrument_params;
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
            params = engine_instrument_params_set(track->midi_instrument_preset, params, param, (float)val);
        } else if (!json_skip_value(r)) {
            return false;
        }
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ',') {
            ++r->pos;
            continue;
        }
    }
    track->midi_instrument_params =
        engine_instrument_params_sanitize(track->midi_instrument_preset, params);
    return true;
}

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
        } else if (strcmp(key, "midi_instrument_enabled") == 0) {
            if (!json_parse_bool(r, &track->midi_instrument_enabled)) {
                return false;
            }
        } else if (strcmp(key, "midi_instrument_preset") == 0) {
            if (!parse_session_track_midi_instrument_preset(r, track)) {
                return false;
            }
        } else if (strcmp(key, "midi_instrument_params") == 0) {
            if (!parse_session_track_midi_instrument_params(r, track)) {
                return false;
            }
        } else if (strcmp(key, "midi_instrument_automation") == 0) {
            if (!parse_session_automation_lanes(r,
                                                &track->midi_instrument_automation_lanes,
                                                &track->midi_instrument_automation_lane_count)) {
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
            if (!parse_session_track_clips(r, track)) {
                return false;
            }
        } else if (strcmp(key, "fx") == 0) {
            if (!parse_session_track_fx(r, track)) {
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
            if (!parse_session_document_engine(r, doc)) {
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
            if (!parse_session_document_tempo(r, doc)) {
                return false;
            }
        } else if (strcmp(key, "tempo_map") == 0) {
            if (!parse_session_document_tempo_map(r, doc)) {
                return false;
            }
        } else if (strcmp(key, "time_signature_map") == 0) {
            if (!parse_session_document_time_signature_map(r, doc)) {
                return false;
            }
        } else if (strcmp(key, "loop") == 0) {
            if (!parse_session_document_loop(r, doc)) {
                return false;
            }
        } else if (strcmp(key, "timeline") == 0) {
            if (!parse_session_document_timeline(r, doc)) {
                return false;
            }
        } else if (strcmp(key, "selected_track_index") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->selected_track_index = (int)val;
        } else if (strcmp(key, "selected_clip_index") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->selected_clip_index = (int)val;
        } else if (strcmp(key, "midi_editor") == 0) {
            if (!parse_session_midi_editor(r, doc)) {
                return false;
            }
        } else if (strcmp(key, "clip_inspector") == 0) {
            if (!parse_session_document_clip_inspector(r, doc)) {
                return false;
            }
        } else if (strcmp(key, "effects_panel") == 0) {
            if (!parse_session_document_effects_panel(r, doc)) {
                return false;
            }
        } else if (strcmp(key, "layout") == 0) {
            if (!parse_session_document_layout(r, doc)) {
                return false;
            }
        } else if (strcmp(key, "library") == 0) {
            if (!parse_session_document_library(r, doc)) {
                return false;
            }
        } else if (strcmp(key, "data_paths") == 0) {
            if (!parse_session_document_data_paths(r, doc)) {
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
