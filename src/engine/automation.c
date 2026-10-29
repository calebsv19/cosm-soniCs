#include "engine/automation.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static int automation_point_compare(const void* a, const void* b) {
    const EngineAutomationPoint* pa = (const EngineAutomationPoint*)a;
    const EngineAutomationPoint* pb = (const EngineAutomationPoint*)b;
    if (pa->frame < pb->frame) {
        return -1;
    }
    if (pa->frame > pb->frame) {
        return 1;
    }
    return 0;
}

static void automation_sort_points(EngineAutomationLane* lane) {
    if (!lane || lane->point_count <= 1) {
        return;
    }
    qsort(lane->points, (size_t)lane->point_count, sizeof(EngineAutomationPoint), automation_point_compare);
}

void engine_automation_lane_init(EngineAutomationLane* lane, EngineAutomationTarget target) {
    if (!lane) {
        return;
    }
    memset(lane, 0, sizeof(*lane));
    lane->target = target;
    lane->points = NULL;
    lane->point_count = 0;
    lane->point_capacity = 0;
}

void engine_automation_lane_free(EngineAutomationLane* lane) {
    if (!lane) {
        return;
    }
    if (lane->points) {
        free(lane->points);
        lane->points = NULL;
    }
    lane->point_count = 0;
    lane->point_capacity = 0;
}

// Copies through replacement storage so allocation failure and self-copy preserve the original lane.
bool engine_automation_lane_copy(const EngineAutomationLane* src, EngineAutomationLane* dst) {
    if (!src || !dst) return false;
    EngineAutomationLane metadata = *src;
    if (!engine_automation_lane_set_points(dst, src->points, src->point_count)) return false;
    dst->target = metadata.target;
    dst->domain = metadata.domain;
    dst->before = metadata.before;
    dst->after = metadata.after;
    return true;
}

// Prepares all points before releasing old storage, including when the input aliases that storage.
bool engine_automation_lane_set_points(EngineAutomationLane* lane, const EngineAutomationPoint* points, int count) {
    if (!lane || count < 0 || count > ENGINE_AUTOMATION_POINT_LIMIT || (count > 0 && !points) ||
        (size_t)count > SIZE_MAX / sizeof(EngineAutomationPoint)) return false;
    EngineAutomationPoint* replacement = NULL;
    if (count > 0) {
        replacement = malloc(sizeof(*replacement) * (size_t)count);
        if (!replacement) return false;
        memcpy(replacement, points, sizeof(*replacement) * (size_t)count);
    }
    EngineAutomationLane candidate = *lane;
    candidate.points = replacement;
    candidate.point_count = candidate.point_capacity = count;
    automation_sort_points(&candidate);
    for (int i = 0; i < count; ++i) {
        if (!isfinite(replacement[i].value) || replacement[i].curve < 0 || replacement[i].curve >= ENGINE_AUTOMATION_CURVE_COUNT ||
            (i > 0 && replacement[i - 1].frame == replacement[i].frame)) {
            free(replacement);
            return false;
        }
    }
    free(lane->points);
    lane->points = replacement;
    lane->point_count = lane->point_capacity = count;
    automation_sort_points(lane);
    return true;
}

static bool automation_ensure_capacity(EngineAutomationLane* lane, int needed) {
    if (!lane) {
        return false;
    }
    if (lane->point_capacity >= needed) {
        return true;
    }
    int new_cap = lane->point_capacity == 0 ? 4 : lane->point_capacity * 2;
    if (new_cap < needed) {
        new_cap = needed;
    }
    EngineAutomationPoint* points = (EngineAutomationPoint*)realloc(lane->points,
                                                                    sizeof(EngineAutomationPoint) * (size_t)new_cap);
    if (!points) {
        return false;
    }
    lane->points = points;
    lane->point_capacity = new_cap;
    return true;
}

