#include "engine/automation.h"
#include <math.h>

// Validates the complete authored lane without changing its ownership or ordering.
bool engine_automation_lane_validate(const EngineAutomationLane* lane, uint64_t region_frames) {
    if (!lane || lane->target < 0 || lane->target >= ENGINE_AUTOMATION_TARGET_COUNT ||
        lane->domain < 0 || lane->domain >= ENGINE_AUTOMATION_DOMAIN_COUNT ||
        lane->before < 0 || lane->before >= ENGINE_AUTOMATION_BOUNDARY_COUNT ||
        lane->after < 0 || lane->after >= ENGINE_AUTOMATION_BOUNDARY_COUNT ||
        lane->point_count < 0 || lane->point_count > ENGINE_AUTOMATION_POINT_LIMIT ||
        (lane->point_count > 0 && !lane->points)) return false;
    if (lane->domain == ENGINE_AUTOMATION_DOMAIN_PHYSICAL &&
        lane->target != ENGINE_AUTOMATION_TARGET_VOLUME && lane->target != ENGINE_AUTOMATION_TARGET_PAN) return false;
    for (int i = 0; i < lane->point_count; ++i) {
        const EngineAutomationPoint* p = &lane->points[i];
        if (!isfinite(p->value) || p->frame > region_frames || p->curve < 0 || p->curve >= ENGINE_AUTOMATION_CURVE_COUNT ||
            (i > 0 && lane->points[i - 1].frame >= p->frame)) return false;
        if (lane->domain == ENGINE_AUTOMATION_DOMAIN_PHYSICAL) {
            float low = lane->target == ENGINE_AUTOMATION_TARGET_VOLUME ? -120.f : -1.f;
            float high = lane->target == ENGINE_AUTOMATION_TARGET_VOLUME ? 24.f : 1.f;
            if (p->value < low || p->value > high) return false;
        }
    }
    return true;
}

// Returns neutral gain (0 dB), centered balance, or a zero additive parameter offset.
float engine_automation_lane_baseline(const EngineAutomationLane* lane) {
    (void)lane;
    return 0.f;
}

// Labels serialized values distinctly so legacy offsets cannot be mistaken for dB.
const char* engine_automation_lane_unit(const EngineAutomationLane* lane) {
    if (!lane || lane->domain == ENGINE_AUTOMATION_DOMAIN_OFFSET) return "offset";
    return lane->target == ENGINE_AUTOMATION_TARGET_VOLUME ? "dB" : "balance";
}

// Maps explicit dB values to a gain delta while preserving legacy offsets exactly.
float engine_automation_value_to_delta(const EngineAutomationLane* lane, float value) {
    if (!lane || !isfinite(value)) return 0.f;
    if (lane->domain == ENGINE_AUTOMATION_DOMAIN_PHYSICAL && lane->target == ENGINE_AUTOMATION_TARGET_VOLUME) {
        return value <= -120.f ? -1.f : powf(10.f, value / 20.f) - 1.f;
    }
    return value;
}

// Interpolates a bounded segment without intermediate float overflow.
static float interpolate(float a, float b, double t, EngineAutomationCurve curve) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    if (curve == ENGINE_AUTOMATION_CURVE_HOLD) return a;
    if (curve == ENGINE_AUTOMATION_CURVE_SMOOTH) t = t * t * (3 - 2 * t);
    return (float)((double)a * (1 - t) + (double)b * t);
}

// Uses logarithmic point lookup with explicit endpoint behavior and sample-exact hold transitions.
float engine_automation_lane_eval_value(const EngineAutomationLane* lane, uint64_t frame, uint64_t region_frames) {
    float baseline = engine_automation_lane_baseline(lane);
    if (!lane || lane->point_count <= 0 || !lane->points || region_frames == 0) return baseline;
    if (frame > region_frames) frame = region_frames;
    const EngineAutomationPoint* first = lane->points;
    const EngineAutomationPoint* last = &lane->points[lane->point_count - 1];
    if (frame <= first->frame) {
        if (!first->frame || lane->before == ENGINE_AUTOMATION_BOUNDARY_HOLD) return first->value;
        return interpolate(baseline, first->value, (double)frame / first->frame, ENGINE_AUTOMATION_CURVE_LINEAR);
    }
    if (frame >= last->frame) {
        if (region_frames <= last->frame || lane->after == ENGINE_AUTOMATION_BOUNDARY_HOLD) return last->value;
        return interpolate(last->value, baseline, (double)(frame - last->frame) / (region_frames - last->frame), last->curve);
    }
    int low = 0, high = lane->point_count - 1;
    while (high - low > 1) {
        int mid = low + (high - low) / 2;
        if (lane->points[mid].frame <= frame) low = mid;
        else high = mid;
    }
    const EngineAutomationPoint* a = &lane->points[low];
    const EngineAutomationPoint* b = &lane->points[high];
    return interpolate(a->value, b->value, (double)(frame - a->frame) / (b->frame - a->frame), a->curve);
}
