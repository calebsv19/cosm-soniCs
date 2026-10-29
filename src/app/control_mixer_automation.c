#include "daw/mixer_automation_wire.h"
#include "cJSON.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Rejects duplicate and unknown object fields before interpreting a curve.
static bool fields(const cJSON* o, const char* const* allowed) {
    if (!cJSON_IsObject(o))
        return false;
    for (const cJSON* p = o->child; p; p = p->next) {
        bool found = false;
        for (int i = 0; allowed[i]; ++i)
            if (!strcmp(p->string, allowed[i]))
                found = true;
        if (!found)
            return false;
        for (const cJSON* q = p->next; q; q = q->next)
            if (!strcmp(p->string, q->string))
                return false;
    }
    return true;
}
// Reads finite integral enums with no bool or fractional coercion.
static bool integer(const cJSON* o, const char* key, int max, int* out) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 ||
        v->valuedouble > max || floor(v->valuedouble) != v->valuedouble)
        return false;
    *out = v->valueint;
    return true;
}
// Preserves canonical full-width unsigned project frames on the wire.
static bool frame(const cJSON* o, uint64_t* out) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, "frame");
    if (!cJSON_IsString(v) || !*v->valuestring || (v->valuestring[0] == '0' && v->valuestring[1]))
        return false;
    for (char* p = v->valuestring; *p; ++p)
        if (*p < '0' || *p > '9')
            return false;
    errno = 0;
    char* end;
    unsigned long long n = strtoull(v->valuestring, &end, 10);
    if (errno || *end)
        return false;
    *out = n;
    return true;
}
// Emits physical gain points without truncating captured state or borrowing queued text.
void daw_mixer_automation_json(cJSON* j, const EngineMixerAutomation* a) {
    cJSON_AddNumberToObject(j, "before", a->before);
    cJSON_AddNumberToObject(j, "after", a->after);
    cJSON* array = cJSON_AddArrayToObject(j, "points");
    for (int i = 0; i < a->count; ++i) {
        const EngineAutomationPoint* p = &a->points[i];
        cJSON* item = cJSON_CreateObject();
        cJSON_AddItemToArray(array, item);
        char s[32];
        snprintf(s, sizeof(s), "%" PRIu64, p->frame);
        cJSON_AddStringToObject(item, "frame", s);
        cJSON_AddNumberToObject(item, "value", p->value);
        cJSON_AddNumberToObject(item, "curve", p->curve);
    }
}
// Parses one complete candidate before the existing guarded routing publication/history path.
bool daw_parameter_automation_parse(const cJSON* j, EngineMixerAutomation* a, float minimum, float maximum) {
    const char* const keys[] = {"before", "after", "points", NULL};
    const char* const point_keys[] = {"frame", "value", "curve", NULL};
    if (!fields(j, keys))
        return false;
    *a = (EngineMixerAutomation){0};
    int before, after;
    if (!integer(j, "before", 1, &before) || !integer(j, "after", 1, &after))
        return false;
    a->before = before;
    a->after = after;
    const cJSON* array = cJSON_GetObjectItemCaseSensitive(j, "points");
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > ENGINE_MIX_AUTOMATION_POINTS)
        return false;
    a->count = cJSON_GetArraySize(array);
    for (int i = 0; i < a->count; ++i) {
        const cJSON* p = cJSON_GetArrayItem(array, i);
        int curve;
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(p, "value");
        if (!fields(p, point_keys) || !frame(p, &a->points[i].frame) ||
            !integer(p, "curve", 2, &curve) || !cJSON_IsNumber(value) ||
            !isfinite(value->valuedouble) || value->valuedouble < minimum || value->valuedouble > maximum)
            return false;
        a->points[i].value = (float)value->valuedouble;
        a->points[i].curve = curve;
    }
    return engine_mixer_automation_valid_range(a, minimum, maximum);
}

// Retains strict nonnegative physical mixer curves.
bool daw_mixer_automation_parse(const cJSON* j,EngineMixerAutomation* a,float maximum){return daw_parameter_automation_parse(j,a,0,maximum);}
