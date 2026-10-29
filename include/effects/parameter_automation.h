#pragma once
#include "engine/mixer_automation.h"
#include "effects/effects_api.h"
#define FX_AUTOMATION_TOTAL_POINTS 256
// Reports the qualified physical range for Gain dB and constant-power Pan.
bool fx_parameter_automation_range(uint32_t type, uint32_t parameter, float* minimum,
                                   float* maximum);
// Validates one exact complete lane for its bound processor.
bool fx_parameter_automation_valid(uint32_t type, const EngineMixerAutomation*);
// Applies exact per-sample targets on a prepared handle with no mutation of authored controls.
void fx_parameter_automation_process(const FxVTable*, FxHandle*, const EngineMixerAutomation*,
                                     float baseline, float* io, int frames, int channels,
                                     uint64_t start, uint64_t latency);
