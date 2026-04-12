#include "session_io_read_internal.h"

#include <stdlib.h>
#include <string.h>

// Finds or appends a parameter id entry for session FX parsing.
static int session_fx_find_or_add_param_id(SessionFxInstance* fx, const char* id) {
    if (!fx || !id || id[0] == '\0') {
        return -1;
    }
    for (uint32_t i = 0; i < fx->param_id_count && i < FX_MAX_PARAMS; ++i) {
        if (strncmp(fx->param_ids[i], id, sizeof(fx->param_ids[i])) == 0) {
            return (int)i;
        }
    }
    if (fx->param_id_count >= FX_MAX_PARAMS) {
        return -1;
    }
    uint32_t idx = fx->param_id_count++;
    strncpy(fx->param_ids[idx], id, sizeof(fx->param_ids[idx]) - 1);
    fx->param_ids[idx][sizeof(fx->param_ids[idx]) - 1] = '\0';
    return (int)idx;
}

bool parse_session_track_fx(JsonReader* r, SessionTrack* track) {
    if (!json_expect(r, '[')) {
        return false;
    }
    json_skip_whitespace(r);
    if (r->pos < r->length && r->data[r->pos] == ']') {
        ++r->pos;
        return true;
    }
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
            } else if (strcmp(fx_key, "param_ids") == 0) {
                if (!json_expect(r, '[')) {
                    return false;
                }
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == ']') {
                    ++r->pos;
                } else {
                    while (true) {
                        char id_buf[64];
                        if (!json_parse_string(r, id_buf, sizeof(id_buf))) {
                            return false;
                        }
                        session_fx_find_or_add_param_id(fx, id_buf);
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
            } else if (strcmp(fx_key, "param_values_by_id") == 0) {
                if (!json_expect(r, '{')) {
                    return false;
                }
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                } else {
                    while (true) {
                        char id_buf[64];
                        if (!json_parse_string(r, id_buf, sizeof(id_buf))) {
                            return false;
                        }
                        if (!json_expect(r, ':')) {
                            return false;
                        }
                        double val;
                        if (!json_parse_number(r, &val)) {
                            return false;
                        }
                        int idx = session_fx_find_or_add_param_id(fx, id_buf);
                        if (idx >= 0) {
                            fx->param_values_by_id[idx] = (float)val;
                        }
                        json_skip_whitespace(r);
                        if (r->pos < r->length && r->data[r->pos] == ',') {
                            ++r->pos;
                            continue;
                        }
                        break;
                    }
                    if (!json_expect(r, '}')) {
                        return false;
                    }
                }
            } else if (strcmp(fx_key, "param_modes_by_id") == 0) {
                if (!json_expect(r, '{')) {
                    return false;
                }
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                } else {
                    while (true) {
                        char id_buf[64];
                        if (!json_parse_string(r, id_buf, sizeof(id_buf))) {
                            return false;
                        }
                        if (!json_expect(r, ':')) {
                            return false;
                        }
                        double val;
                        if (!json_parse_number(r, &val)) {
                            return false;
                        }
                        int idx = session_fx_find_or_add_param_id(fx, id_buf);
                        if (idx >= 0) {
                            fx->param_modes_by_id[idx] = (FxParamMode)((int)val);
                        }
                        json_skip_whitespace(r);
                        if (r->pos < r->length && r->data[r->pos] == ',') {
                            ++r->pos;
                            continue;
                        }
                        break;
                    }
                    if (!json_expect(r, '}')) {
                        return false;
                    }
                }
            } else if (strcmp(fx_key, "param_beats_by_id") == 0) {
                if (!json_expect(r, '{')) {
                    return false;
                }
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == '}') {
                    ++r->pos;
                } else {
                    while (true) {
                        char id_buf[64];
                        if (!json_parse_string(r, id_buf, sizeof(id_buf))) {
                            return false;
                        }
                        if (!json_expect(r, ':')) {
                            return false;
                        }
                        double val;
                        if (!json_parse_number(r, &val)) {
                            return false;
                        }
                        int idx = session_fx_find_or_add_param_id(fx, id_buf);
                        if (idx >= 0) {
                            fx->param_beats_by_id[idx] = (float)val;
                        }
                        json_skip_whitespace(r);
                        if (r->pos < r->length && r->data[r->pos] == ',') {
                            ++r->pos;
                            continue;
                        }
                        break;
                    }
                    if (!json_expect(r, '}')) {
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
    return true;
}