bool engine_automation_lane_insert_point(EngineAutomationLane* lane,
                                         uint64_t frame,
                                         float value,
                                         int* out_index) {
    if (!lane || !isfinite(value)) {
        return false;
    }
    for (int i = 0; i < lane->point_count; ++i) {
        if (lane->points[i].frame == frame) {
            lane->points[i].value = value;
            if (out_index) {
                *out_index = i;
            }
            return true;
        }
    }
    if (lane->point_count >= ENGINE_AUTOMATION_POINT_LIMIT) return false;
    if (!automation_ensure_capacity(lane, lane->point_count + 1)) {
        return false;
    }
    lane->points[lane->point_count++] = (EngineAutomationPoint){.frame = frame, .value = value};
    automation_sort_points(lane);
    if (out_index) {
        for (int i = 0; i < lane->point_count; ++i) {
            if (lane->points[i].frame == frame) {
                *out_index = i;
                break;
            }
        }
    }
    return true;
}

bool engine_automation_lane_update_point(EngineAutomationLane* lane,
                                         int point_index,
                                         uint64_t frame,
                                         float value,
                                         int* out_index) {
    if (!lane || !isfinite(value) || point_index < 0 || point_index >= lane->point_count) {
        return false;
    }
    for (int i = 0; i < lane->point_count; ++i) {
        if (i != point_index && lane->points[i].frame == frame) return false;
    }
    lane->points[point_index].frame = frame;
    lane->points[point_index].value = value;
    automation_sort_points(lane);
    if (out_index) {
        *out_index = point_index;
        for (int i = 0; i < lane->point_count; ++i) {
            if (lane->points[i].frame == frame && lane->points[i].value == value) {
                *out_index = i;
                break;
            }
        }
    }
    return true;
}

bool engine_automation_lane_remove_point(EngineAutomationLane* lane, int point_index) {
    if (!lane || point_index < 0 || point_index >= lane->point_count) {
        return false;
    }
    int remaining = lane->point_count - point_index - 1;
    if (remaining > 0) {
        memmove(&lane->points[point_index],
                &lane->points[point_index + 1],
                sizeof(EngineAutomationPoint) * (size_t)remaining);
    }
    lane->point_count--;
    return true;
}

// Evaluates a curve in its authored domain, then converts it to the existing playback delta.
float engine_automation_lane_eval(const EngineAutomationLane* lane, uint64_t frame, uint64_t clip_frames) {
    return engine_automation_value_to_delta(lane, engine_automation_lane_eval_value(lane, frame, clip_frames));
}

bool engine_automation_target_is_instrument_param(EngineAutomationTarget target) {
    return target >= ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL &&
           target < ENGINE_AUTOMATION_TARGET_COUNT;
}

const char* engine_automation_target_display_label(EngineAutomationTarget target) {
    switch (target) {
    case ENGINE_AUTOMATION_TARGET_VOLUME:
        return "VOL";
    case ENGINE_AUTOMATION_TARGET_PAN:
        return "PAN";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL:
        return "LEV";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_TONE:
        return "TON";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_ATTACK_MS:
        return "ATK";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_RELEASE_MS:
        return "REL";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_DECAY_MS:
        return "DEC";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_SUSTAIN:
        return "SUS";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_OSC_MIX:
        return "MIX";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_OSC2_DETUNE:
        return "DET";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_SUB_MIX:
        return "SUB";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_DRIVE:
        return "DRV";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_VIBRATO_RATE:
        return "VRT";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_VIBRATO_DEPTH:
        return "VDP";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_CUTOFF_HZ:
        return "CUT";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_RESONANCE_Q:
        return "RES";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_FILTER_ENV_OCTAVES: return "FEN";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_FILTER_DECAY_MS: return "FDC";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_FILTER_LFO_HZ: return "FLR";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_FILTER_LFO_OCTAVES: return "FLD";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_BODY_DECAY_MS: return "Body Decay";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_PITCH_ENV_SEMITONES: return "Pitch Drop";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_PITCH_DECAY_MS: return "Pitch Decay";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_NOISE_MIX: return "Noise Mix";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_NOISE_DECAY_MS: return "Noise Decay";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_CLICK_MIX: return "Click";

    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_DYNAMICS: return "Dynamics";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_VELOCITY_DEPTH: return "Velocity Timbre";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_SUSTAIN_PEDAL: return "Sustain Pedal";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_STRING_RESONANCE: return "String Resonance";
    case ENGINE_AUTOMATION_TARGET_INSTRUMENT_RELEASE_NOISE: return "Release Noise";

    case ENGINE_AUTOMATION_TARGET_COUNT:
    default:
        return "";
    }
}
