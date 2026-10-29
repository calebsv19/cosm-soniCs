#include "engine/mixer_automation.h"
#include <math.h>
// Validates the complete fixed-capacity lane before publication or project loading.
bool engine_mixer_automation_valid_range(const EngineMixerAutomation* a, float minimum, float maximum) {
    if (!isfinite(minimum) || !isfinite(maximum) || minimum>maximum)return false;
    if (!a || a->count < 0 || a->count > ENGINE_MIX_AUTOMATION_POINTS || a->before < 0 ||
        a->before >= ENGINE_AUTOMATION_BOUNDARY_COUNT || a->after < 0 ||
        a->after >= ENGINE_AUTOMATION_BOUNDARY_COUNT)
        return false;
    if (!a->count && (a->before || a->after))
        return false;
    for (int i = 0; i < a->count; ++i) {
        const EngineAutomationPoint* p = &a->points[i];
        if (!isfinite(p->value) || p->value < minimum || p->value > maximum || p->curve < 0 ||
            p->curve >= ENGINE_AUTOMATION_CURVE_COUNT || (i && p->frame <= a->points[i - 1].frame))
            return false;
    }
    return true;
}
// Retains the nonnegative mixer-lane validation contract.
bool engine_mixer_automation_valid(const EngineMixerAutomation* a, float maximum) { return engine_mixer_automation_valid_range(a, 0, maximum); }
// Protects exact routing guards without relying on unused array storage.
bool engine_mixer_automation_equal(const EngineMixerAutomation* a, const EngineMixerAutomation* b) {
    if (!a || !b || a->count < 0 || a->count > ENGINE_MIX_AUTOMATION_POINTS || b->count < 0 ||
        b->count > ENGINE_MIX_AUTOMATION_POINTS)
        return false;
    if (a->count != b->count || a->before != b->before || a->after != b->after)
        return false;
    for (int i = 0; i < a->count; ++i)
        if (a->points[i].frame != b->points[i].frame || a->points[i].value != b->points[i].value ||
            a->points[i].curve != b->points[i].curve)
            return false;
    return true;
}
// Uses absolute physical gain within authored segments and explicit static-baseline or hold
// extrapolation.
float engine_mixer_automation_eval(const EngineMixerAutomation* a, uint64_t frame, float baseline) {
    if (!a->count)
        return baseline;
    if (frame < a->points[0].frame)
        return a->before == ENGINE_AUTOMATION_BOUNDARY_HOLD ? a->points[0].value : baseline;
    const EngineAutomationPoint* last = &a->points[a->count - 1];
    if (frame > last->frame)
        return a->after == ENGINE_AUTOMATION_BOUNDARY_HOLD ? last->value : baseline;
    for (int i = 0; i < a->count; ++i) {
        const EngineAutomationPoint* p = &a->points[i];
        if (frame == p->frame || i == a->count - 1)
            return p->value;
        const EngineAutomationPoint* next = &a->points[i + 1];
        if (frame < next->frame) {
            if (p->curve == ENGINE_AUTOMATION_CURVE_HOLD)
                return p->value;
            double t = (double)(frame - p->frame) / (double)(next->frame - p->frame);
            if (p->curve == ENGINE_AUTOMATION_CURVE_SMOOTH)
                t = t * t * (3 - 2 * t);
            return (float)(p->value + (next->value - p->value) * t);
        }
    }
    return baseline;
}
