#include "session_automation.h"
#include <stdlib.h>

// Adapts serialized lane metadata without taking ownership of its point storage.
static EngineAutomationLane adapt(const SessionAutomationLane* lane) {
    return (EngineAutomationLane){.target = lane->target, .points = lane->points,
        .point_count = lane->point_count, .point_capacity = lane->point_count,
        .domain = lane->domain, .before = lane->before, .after = lane->after};
}

// Validates serialized values before a session can publish them to playback.
bool session_automation_lane_validate(const SessionAutomationLane* lane, uint64_t frames) {
    if (!lane) return false;
    EngineAutomationLane candidate = adapt(lane);
    return engine_automation_lane_validate(&candidate, frames);
}

// Adapts all lanes before invoking a single complete engine replacement.
static bool apply(Engine* engine, int track, int clip, const SessionAutomationLane* lanes, int count) {
    if (count < 0 || count > ENGINE_AUTOMATION_TARGET_COUNT || (count && !lanes)) return false;
    EngineAutomationLane* prepared = count ? calloc((size_t)count, sizeof(*prepared)) : NULL;
    if (count && !prepared) return false;
    for (int i = 0; i < count; ++i) prepared[i] = adapt(&lanes[i]);
    bool ok = clip < 0 ? engine_track_midi_set_instrument_automation_lanes(engine, track, prepared, count)
                       : engine_clip_set_automation_lanes(engine, track, clip, prepared, count);
    free(prepared);
    return ok;
}

// Restores region automation without dropping curve or endpoint metadata.
bool session_apply_clip_automation(Engine* engine, int track, int clip, const SessionAutomationLane* lanes, int count) {
    return clip >= 0 && apply(engine, track, clip, lanes, count);
}

// Restores track instrument automation through the same complete metadata adapter.
bool session_apply_track_automation(Engine* engine, int track, const SessionAutomationLane* lanes, int count) {
    return apply(engine, track, -1, lanes, count);
}
