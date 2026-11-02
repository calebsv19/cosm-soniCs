#include "session_io_read_internal.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
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

// Reads strict finite JSON numbers, retaining negative zero and refusing malformed exponents.
bool json_parse_number(JsonReader* r,double* out_value) {
    json_skip_whitespace(r);size_t start=r->pos;
    if(r->pos<r->length && r->data[r->pos]=='-')++r->pos;
    if(r->pos>=r->length)return false;
    if(r->data[r->pos]=='0')++r->pos;
    else {size_t begin=r->pos;while(r->pos<r->length && isdigit((unsigned char)r->data[r->pos]))++r->pos;if(begin==r->pos)return false;}
    if(r->pos<r->length && r->data[r->pos]=='.') {size_t begin=++r->pos;while(r->pos<r->length&&isdigit((unsigned char)r->data[r->pos]))++r->pos;if(begin==r->pos)return false;}
    if(r->pos<r->length && (r->data[r->pos]=='e'||r->data[r->pos]=='E')) {
        ++r->pos;if(r->pos<r->length&&(r->data[r->pos]=='+'||r->data[r->pos]=='-'))++r->pos;
        size_t begin=r->pos;while(r->pos<r->length&&isdigit((unsigned char)r->data[r->pos]))++r->pos;if(begin==r->pos)return false;
    }
    errno=0;char* end;double n=strtod(r->data+start,&end);
    if(end!=r->data+r->pos || !isfinite(n) || fabs(n)>FLT_MAX || (errno && n==0))return false;
    if(out_value)*out_value=n;return true;
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
