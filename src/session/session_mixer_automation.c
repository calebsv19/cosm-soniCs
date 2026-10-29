#include "session_io_read_internal.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Reads exact unsigned project frames without accepting coercion, overflow or leading zero aliases.
static bool frame(JsonReader* r, uint64_t* out) {
    char s[32];
    if (!json_parse_string(r, s, sizeof(s)) || !*s || (s[0] == '0' && s[1]))
        return false;
    for (char* p = s; *p; ++p)
        if (*p < '0' || *p > '9')
            return false;
    errno = 0;
    char* end;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno || *end)
        return false;
    *out = v;
    return true;
}
// Reads strict complete fixed-capacity mixer curves with no unbounded allocations.
bool session_parse_parameter_automation(JsonReader* r, EngineMixerAutomation* out, float minimum, float maximum) {
    *out = (EngineMixerAutomation){0};
    if (!json_expect(r, '{'))
        return false;
    unsigned seen = 0;
    for (int field = 0; field < 3; ++field) {
        if (field && !json_expect(r, ','))
            return false;
        char key[32];
        if (!json_parse_string(r, key, sizeof(key)) || !json_expect(r, ':'))
            return false;
        if (!strcmp(key, "before") || !strcmp(key, "after")) {
            unsigned bit = !strcmp(key, "before") ? 1 : 2;
            double v;
            if ((seen & bit) || !json_parse_number(r, &v) || !isfinite(v) || (v != 0 && v != 1))
                return false;
            seen |= bit;
            if (bit == 1)
                out->before = (int)v;
            else
                out->after = (int)v;
        } else if (!strcmp(key, "points")) {
            if (seen & 4 || !json_expect(r, '['))
                return false;
            seen |= 4;
            json_skip_whitespace(r);
            if (r->pos < r->length && r->data[r->pos] == ']') {
                ++r->pos;
                continue;
            }
            for (;;) {
                if (out->count >= ENGINE_MIX_AUTOMATION_POINTS || !json_expect(r, '{'))
                    return false;
                EngineAutomationPoint* p = &out->points[out->count++];
                unsigned fields = 0;
                for (int j = 0; j < 3; ++j) {
                    if (j && !json_expect(r, ','))
                        return false;
                    char k[32];
                    if (!json_parse_string(r, k, sizeof(k)) || !json_expect(r, ':'))
                        return false;
                    double v;
                    if (!strcmp(k, "frame")) {
                        if (fields & 1 || !frame(r, &p->frame))
                            return false;
                        fields |= 1;
                    } else if (!strcmp(k, "value")) {
                        if (fields & 2 || !json_parse_number(r, &v) || !isfinite(v) || v < minimum ||
                            v > maximum)
                            return false;
                        p->value = (float)v;
                        fields |= 2;
                    } else if (!strcmp(k, "curve")) {
                        if (fields & 4 || !json_parse_number(r, &v) || !isfinite(v) || v < 0 ||
                            v >= ENGINE_AUTOMATION_CURVE_COUNT || floor(v) != v)
                            return false;
                        p->curve = (int)v;
                        fields |= 4;
                    } else
                        return false;
                }
                if (fields != 7 || !json_expect(r, '}'))
                    return false;
                json_skip_whitespace(r);
                if (r->pos < r->length && r->data[r->pos] == ']') {
                    ++r->pos;
                    break;
                }
                if (!json_expect(r, ','))
                    return false;
            }
        } else
            return false;
    }
    return seen == 7 && json_expect(r, '}') && engine_mixer_automation_valid_range(out, minimum, maximum);
}
// Retains nonnegative physical mixer persistence.
bool session_parse_mixer_automation(JsonReader* r,EngineMixerAutomation* a,float maximum){return session_parse_parameter_automation(r,a,0,maximum);}

// Writes stable full-width frame strings and round-trip float gain values.
void session_write_mixer_automation(FILE* f, const EngineMixerAutomation* a) {
    fprintf(f, "{\"before\": %d, \"after\": %d, \"points\": [", a->before, a->after);
    for (int i = 0; i < a->count; ++i) {
        if (i)
            fprintf(f, ", ");
        fprintf(f, "{\"frame\": \"%" PRIu64 "\", \"value\": %.9g, \"curve\": %d}",
                a->points[i].frame, (double)a->points[i].value, a->points[i].curve);
    }
    fprintf(f, "]}");
}
