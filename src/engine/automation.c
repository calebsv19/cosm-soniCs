#include "engine/automation.h"

#include <stdlib.h>
#include <string.h>

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

bool engine_automation_lane_copy(const EngineAutomationLane* src, EngineAutomationLane* dst) {
    if (!src || !dst) {
        return false;
    }
    engine_automation_lane_free(dst);
    dst->target = src->target;
    if (src->point_count <= 0) {
        return true;
    }
    dst->points = (EngineAutomationPoint*)malloc(sizeof(EngineAutomationPoint) * (size_t)src->point_count);
    if (!dst->points) {
        dst->point_capacity = 0;
        dst->point_count = 0;
        return false;
    }
    memcpy(dst->points, src->points, sizeof(EngineAutomationPoint) * (size_t)src->point_count);
    dst->point_count = src->point_count;
    dst->point_capacity = src->point_count;
    return true;
}

bool engine_automation_lane_set_points(EngineAutomationLane* lane, const EngineAutomationPoint* points, int count) {
    if (!lane) {
        return false;
    }
    engine_automation_lane_free(lane);
    if (!points || count <= 0) {
        return true;
    }
    lane->points = (EngineAutomationPoint*)malloc(sizeof(EngineAutomationPoint) * (size_t)count);
    if (!lane->points) {
        lane->point_capacity = 0;
        lane->point_count = 0;
        return false;
    }
    memcpy(lane->points, points, sizeof(EngineAutomationPoint) * (size_t)count);
    lane->point_count = count;
    lane->point_capacity = count;
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
    if (!lane) {
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
    if (!automation_ensure_capacity(lane, lane->point_count + 1)) {
        return false;
    }
    lane->points[lane->point_count++] = (EngineAutomationPoint){frame, value};
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
    if (!lane || point_index < 0 || point_index >= lane->point_count) {
        return false;
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

float engine_automation_lane_eval(const EngineAutomationLane* lane, uint64_t frame, uint64_t clip_frames) {
    if (!lane || lane->point_count <= 0 || clip_frames == 0) {
        return 0.0f;
    }
    const EngineAutomationPoint* points = lane->points;
    int count = lane->point_count;
    if (frame <= points[0].frame) {
        uint64_t end_frame = points[0].frame;
        if (end_frame == 0) {
            return points[0].value;
        }
        float t = (float)frame / (float)end_frame;
        return points[0].value * t;
    }
    if (frame >= points[count - 1].frame) {
        uint64_t start_frame = points[count - 1].frame;
        uint64_t end_frame = clip_frames > start_frame ? clip_frames - start_frame : 0;
        if (end_frame == 0) {
            return points[count - 1].value;
        }
        float t = (float)(frame - start_frame) / (float)end_frame;
        if (t > 1.0f) t = 1.0f;
        return points[count - 1].value * (1.0f - t);
    }
    for (int i = 0; i < count - 1; ++i) {
        const EngineAutomationPoint* a = &points[i];
        const EngineAutomationPoint* b = &points[i + 1];
        if (frame >= a->frame && frame <= b->frame) {
            uint64_t span = b->frame - a->frame;
            if (span == 0) {
                return b->value;
            }
            float t = (float)(frame - a->frame) / (float)span;
            return a->value + (b->value - a->value) * t;
        }
    }
    return 0.0f;
}

bool engine_automation_target_is_instrument_param(EngineAutomationTarget target) {
    return target >= ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL &&
           target <= ENGINE_AUTOMATION_TARGET_INSTRUMENT_VIBRATO_DEPTH;
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
    case ENGINE_AUTOMATION_TARGET_COUNT:
    default:
        return "";
    }
}
