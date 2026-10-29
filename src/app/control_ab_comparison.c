#include "control_bridge_internal.h"
#include "app_state.h"
#include "daw/ab_comparison.h"
#include "cJSON.h"
#include <errno.h>
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Separates accepted analysis/listening cache changes from authored project mutations.
static cJSON* reply(bool ok, const char* code) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", ok);
    cJSON_AddStringToObject(j, "code", code);
    return j;
}
// Reads typed strings without numeric or boolean coercion.
static const char* text(const cJSON* j, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
// Preserves exact frame and generation values in compact readback.
static void id(cJSON* j, const char* key, uint64_t value) {
    char s[32];
    snprintf(s, sizeof(s), "%" PRIu64, value);
    cJSON_AddStringToObject(j, key, s);
}
// Identifies current ephemeral native/MCP cache ownership with an exact project/revision guard.
static void token(const AppState* s, char out[80]) {
    snprintf(out, 80, "%" PRIu64 ":ab:%" PRIu64, engine_instance_id(s->engine), daw_ab_revision(s));
}
// Rejects aliases, signs, overflow and numeric frame payloads before rendering.
static bool frame(const cJSON* j, const char* key, bool optional, uint64_t* out) {
    *out = 0;
    const char* p = text(j, key);
    if (!cJSON_GetObjectItemCaseSensitive(j, key))
        return optional;
    if (!p || !*p || strlen(p) > 20 || (p[0] == '0' && p[1]))
        return false;
    for (const char* q = p; *q; ++q)
        if (*q < '0' || *q > '9')
            return false;
    errno = 0;
    char* end;
    *out = strtoull(p, &end, 10);
    return !errno && !*end;
}
// Reads optional booleans strictly, retaining explicit false values.
static bool boolean(const cJSON* j, const char* key, bool fallback, bool* out) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (v && !cJSON_IsBool(v))
        return false;
    *out = v ? cJSON_IsTrue(v) : fallback;
    return true;
}
// Describes captured original levels, attenuation-only listening gains and atomic completion.
static void describe(cJSON* j, const AppState* s) {
    char guard[80];
    token(s, guard);
    cJSON_AddStringToObject(j, "comparison_id", guard);
    id(j, "generation", engine_instance_id(s->engine));
    id(j, "revision", daw_ab_revision(s));
    const DawAbComparison* c = s->ab;
    uint64_t position = 0;
    bool playing = c && engine_comparison_status(s->engine, c->revision, &position);
    cJSON_AddBoolToObject(j, "ready", c && c->ready);
    cJSON_AddBoolToObject(j, "match_available", c && c->match_available);
    cJSON_AddBoolToObject(j, "playing", playing);
    id(j, "position_frames", position);
    cJSON_AddStringToObject(j, "selected_side", c && c->selection % 2 ? "B" : "A");
    cJSON_AddBoolToObject(j, "matched", !c || c->selection < 2);
    cJSON_AddStringToObject(
        j, "matching",
        "all-channel unweighted RMS over the equal passage including tail; attenuate louder to "
        "quieter, then shared sample headroom; not LUFS matching");
    cJSON_AddStringToObject(j, "playback",
                            "finite aligned immutable buffers; 5 ms convex side/mode crossfade on "
                            "shared cursor; switching never restarts unless restart=true");
    cJSON_AddNumberToObject(j, "sample_headroom", .95);
    cJSON_AddNumberToObject(j, "material_seconds_limit", 10);
    cJSON_AddNumberToObject(j, "tail_seconds_limit", 5);
    cJSON_AddNumberToObject(j, "preceding_context_seconds_limit", 60);
    cJSON* captures = cJSON_AddArrayToObject(j, "captures");
    for (int k = 0; k < 2; ++k) {
        if (!c || !c->captures[k].audio.data)
            continue;
        const DawCompositionRender* r = &c->captures[k];
        cJSON* item = cJSON_CreateObject();
        cJSON_AddItemToArray(captures, item);
        cJSON_AddStringToObject(item, "side", k ? "B" : "A");
        cJSON_AddBoolToObject(item, "dry", r->request.dry);
        id(item, "start_frame", r->request.start);
        id(item, "end_frame", r->request.end);
        id(item, "tail_frames", r->request.tail);
        id(item, "frames", r->audio.frame_count);
        cJSON_AddNumberToObject(item, "sample_rate", r->audio.sample_rate);
        cJSON_AddNumberToObject(item, "channels", r->audio.channels);
        cJSON_AddNumberToObject(item, "original_peak", r->analysis.peak);
        cJSON_AddNumberToObject(item, "original_rms", r->analysis.rms);
        cJSON_AddStringToObject(
            item, "scope",
            r->request.has_target
                ? r->request.target.kind == DAW_ACTION_TARGET_TRACK ? "track" : "midi_region"
                : "project");
        id(item, "track_id", r->request.has_target ? r->request.target.track_id : 0);
        id(item, "clip_id", r->request.has_target ? r->request.target.clip_id : 0);
        char fp[32];
        snprintf(fp, sizeof(fp), "%016" PRIx64, c->provenance[k]->settings_fingerprint);
        cJSON_AddStringToObject(item, "captured_configuration_fingerprint", fp);
        if (c->ready) {
            cJSON_AddNumberToObject(item, "matched_gain", c->gains[k]);
            cJSON_AddNumberToObject(item, "matched_gain_db", 20 * log10(c->gains[k]));
            cJSON_AddNumberToObject(item, "original_safety_gain", c->gains[k + 2]);
            cJSON_AddNumberToObject(item, "matched_rms", r->analysis.rms * c->gains[k]);
        }
    }
}
// Adopts receipt-guarded capture/listening operations while keeping authored change flags false.
cJSON* daw_control_ab_comparison(DawControlBridge* bridge, AppState* s, const cJSON* request) {
    const char* op = text(request, "op");
    if (!strcmp(op, "get_ab_comparison")) {
        cJSON* j = reply(true, "comparison");
        describe(j, s);
        return j;
    }
    char guard[80];
    token(s, guard);
    const char* expected = text(request, "expected_comparison");
    if (!expected || strcmp(expected, guard))
        return reply(false, "comparison_conflict");
    DawActionStatus status = DAW_ACTION_INVALID;
    if (!strcmp(op, "capture_ab")) {
        const char* side = text(request, "side");
        int index = side && !strcmp(side, "A") ? 0 : side && !strcmp(side, "B") ? 1 : -1;
        DawCompositionRequest r = {0};
        if (index < 0 || !frame(request, "start_frame", false, &r.start) ||
            !frame(request, "end_frame", false, &r.end) ||
            !frame(request, "tail_frames", true, &r.tail) ||
            !boolean(request, "dry", false, &r.dry))
            return reply(false, "invalid_request");
        const cJSON* target = cJSON_GetObjectItemCaseSensitive(request, "target");
        if (target) {
            if (!daw_control_parse_target(target, &r.target))
                return reply(false, "invalid_target");
            r.has_target = true;
        }
        const char* mix = text(request, "expected_mix");
        const DawMixSnapshot* snapshot = NULL;
        for (int i = 0; mix && i < DAW_CONTROL_MIX_STATES; ++i)
            if (!strcmp(mix, bridge->mix_guards[i].token))
                snapshot = bridge->mix_guards[i].snapshot;
        if (!snapshot)
            return reply(false, "state_conflict");
        status = daw_ab_capture(s, daw_ab_revision(s), snapshot, index, &r);
    } else if (!strcmp(op, "listen_ab")) {
        const char* side = text(request, "side");
        int index = side && !strcmp(side, "A") ? 0 : side && !strcmp(side, "B") ? 1 : -1;
        bool matched, restart;
        if (index < 0 || !boolean(request, "matched", true, &matched) ||
            !boolean(request, "restart", false, &restart))
            return reply(false, "invalid_request");
        status = daw_ab_listen(s, daw_ab_revision(s), index, matched, restart);
    } else if (!strcmp(op, "stop_ab")) {
        bool clear;
        if (!boolean(request, "clear", false, &clear))
            return reply(false, "invalid_request");
        status = daw_ab_stop(s, daw_ab_revision(s));
        if (clear && status <= DAW_ACTION_UNCHANGED) {
            daw_ab_release(s);
            status = DAW_ACTION_APPLIED;
        }
    }
    const char* codes[] = {
        "applied",        "unchanged", "invalid_request",      "wrong_thread",
        "state_conflict", "busy",      "capacity_unavailable", "output_unavailable"};
    cJSON* j = reply(status <= DAW_ACTION_UNCHANGED, codes[status]);
    if (status <= DAW_ACTION_UNCHANGED)
        describe(j, s);
    return j;
}
