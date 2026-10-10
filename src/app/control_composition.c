#include "control_bridge_internal.h"
#include "app_state.h"
#include "engine/analysis_math.h"
#include "cJSON.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Separates domain acceptance from successful MCP message transport.
static cJSON* reply(bool ok, const char* code) {
    cJSON* r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", ok); cJSON_AddStringToObject(r, "code", code); return r;
}
// Reads a typed string without coercing numbers or nulls.
static const char* text(const cJSON* request, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(request, key); return cJSON_IsString(v) ? v->valuestring : NULL;
}
// Preserves all frame and generation bits in wire values.
static void id(cJSON* r, const char* key, uint64_t value) {
    char value_text[32]; snprintf(value_text, sizeof(value_text), "%" PRIu64, value); cJSON_AddStringToObject(r, key, value_text);
}
// Refuses noncanonical or overflowing unsigned frame strings.
static bool frame(const cJSON* request, const char* key, bool optional, uint64_t* out) {
    const char* value = text(request, key); *out = 0;
    if (!cJSON_GetObjectItemCaseSensitive(request, key)) return optional;
    if (!value || !*value || strlen(value) > 20 || (value[0] == '0' && value[1])) return false;
    for (const char* p = value; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0; char* end; unsigned long long parsed = strtoull(value, &end, 10);
    if (errno || *end) return false; *out = parsed; return true;
}
// Serializes bounded floating plot data without allocating sample-sized wire output.
static void array(cJSON* r, const char* name, const float* values, int count) {
    cJSON* a = cJSON_AddArrayToObject(r, name);
    for (int i = 0; i < count; ++i) cJSON_AddItemToArray(a, cJSON_CreateNumber(values[i]));
}
// Reclaims one immutable cached render only after its endpoint consumer has finished.
static void release(DawControlBridge* bridge, DawControlCompositionState* state) {
    if (state->render) { bridge->composition_bytes -= state->bytes; daw_composition_render_free(state->render); free(state->render); }
    memset(state, 0, sizeof(*state));
}
// Retains the accepted audio itself, evicting old entries to enforce count and byte bounds.
static DawControlCompositionState* remember(DawControlBridge* bridge, DawCompositionRender* render) {
    uint64_t bytes = render->audio.frame_count * (uint64_t)render->audio.channels * sizeof(float);
    if (bytes > DAW_CONTROL_COMPOSITION_BYTES || bridge->composition_sequence == UINT64_MAX) return NULL;
    DawCompositionRender* owned = malloc(sizeof(*owned)); if (!owned) return NULL;
    *owned = *render; memset(render, 0, sizeof(*render));
    DawControlCompositionState* entry = &bridge->composition_states[bridge->composition_cursor++ % DAW_CONTROL_COMPOSITION_STATES];
    release(bridge, entry);
    while (bridge->composition_bytes + bytes > DAW_CONTROL_COMPOSITION_BYTES) {
        DawControlCompositionState* oldest = NULL;
        for (int i = 0; i < DAW_CONTROL_COMPOSITION_STATES; ++i) {
            DawControlCompositionState* candidate = &bridge->composition_states[i];
            if (candidate->render && (!oldest || candidate->sequence < oldest->sequence)) oldest = candidate;
        }
        if (!oldest) { daw_composition_render_free(owned); free(owned); return NULL; }
        release(bridge, oldest);
    }
    entry->render = owned; entry->bytes = bytes; bridge->composition_bytes += bytes;
    entry->sequence = ++bridge->composition_sequence;
    snprintf(entry->token, sizeof(entry->token), "%s:composition:%" PRIu64, bridge->instance, entry->sequence); return entry;
}
// Reports the exact captured stimulus, original-level metrics and calibrated plot semantics.
static void describe(cJSON* r, const DawControlCompositionState* entry) {
    const DawCompositionRender* render = entry->render; const DawCompositionAnalysis* a = &render->analysis;
    cJSON_AddStringToObject(r, "render_id", entry->token); id(r, "generation", render->generation);
    cJSON_AddStringToObject(r, "scope", render->request.has_target ? render->request.target.kind == DAW_ACTION_TARGET_TRACK ? "track" : "midi_region" : "project");
    cJSON_AddBoolToObject(r, "dry", render->request.dry); cJSON_AddBoolToObject(r, "normalized", false);
    id(r, "start_frame", render->request.start); id(r, "end_frame", render->request.end); id(r, "tail_frames", render->request.tail);
    id(r, "frames", render->audio.frame_count); cJSON_AddNumberToObject(r, "sample_rate", render->audio.sample_rate); cJSON_AddNumberToObject(r, "channels", render->audio.channels);
    cJSON_AddStringToObject(r, "capture_semantics", "immutable production audio captured when analyzed; publication does not re-render current project");
    if (render->request.has_target) {
        cJSON* target = cJSON_AddObjectToObject(r, "target"); id(target, "generation", render->request.target.project_generation);
        id(target, "track_id", render->request.target.track_id); id(target, "clip_id", render->request.target.clip_id);
        cJSON_AddStringToObject(target, "kind", render->request.target.kind == DAW_ACTION_TARGET_TRACK ? "track" : "midi_region");
    }
    cJSON* processing = cJSON_AddObjectToObject(r, "captured_inserts");
    cJSON_AddBoolToObject(processing, "bypassed_for_dry", render->request.dry);
    daw_control_fx_values_json(cJSON_AddObjectToObject(processing, "master"), &render->master_fx.chain);
    if (render->request.has_target) daw_control_fx_values_json(cJSON_AddObjectToObject(processing, "track"), &render->track_fx.chain);
    cJSON* metrics = cJSON_AddObjectToObject(r, "metrics"); cJSON_AddNumberToObject(metrics, "peak", a->peak); cJSON_AddNumberToObject(metrics, "rms", a->rms);
    cJSON_AddNumberToObject(metrics, "dc", a->dc); id(metrics, "full_scale_samples", a->clipped_samples);
    cJSON* plots = cJSON_AddObjectToObject(r, "plots"); id(plots, "window_frame", a->window_frame);
    cJSON_AddNumberToObject(plots, "spectrum_window_frames", ENGINE_ANALYSIS_MAX_FRAMES);
    cJSON_AddStringToObject(plots, "spectrum_calibration", "Hann coherent-gain tonal peak amplitude dBFS; 2048-frame mean-of-channels window; zero-padded at range end");
    cJSON_AddStringToObject(plots, "waveform_semantics", "256 sampled mean-of-channels points over 20ms; no normalization");
    cJSON_AddStringToObject(plots, "envelope_semantics", "256 original-level all-channel RMS buckets over entire returned range including tail");
    array(plots, "waveform", a->waveform, DAW_COMPOSITION_POINTS); array(plots, "envelope_rms", a->envelope_rms, DAW_COMPOSITION_POINTS);
    array(plots, "frequency_hz", a->frequency_hz, DAW_COMPOSITION_BINS); array(plots, "spectrum_db", a->spectrum_db, DAW_COMPOSITION_BINS);
}
// Publishes only the addressed captured render below a private library directory with durability readback.
static cJSON* preview(DawControlBridge* bridge, const cJSON* request) {
    const char* token = text(request, "render_id"); DawControlCompositionState* entry = NULL;
    for (int i = 0; token && i < DAW_CONTROL_COMPOSITION_STATES; ++i)
        if (bridge->composition_states[i].render && !strcmp(token, bridge->composition_states[i].token)) entry = &bridge->composition_states[i];
    if (!entry) return reply(false, "expired_render");
    char artifact[96];
    snprintf(artifact, sizeof(artifact), "%s-%" PRIu64 "-%d", bridge->instance, entry->sequence, bridge->receipt_count);
    DawCompositionPreview published;
    DawCompositionPublishStatus status = daw_composition_publish(entry->render, artifact, &published);
    static const char* codes[] = {"preview_published", "invalid_request", "clipping_requires_gain_edit", "io_error", "destination_occupied"};
    if (status != DAW_COMPOSITION_PUBLISH_OK) return reply(false, codes[status]);
    cJSON* r = reply(true, published.durable ? "preview_published" : "preview_published_sync_uncertain");
    cJSON_AddStringToObject(r, "path", published.path); cJSON_AddBoolToObject(r, "durable", published.durable); describe(r, entry); return r;
}
// Captures read-only production analysis or publishes cached audio after common receipt admission.
cJSON* daw_control_composition(DawControlBridge* bridge, AppState* state, const cJSON* request, const DawActionTarget* target) {
    const char* op = text(request, "op");
    if (!strcmp(op, "preview_composition")) return preview(bridge, request);
    DawCompositionRequest specification = {.has_target = target != NULL};
    if (target) specification.target = *target;
    if (!frame(request, "start_frame", false, &specification.start) || !frame(request, "end_frame", false, &specification.end) ||
        !frame(request, "tail_frames", true, &specification.tail) || !frame(request, "window_frame", true, &specification.window)) return reply(false, "invalid_request");
    const cJSON* dry = cJSON_GetObjectItemCaseSensitive(request, "dry");
    if (dry && !cJSON_IsBool(dry)) return reply(false, "invalid_request"); specification.dry = cJSON_IsTrue(dry);
    DawCompositionRender render = {0}; DawActionStatus status = daw_action_render_composition(state, &specification, &render);
    if (status != DAW_ACTION_UNCHANGED) {
        static const char* codes[] = {"applied", "unchanged", "invalid_request", "wrong_thread", "stale_target", "busy", "history_unavailable", "engine_rejected"};
        return reply(false, codes[status]);
    }
    DawControlCompositionState* entry = remember(bridge, &render); daw_composition_render_free(&render);
    cJSON* r = reply(entry != NULL, entry ? "composition_analysis" : "allocation_failed"); if (entry) describe(r, entry); return r;
}
