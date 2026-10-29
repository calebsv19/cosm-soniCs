#include "effects/parameter_automation.h"
#include <stddef.h>
// Restricts this slice to processors whose parameter changes preserve latency and deterministic
// sample behavior.
bool fx_parameter_automation_range(uint32_t type, uint32_t parameter, float* lo, float* hi) {
    if (parameter || (type != 1 && type != 3))
        return false;
    *lo = type == 1 ? -96 : -1;
    *hi = type == 1 ? 24 : 1;
    return true;
}
// Empty unsupported lanes remain compatible with legacy static effects.
bool fx_parameter_automation_valid(uint32_t type, const EngineMixerAutomation* a) {
    if (!a)
        return false;
    float lo = 0, hi = 0;
    if (!a->count)
        return engine_mixer_automation_valid(a, 0);
    return fx_parameter_automation_range(type, 0, &lo, &hi) &&
           engine_mixer_automation_valid_range(a, lo, hi);
}
// Keeps physical point timing independent of host block partitions and static-edit smoothing.
void fx_parameter_automation_process(const FxVTable* vt, FxHandle* h,
                                     const EngineMixerAutomation* a, float baseline, float* io,
                                     int frames, int channels, uint64_t start, uint64_t latency) {
    for (int f = 0; f < frames; ++f) {
        uint64_t rendered = start + (uint64_t)f;
        float v = rendered >= latency
                      ? engine_mixer_automation_eval(a, rendered - latency, baseline)
                      : baseline;
        if (vt->set_param_automated)
            vt->set_param_automated(h, 0, v);
        else
            vt->set_param(h, 0, v);
        float* sample = io + (size_t)f * channels;
        vt->process(h, sample, sample, 1, channels);
    }
}
