#pragma once
#include "engine/automation.h"
#define ENGINE_MIX_AUTOMATION_POINTS 32
#define ENGINE_MIX_AUTOMATION_TOTAL 256
// Owns bounded project-time physical gain points independently of clips and mutable render state.
typedef struct EngineMixerAutomation {
    int count;
    EngineAutomationBoundary before, after;
    EngineAutomationPoint points[ENGINE_MIX_AUTOMATION_POINTS];
} EngineMixerAutomation;
// Rejects unsorted frames, nonfinite/out-of-range levels and unsupported curves or boundaries.
// Validates physical parameter curves with a finite signed range.
bool engine_mixer_automation_valid_range(const EngineMixerAutomation*, float minimum, float maximum);
bool engine_mixer_automation_valid(const EngineMixerAutomation*, float maximum);
// Compares only authored fields, ignoring unused capacity and struct padding.
bool engine_mixer_automation_equal(const EngineMixerAutomation*, const EngineMixerAutomation*);
// Evaluates immutable linear gains at the node's latency-corrected project frame with no
// allocation.
float engine_mixer_automation_eval(const EngineMixerAutomation*, uint64_t, float baseline);
