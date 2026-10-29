#pragma once
#include "session.h"
#include "engine/engine.h"

// Applies complete serialized region lanes, including units, endpoints and curves, in one engine publication.
bool session_apply_clip_automation(Engine* engine, int track, int clip, const SessionAutomationLane* lanes, int count);
// Applies complete serialized track-instrument lanes while preserving their declared semantics.
bool session_apply_track_automation(Engine* engine, int track, const SessionAutomationLane* lanes, int count);
// Checks a serialized lane against its region duration using the engine's semantic contract.
bool session_automation_lane_validate(const SessionAutomationLane* lane, uint64_t frames);
