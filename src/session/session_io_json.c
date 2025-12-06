#include "session_io_read_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void json_reader_init(JsonReader* r, const char* data, size_t length) {
    r->data = data;
    r->length = length;
    r->pos = 0;
}

void json_skip_whitespace(JsonReader* r) {
    while (r->pos < r->length) {
        char ch = r->data[r->pos];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            ++r->pos;
        } else {
            break;
        }
    }
}

bool json_expect(JsonReader* r, char expected) {
    json_skip_whitespace(r);
    if (r->pos >= r->length || r->data[r->pos] != expected) {
        return false;
    }
    ++r->pos;
    return true;
}

bool json_match_literal(JsonReader* r, const char* literal) {
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

bool json_parse_bool(JsonReader* r, bool* out_value) {
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

bool json_parse_number(JsonReader* r, double* out_value) {
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

bool json_parse_string(JsonReader* r, char* dst, size_t dst_len) {
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

static bool json_skip_value_internal(JsonReader* r);

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
        if (!json_skip_value_internal(r)) {
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
        if (!json_skip_value_internal(r)) {
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

static bool json_skip_value_internal(JsonReader* r) {
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
        case '\"':
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

bool json_skip_value(JsonReader* r) {
    return json_skip_value_internal(r);
}
