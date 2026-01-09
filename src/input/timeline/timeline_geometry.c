#include "input/timeline/timeline_geometry.h"

#include "app_state.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/timeline_view.h"

#include <math.h>

static float clamp_scalar(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

float timeline_total_seconds(const AppState* state) {
    if (!state || !state->engine) {
        return 0.0f;
    }
    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    int sample_rate = cfg ? cfg->sample_rate : 0;
    if (sample_rate <= 0) {
        return 0.0f;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    uint64_t max_frames = 0;
    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        if (!track) continue;
        for (int i = 0; i < track->clip_count; ++i) {
            const EngineClip* clip = &track->clips[i];
            if (!clip) continue;
            uint64_t start = clip->timeline_start_frames;
            uint64_t length = clip->duration_frames;
            if (length == 0) {
                length = engine_clip_get_total_frames(state->engine, t, i);
            }
            uint64_t end = start + length;
            if (end > max_frames) {
                max_frames = end;
            }
        }
    }
    if (max_frames == 0) {
        return 0.0f;
    }
    return (float)max_frames / (float)sample_rate;
}

bool timeline_compute_geometry(const AppState* state, const Pane* timeline, TimelineGeometry* out_geom) {
    if (!state || !timeline || !out_geom) {
        return false;
    }
    TimelineGeometry geom = {0};
    geom.track_spacing = 12;
    geom.header_width = TIMELINE_TRACK_HEADER_WIDTH;
    geom.visible_seconds = clamp_scalar(state->timeline_visible_seconds,
                                        TIMELINE_MIN_VISIBLE_SECONDS,
                                        TIMELINE_MAX_VISIBLE_SECONDS);
    float vertical_scale = clamp_scalar(state->timeline_vertical_scale,
                                        TIMELINE_MIN_VERTICAL_SCALE,
                                        TIMELINE_MAX_VERTICAL_SCALE);
    geom.track_height = (int)(TIMELINE_BASE_TRACK_HEIGHT * vertical_scale);
    if (geom.track_height < 32) {
        geom.track_height = 32;
    }
    geom.track_top = timeline->rect.y + TIMELINE_CONTROLS_HEIGHT + 8;
    geom.content_left = timeline->rect.x + TIMELINE_TRACK_HEADER_WIDTH + TIMELINE_BORDER_MARGIN;
    int content_right = timeline->rect.x + timeline->rect.w - TIMELINE_BORDER_MARGIN;
    geom.content_width = content_right - geom.content_left;
    if (geom.content_width <= 0) {
        return false;
    }
    float total_seconds = timeline_total_seconds(state);
    float max_start = total_seconds > geom.visible_seconds ? total_seconds - geom.visible_seconds : 0.0f;
    if (max_start < 0.0f) {
        max_start = 0.0f;
    }
    float window_start = state->timeline_window_start_seconds;
    if (window_start < 0.0f) window_start = 0.0f;
    if (window_start > max_start) window_start = max_start;
    geom.window_start_seconds = window_start;
    geom.pixels_per_second = geom.visible_seconds > 0.0f
                                 ? (float)geom.content_width / geom.visible_seconds
                                 : 0.0f;
    if (geom.pixels_per_second <= 0.0f) {
        return false;
    }
    *out_geom = geom;
    return true;
}

float timeline_x_to_seconds(const TimelineGeometry* geom, int x) {
    if (!geom || geom->pixels_per_second <= 0.0f) {
        return 0.0f;
    }
    float local = (float)(x - geom->content_left) / geom->pixels_per_second;
    return geom->window_start_seconds + local;
}

int timeline_track_at_position(const AppState* state, int y, int track_height, int track_spacing) {
    if (!state) {
        return -1;
    }
    const Pane* timeline = ui_layout_get_pane(state, 1);
    if (!timeline) {
        return -1;
    }
    int track_top = timeline->rect.y + TIMELINE_CONTROLS_HEIGHT + 8;
    int relative = y - track_top;
    if (relative < 0) {
        return 0;
    }
    int lane_height = track_height + track_spacing;
    if (lane_height <= 0) {
        return 0;
    }
    int index = relative / lane_height;
    if (index < 0) {
        index = 0;
    }
    return index;
}
