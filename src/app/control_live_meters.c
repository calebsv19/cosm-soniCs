#include "control_bridge_internal.h"
#include "app_state.h"
#include "daw/live_meters.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
// Reports explicit meter/monitor admission without claiming an authored change.
static cJSON* reply(bool ok, const char* code) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", ok);
    cJSON_AddStringToObject(j, "code", code);
    return j;
}
// Reads a typed string without coercion.
static const char* text(const cJSON* j, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
// Retains exact frame/lifetime identities on the wire.
static void id(cJSON* j, const char* key, uint64_t v) {
    char s[32];
    snprintf(s, sizeof(s), "%" PRIu64, v);
    cJSON_AddStringToObject(j, key, s);
}
// Uses null for unavailable/silent dB values instead of inventing finite levels.
static void db(cJSON* j, const char* key, double amplitude, bool available) {
    if (available && amplitude > 0)
        cJSON_AddNumberToObject(j, key, 20 * log10(amplitude));
    else
        cJSON_AddNullToObject(j, key);
}
// Presents calibrated windows separately from freshness, output monitoring and transport state.
static void describe(cJSON* j, const DawLiveMeterInfo* i) {
    const EngineLiveMeter* m = &i->meter;
    id(j, "generation", i->generation);
    id(j, "track_id", i->track_id);
    cJSON_AddBoolToObject(j, "available", m->available);
    cJSON_AddBoolToObject(j, "valid", m->valid);
    cJSON_AddBoolToObject(j, "fresh", i->fresh);
    cJSON_AddBoolToObject(j, "playing", i->playing);
    cJSON_AddBoolToObject(j, "output_available", i->output_available);
    cJSON_AddStringToObject(j, "expected_monitor", i->expected_monitor);
    cJSON_AddStringToObject(j, "monitor_mode", i->mono ? "mono" : "stereo");
    if (m->available)
        cJSON_AddNumberToObject(j, "age_ms", i->age_ns / 1e6);
    else
        cJSON_AddNullToObject(j, "age_ms");
    cJSON_AddStringToObject(
        j, "tap",
        "live worker node post-insert/EQ/fader/pan; master sanitized production output, before "
        "listening-only fold; excludes captured preview PCM");
    cJSON_AddStringToObject(j, "window",
                            "disjoint exact 400 ms windows; seek/rate/channel/latency discontinuities warm "
                            "up; compatible prepared-graph handoffs preserve the stream");
    id(j, "window_start_frame", m->start);
    id(j, "window_end_frame", m->end);
    id(j, "latency_frames", m->latency);
    id(j, "sequence", m->sequence);
    cJSON_AddNumberToObject(j, "sample_rate", m->rate);
    cJSON_AddBoolToObject(j, "correlation_available", m->correlation_available);
    if (m->correlation_available)
        cJSON_AddNumberToObject(j, "correlation", m->correlation);
    else
        cJSON_AddNullToObject(j, "correlation");
    cJSON_AddStringToObject(
        j, "correlation_semantics",
        "uncentered sum(LR)/sqrt(sum(L2)sum(R2)); undefined for silent channel or mono input");
    cJSON_AddBoolToObject(j, "mono_available", m->mono_available);
    cJSON_AddBoolToObject(j, "full_mono_cancellation", m->mono_available && m->mono_ratio == 0);
    db(j, "mono_loss_db", m->mono_ratio, m->mono_available);
    cJSON_AddBoolToObject(j, "balance_available",
                          m->valid && m->channels == 2 && m->mono_available);
    if (m->valid && m->channels == 2 && m->mono_available)
        cJSON_AddNumberToObject(j, "balance", m->balance);
    else
        cJSON_AddNullToObject(j, "balance");
    cJSON_AddNumberToObject(j, "mid_rms", m->mid_rms);
    cJSON_AddNumberToObject(j, "side_rms", m->side_rms);
    cJSON_AddStringToObject(
        j, "mono_semantics",
        "M=(L+R)/2; loss compares M RMS with sqrt((L power+R power)/2), no boost; null loss plus "
        "full_mono_cancellation distinguishes full cancellation from silence");
    cJSON* channels = cJSON_AddArrayToObject(j, "channels");
    for (int ch = 0; ch < m->channels; ++ch) {
        cJSON* c = cJSON_CreateObject();
        cJSON_AddItemToArray(channels, c);
        cJSON_AddNumberToObject(c, "index", ch);
        cJSON_AddNumberToObject(c, "peak", m->peak[ch]);
        cJSON_AddNumberToObject(c, "rms", m->rms[ch]);
        db(c, "peak_dbfs", m->peak[ch], m->valid);
        db(c, "rms_dbfs", m->rms[ch], m->valid);
        id(c, "full_scale_samples", m->full_scale[ch]);
        id(c, "overload_samples", m->overload[ch]);
    }
    cJSON_AddStringToObject(j, "limits",
                            "sample peak/unweighted RMS and stereo compatibility; not live "
                            "LUFS/true peak or physical-device loudness");
}
// Adopts guarded output-only monitoring while keeping ordinary reads available on read-only
// endpoints.
struct cJSON* daw_control_live_meters(DawControlBridge* bridge, AppState* s, const cJSON* request) {
    (void)bridge;
    const char* op = text(request, "op");
    DawActionTarget target, *p = NULL;
    const cJSON* raw = cJSON_GetObjectItemCaseSensitive(request, "target");
    if (raw) {
        if (!daw_control_parse_target(raw, &target))
            return reply(false, "invalid_target");
        p = &target;
    }
    DawActionStatus status = DAW_ACTION_UNCHANGED;
    if (!strcmp(op, "set_monitor_mode")) {
        const char* mode = text(request, "mode");
        if (!mode || (strcmp(mode, "mono") && strcmp(mode, "stereo")))
            return reply(false, "invalid_request");
        status = daw_monitor_set(s, text(request, "expected_monitor"), !strcmp(mode, "mono"));
    }
    DawLiveMeterInfo info;
    if (status <= DAW_ACTION_UNCHANGED) {
        DawActionStatus got = daw_live_meters_get(s, p, &info);
        if (got != DAW_ACTION_UNCHANGED)
            status = got;
    }
    const char* codes[] = {"applied",
                           "live_meters",
                           "invalid_request",
                           "wrong_thread",
                           !strcmp(op, "set_monitor_mode") ? "monitor_conflict" : "stale_target",
                           "busy",
                           "capacity_unavailable",
                           "output_unavailable"};
    cJSON* j = reply(status <= DAW_ACTION_UNCHANGED, codes[status]);
    if (status <= DAW_ACTION_UNCHANGED)
        describe(j, &info);
    return j;
}
