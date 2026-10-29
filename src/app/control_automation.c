#include "control_bridge_internal.h"
#include "app_state.h"
#include "daw/automation_actions.h"
#include "engine/engine_internal.h"
#include "cJSON.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUTOMATION_PAGE 128
// Returns a typed domain outcome independent of transport success.
static cJSON* reply(bool ok, const char* code) {
    cJSON* out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", ok);
    cJSON_AddStringToObject(out, "code", code);
    return out;
}
// Reads wire strings without implicit type conversion.
static const char* text(const cJSON* object, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}
// Rejects repeated or unknown keys in nested request objects.
static bool fields(const cJSON* object, const char* const* allowed) {
    if (!cJSON_IsObject(object)) return false;
    for (const cJSON* item = object->child; item; item = item->next) {
        bool known = false;
        for (int i = 0; allowed[i]; ++i) if (!strcmp(item->string, allowed[i])) known = true;
        if (!known) return false;
        for (const cJSON* other = item->next; other; other = other->next)
            if (!strcmp(item->string, other->string)) return false;
    }
    return true;
}
// Parses canonical unsigned frame and identity strings without losing high bits.
static bool integer_string(const cJSON* object, const char* key, uint64_t* out) {
    const char* value = text(object, key);
    if (!value || !*value || strlen(value) > 20 || (value[0] == '0' && value[1])) return false;
    for (const char* p = value; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0;
    char* end;
    unsigned long long number = strtoull(value, &end, 10);
    if (errno || *end) return false;
    *out = number;
    return true;
}
// Reads a bounded integral JSON number without accepting booleans or fractional indices.
static bool number(const cJSON* object, const char* key, int max, int* out) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || value->valuedouble < 0 ||
        value->valuedouble > max || floor(value->valuedouble) != value->valuedouble) return false;
    *out = value->valueint;
    return true;
}
// Serializes frame and identity values as canonical decimal strings.
static void id(cJSON* out, const char* key, uint64_t value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    cJSON_AddStringToObject(out, key, buffer);
}
// Resolves either audio or MIDI regions by generation and stable identities rather than indices.
static bool resolve(AppState* state, const cJSON* object, int* track, int* clip) {
    static const char* const keys[] = {"generation", "track_id", "clip_id", "kind", NULL};
    uint64_t generation, track_id, clip_id;
    const char* kind = text(object, "kind");
    if (!state || !state->engine || SDL_ThreadID() != state->engine->control_thread_id ||
        !fields(object, keys) || !kind || strcmp(kind, "region") ||
        !integer_string(object, "generation", &generation) || generation != engine_instance_id(state->engine) ||
        !integer_string(object, "track_id", &track_id) || !integer_string(object, "clip_id", &clip_id)) return false;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    for (int t = 0; tracks && t < engine_get_track_count(state->engine); ++t)
        if (tracks[t].runtime_id == track_id)
            for (int c = 0; c < tracks[t].clip_count; ++c)
                if (tracks[t].clips[c].creation_index == clip_id) { *track = t; *clip = c; return true; }
    return false;
}
// Requires a complete retained arrangement precondition, including curves, domains and boundaries.
static bool expected(DawControlBridge* bridge, AppState* state, const cJSON* request) {
    const char* token = text(request, "expected_arrangement");
    for (int i = 0; token && i < DAW_CONTROL_ARRANGEMENT_STATES; ++i)
        if (bridge->arrangement_states[i].snapshot && !strcmp(token, bridge->arrangement_states[i].token))
            return daw_arrangement_snapshot_current(state, bridge->arrangement_states[i].snapshot);
    return false;
}
// Encodes an exact paginated lane plus a fresh reusable full-state guard.
static cJSON* read_lane(DawControlBridge* bridge, AppState* state, int track, int clip,
                        int target, int offset, const char* code) {
    const EngineClip* region = &engine_get_tracks(state->engine)[track].clips[clip];
    const EngineAutomationLane* lane = NULL;
    for (int i = 0; i < region->automation_lane_count; ++i)
        if (region->automation_lanes[i].target == target) lane = &region->automation_lanes[i];
    int count = lane ? lane->point_count : 0;
    if (offset > count) return reply(false, "invalid_offset");
    DawControlArrangementState* guard = daw_control_arrangement_capture(bridge, state);
    if (!guard) return reply(false, "capture_unavailable");
    cJSON* out = reply(true, code);
    cJSON_AddStringToObject(out, "expected_arrangement", guard->token);
    id(out, "duration_frames", region->duration_frames);
    cJSON_AddStringToObject(out, "scope", "region-local sample frames; stereo balance attenuates the opposite channel");
    cJSON_AddNumberToObject(out, "sample_rate", engine_get_config(state->engine)->sample_rate);
    cJSON_AddNumberToObject(out, "lane_target", target);
    cJSON_AddStringToObject(out, "label", engine_automation_target_display_label(target));
    cJSON_AddBoolToObject(out, "exists", lane != NULL);
    cJSON_AddStringToObject(out, "domain", lane && lane->domain == ENGINE_AUTOMATION_DOMAIN_PHYSICAL ? "physical" : "offset");
    cJSON_AddStringToObject(out, "unit", lane ? engine_automation_lane_unit(lane) : "offset");
    cJSON_AddStringToObject(out, "before", lane && lane->before == ENGINE_AUTOMATION_BOUNDARY_HOLD ? "hold" : "baseline");
    cJSON_AddStringToObject(out, "after", lane && lane->after == ENGINE_AUTOMATION_BOUNDARY_HOLD ? "hold" : "baseline");
    cJSON_AddNumberToObject(out, "point_count", count);
    cJSON_AddNumberToObject(out, "lane_count", region->automation_lane_count);
    cJSON* targets = cJSON_AddArrayToObject(out, "available_targets");
    for (int i = 0; i < ENGINE_AUTOMATION_TARGET_COUNT; ++i) {
        if (region->kind == ENGINE_CLIP_KIND_AUDIO && engine_automation_target_is_instrument_param(i)) continue;
        if (engine_automation_target_is_instrument_param(i)) {
            EngineInstrumentParamId param;
            EngineInstrumentPresetId preset = engine_clip_midi_effective_instrument_preset(state->engine,track,clip);
            if (!engine_instrument_param_from_automation_target(i,&param) ||
                !engine_instrument_preset_param_active(preset,param)) continue;
        }
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "lane_target", i);
        cJSON_AddStringToObject(item, "label", engine_automation_target_display_label(i));
        cJSON_AddBoolToObject(item, "physical_units", i <= ENGINE_AUTOMATION_TARGET_PAN);
        cJSON_AddItemToArray(targets, item);
    }
    cJSON* points = cJSON_AddArrayToObject(out, "points");
    int end = offset + AUTOMATION_PAGE;
    if (end > count) end = count;
    static const char* const curves[] = {"linear", "hold", "smooth"};
    for (int i = offset; i < end; ++i) {
        cJSON* point = cJSON_CreateObject();
        id(point, "frame", lane->points[i].frame);
        cJSON_AddNumberToObject(point, "value", lane->points[i].value);
        cJSON_AddStringToObject(point, "curve", curves[lane->points[i].curve]);
        cJSON_AddItemToArray(points, point);
    }
    cJSON_AddNumberToObject(out, "next_offset", end < count ? end : -1);
    return out;
}
// Parses explicit semantics and a bounded complete lane without partially changing live data.
static bool parse_lane(const cJSON* request, int target, EngineAutomationLane* lane) {
    const char* domain = text(request, "domain");
    const char* before = text(request, "before");
    const char* after = text(request, "after");
    if (!domain || !before || !after) return false;
    lane->target = target;
    if (!strcmp(domain, "physical")) lane->domain = ENGINE_AUTOMATION_DOMAIN_PHYSICAL;
    else if (strcmp(domain, "offset")) return false;
    if (!strcmp(before, "hold")) lane->before = ENGINE_AUTOMATION_BOUNDARY_HOLD;
    else if (strcmp(before, "baseline")) return false;
    if (!strcmp(after, "hold")) lane->after = ENGINE_AUTOMATION_BOUNDARY_HOLD;
    else if (strcmp(after, "baseline")) return false;
    const cJSON* points = cJSON_GetObjectItemCaseSensitive(request, "points");
    if (!cJSON_IsArray(points) || cJSON_GetArraySize(points) > AUTOMATION_PAGE) return false;
    lane->point_count = lane->point_capacity = cJSON_GetArraySize(points);
    lane->points = lane->point_count ? calloc((size_t)lane->point_count, sizeof(*lane->points)) : NULL;
    if (lane->point_count && !lane->points) return false;
    static const char* const keys[] = {"frame", "value", "curve", NULL};
    int i = 0;
    for (const cJSON* point = points->child; point; point = point->next, ++i) {
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(point, "value");
        const char* curve = text(point, "curve");
        if (!fields(point, keys) || !integer_string(point, "frame", &lane->points[i].frame) ||
            !cJSON_IsNumber(value) || !isfinite(value->valuedouble) || !curve) return false;
        lane->points[i].value = (float)value->valuedouble;
        if (!strcmp(curve, "hold")) lane->points[i].curve = ENGINE_AUTOMATION_CURVE_HOLD;
        else if (!strcmp(curve, "smooth")) lane->points[i].curve = ENGINE_AUTOMATION_CURVE_SMOOTH;
        else if (strcmp(curve, "linear")) return false;
    }
    return true;
}
// Routes reads and preconditioned edits through the same region command used by the graphical editor.
cJSON* daw_control_automation(DawControlBridge* bridge, AppState* state, const cJSON* request, bool* changed) {
    const char* op = text(request, "op");
    bool read = !strcmp(op, "get_automation");
    int track, clip, target, offset = 0;
    if (!resolve(state, cJSON_GetObjectItemCaseSensitive(request, "target"), &track, &clip)) return reply(false, "stale_target");
    if (!number(request, "lane_target", ENGINE_AUTOMATION_TARGET_COUNT - 1, &target) ||
        (read && cJSON_GetObjectItemCaseSensitive(request, "offset") && !number(request, "offset", ENGINE_AUTOMATION_POINT_LIMIT, &offset)))
        return reply(false, "invalid_request");
    if ((!read || cJSON_GetObjectItemCaseSensitive(request, "expected_arrangement")) && !expected(bridge, state, request))
        return reply(false, "state_conflict");
    if (read) return read_lane(bridge, state, track, clip, target, offset, "automation");
    if (daw_midi_actions_busy(state)) return reply(false, "busy");
    bool accepted = false;
    if (!strcmp(op, "set_automation")) {
        EngineAutomationLane lane = {0};
        bool valid = parse_lane(request, target, &lane);
        const EngineClip* region = &engine_get_tracks(state->engine)[track].clips[clip];
        if (!valid || !engine_automation_lane_validate(&lane, region->duration_frames)) {
            engine_automation_lane_free(&lane);
            return reply(false, "invalid_lane");
        }
        int count = region->automation_lane_count;
        EngineAutomationLane* lanes = calloc((size_t)count + 1, sizeof(*lanes));
        if (!lanes) { engine_automation_lane_free(&lane); return reply(false, "allocation_failed"); }
        int index = -1;
        for (int l = 0; l < count; ++l) { lanes[l] = region->automation_lanes[l]; if (lanes[l].target == target) index = l; }
        if (index < 0) index = count++;
        lanes[index] = lane;
        accepted = daw_automation_replace_lanes(state, track, clip, lanes, count);
        free(lanes);
        engine_automation_lane_free(&lane);
    } else {
        bool redo = !strcmp(op, "redo_automation");
        int count = redo ? state->undo.redo_count : state->undo.undo_count;
        UndoCommand* stack = redo ? state->undo.redo_stack : state->undo.undo_stack;
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        if (!count || stack[count - 1].type != UNDO_CMD_AUTOMATION_EDIT ||
            stack[count - 1].data.automation_edit.track_runtime_id != tracks[track].runtime_id ||
            stack[count - 1].data.automation_edit.clip_creation_id != tracks[track].clips[clip].creation_index)
            return reply(false, "history_conflict");
        accepted = redo ? undo_manager_redo(&state->undo, state) : undo_manager_undo(&state->undo, state);
    }
    if (!accepted) return reply(false, "edit_rejected");
    *changed = true;
    cJSON* out = read_lane(bridge, state, track, clip, target, 0, "applied");
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(out, "ok"))) {
        cJSON_Delete(out);
        out = reply(true, "applied");
        cJSON_AddBoolToObject(out, "readback_required", true);
    }
    return out;
}
