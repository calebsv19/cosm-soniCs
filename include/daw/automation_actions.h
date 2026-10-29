#pragma once
#include "engine/automation.h"
#include <stdbool.h>
struct AppState;
// Publishes complete region lanes only after both sides of undo history are prepared.
bool daw_automation_replace_lanes(struct AppState* state, int track, int clip, const EngineAutomationLane* lanes, int count);
// Inserts or replaces a point through the shared history command.
bool daw_automation_add_point(struct AppState* state, int track, int clip, EngineAutomationTarget target, uint64_t frame, float value, int* index);
// Moves a point and preserves its outgoing curve, rejecting collisions without changing the project.
bool daw_automation_update_point(struct AppState* state, int track, int clip, EngineAutomationTarget target, int point, uint64_t frame, float value, int* index);
// Removes a selected point with the same history semantics as graphical and MCP edits.
bool daw_automation_remove_point(struct AppState* state, int track, int clip, EngineAutomationTarget target, int point);

// Changes a point outgoing curve through complete pre-reserved lane history.
bool daw_automation_set_curve(struct AppState* state, int track, int clip, EngineAutomationTarget target, int point, EngineAutomationCurve curve);
// Changes one region boundary policy through the same complete lane command.
bool daw_automation_set_boundary(struct AppState* state, int track, int clip, EngineAutomationTarget target, bool before, EngineAutomationBoundary boundary);
