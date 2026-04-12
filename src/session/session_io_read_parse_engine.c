#include "session_io_read_internal.h"

#include <string.h>

bool parse_session_document_engine(JsonReader* r, SessionDocument* doc) {
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
    return true;
}
