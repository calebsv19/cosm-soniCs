#include "control_bridge_internal.h"
#include "app_state.h"
#include "daw/song_export.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <errno.h>
// Produces an export-domain envelope without claiming an authored edit.
static cJSON* reply(bool ok, const char* code) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", ok);
    cJSON_AddStringToObject(j, "code", code);
    return j;
}
// Reads case-sensitive typed strings without coercion.
static const char* text(const cJSON* j, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
// Encodes every frame/byte identity losslessly.
static void number(cJSON* j, const char* key, uint64_t v) {
    char s[32];
    snprintf(s, sizeof(s), "%" PRIu64, v);
    cJSON_AddStringToObject(j, key, s);
}
// Rejects signed, padded or overflowing tail-frame requests.
static bool frames(const char* p, uint64_t* out) {
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
// Separates provisional frame progress from verified published bytes and durability.
static void describe(cJSON* j, const DawSongExportInfo* i) {
    cJSON_AddStringToObject(j, "job_id", i->id);
    cJSON_AddStringToObject(j, "state", i->state);
    cJSON_AddStringToObject(j, "output_path", i->path);
    cJSON_AddStringToObject(j, "format",
                            i->format == ENGINE_BOUNCE_WAV_FLOAT32 ? "float32" : "pcm16");
    cJSON_AddBoolToObject(j, "normalize_if_clipping", i->normalize);
    number(j, "generation", i->generation);
    number(j, "authored_end_frame", i->end);
    number(j, "tail_frames", i->tail);
    number(j, "output_frames", i->end + i->tail);
    number(j, "completed_work_frames", i->done);
    number(j, "total_work_frames", i->total);
    cJSON_AddNumberToObject(j, "progress", i->total ? (double)i->done / i->total : 0);
    cJSON_AddNumberToObject(j, "sample_rate", i->rate);
    cJSON_AddNumberToObject(j, "channels", i->channels);
    cJSON_AddBoolToObject(j, "published", i->published);
    cJSON_AddBoolToObject(j, "verified", i->verified);
    cJSON_AddBoolToObject(j, "directory_synced", i->durable);
    number(j, "file_bytes", i->bytes);
    cJSON_AddStringToObject(j, "content_id", i->content_id);
    cJSON_AddStringToObject(j, "content_id_algorithm",
                            "soniCs media-registry FNV-1a64; noncryptographic");
    char fp[32];
    snprintf(fp, sizeof(fp), "%016" PRIx64, i->fingerprint);
    cJSON_AddStringToObject(j, "captured_configuration_fingerprint", fp);
    cJSON_AddStringToObject(j, "progress_semantics",
                            "render plus encode work including latency; 100 percent is provisional "
                            "until verified publication");
}
// Adopts ordinary guarded MCP export requests through the existing immutable receipt dispatcher.
struct cJSON* daw_control_song_export(DawControlBridge* bridge, AppState* state,
                                      const cJSON* request) {
    const char* op = text(request, "op");
    DawSongExportInfo info = {0};
    if (!strcmp(op, "get_song_export")) {
        const char* id = text(request, "job_id");
        if (cJSON_GetObjectItemCaseSensitive(request, "job_id") && (!id || !*id))
            return reply(false, "invalid_request");
        if (!daw_song_export_get(state, id, &info))
            return reply(!id, id ? "unknown_export" : "idle");
        cJSON* j = reply(true, "export_status");
        describe(j, &info);
        return j;
    }
    if (!strcmp(op, "cancel_song_export")) {
        const char* id = text(request, "job_id");
        if (!id || !*id)
            return reply(false, "invalid_request");
        const char* code = daw_song_export_cancel(state, id);
        cJSON* j = reply(strcmp(code, "unknown_export") != 0, code);
        if (daw_song_export_get(state, id, &info))
            describe(j, &info);
        return j;
    }
    const char *mix = text(request, "expected_mix"), *format = text(request, "format");
    const DawMixSnapshot* guard = NULL;
    for (int i = 0; mix && i < DAW_CONTROL_MIX_STATES; ++i)
        if (!strcmp(mix, bridge->mix_guards[i].token))
            guard = bridge->mix_guards[i].snapshot;
    if (!guard)
        return reply(false, "state_conflict");
    uint64_t tail;
    const cJSON* normalize = cJSON_GetObjectItemCaseSensitive(request, "normalize");
    if (!frames(text(request, "tail_frames"), &tail) || !format ||
        (strcmp(format, "pcm16") && strcmp(format, "float32")) ||
        (normalize && !cJSON_IsBool(normalize)))
        return reply(false, "invalid_request");
    DawActionStatus status = daw_song_export_start(
        state, guard,
        !strcmp(format, "float32") ? ENGINE_BOUNCE_WAV_FLOAT32 : ENGINE_BOUNCE_WAV_PCM16, tail,
        cJSON_IsTrue(normalize), &info);
    const char* codes[] = {"export_queued",        "unchanged",         "invalid_request",
                           "wrong_thread",         "state_conflict",    "busy",
                           "capacity_unavailable", "preparation_failed"};
    cJSON* j = reply(status <= DAW_ACTION_UNCHANGED, codes[status]);
    if (status <= DAW_ACTION_UNCHANGED)
        describe(j, &info);
    return j;
}
