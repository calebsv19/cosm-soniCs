#include "app_state.h"
#include "daw/live_meters.h"
#include "engine/engine_internal.h"
#include "core_time.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
// Reads only coherent published windows and stable control-side endpoint identity.
DawActionStatus daw_live_meters_get(AppState* s, const DawActionTarget* target,
                                    DawLiveMeterInfo* out) {
    if (!s || !s->engine || !out)
        return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(s->engine))
        return DAW_ACTION_WRONG_THREAD;
    *out = (DawLiveMeterInfo){.generation = engine_instance_id(s->engine)};
    int track = -1, clip = -1;
    if (target) {
        if (target->kind != DAW_ACTION_TARGET_TRACK)
            return DAW_ACTION_INVALID;
        DawActionStatus status = daw_action_target_resolve(s, *target, &track, &clip);
        if (status != DAW_ACTION_UNCHANGED)
            return status;
        out->track_id = target->track_id;
    }
    EngineMeterSnapshot snapshot = {0};
    bool got = track < 0 ? engine_get_master_meter_snapshot(s->engine, &snapshot)
                         : engine_get_track_meter_snapshot(s->engine, track, &snapshot);
    if (got)
        out->meter = snapshot.live;
    uint64_t now = core_time_now_ns();
    out->age_ns = out->meter.published_ns && now >= out->meter.published_ns
                      ? now - out->meter.published_ns
                      : UINT64_MAX;
    out->fresh = out->meter.available && out->meter.valid && out->age_ns <= UINT64_C(1000000000);
    out->playing = engine_transport_requested_playing(s->engine);
    AudioDevice* d = &s->engine->device;
    out->output_available = d->is_open && d->spec.channels == 2;
    out->mono = d->is_open && atomic_load(&d->monitor_mono);
    snprintf(out->expected_monitor, sizeof(out->expected_monitor),
             "%" PRIu64 ":monitor:%" PRIu64 ":%u:%d", out->generation, s->monitor_revision,
             d->is_open ? d->device_id : 0, out->mono);
    return DAW_ACTION_UNCHANGED;
}
// Delivers an atomic mode intent without altering captures, exported PCM, project data or history.
DawActionStatus daw_monitor_set(AppState* s, const char* expected, bool mono) {
    DawLiveMeterInfo i;
    DawActionStatus status = daw_live_meters_get(s, NULL, &i);
    if (status != DAW_ACTION_UNCHANGED)
        return status;
    if (!expected || strcmp(expected, i.expected_monitor))
        return DAW_ACTION_STALE_TARGET;
    if (!i.output_available)
        return DAW_ACTION_ENGINE_REJECTED;
    if (mono == i.mono)
        return DAW_ACTION_UNCHANGED;
    if (s->monitor_revision == UINT64_MAX)
        return DAW_ACTION_HISTORY_UNAVAILABLE;
    atomic_store_explicit(&s->engine->device.monitor_mono, mono, memory_order_release);
    ++s->monitor_revision;
    return DAW_ACTION_APPLIED;
}
