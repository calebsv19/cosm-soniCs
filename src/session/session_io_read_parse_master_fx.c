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
            } else if (strcmp(key, "param_ids") == 0) {
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
                        if (r->pos < r->length && r->data[r->pos] == ']') {
                            ++r->pos;
                            break;
                        }
                        return false;
                    }
                }
            } else if (strcmp(key, "param_values_by_id") == 0) {
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
                        if (r->pos < r->length && r->data[r->pos] == '}') {
                            ++r->pos;
                            break;
                        }
                        return false;
                    }
                }
            } else if (strcmp(key, "param_modes_by_id") == 0) {
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
                        if (r->pos < r->length && r->data[r->pos] == '}') {
                            ++r->pos;
                            break;
                        }
                        return false;
                    }
                }
            } else if (strcmp(key, "param_beats_by_id") == 0) {
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
                        if (r->pos < r->length && r->data[r->pos] == '}') {
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
