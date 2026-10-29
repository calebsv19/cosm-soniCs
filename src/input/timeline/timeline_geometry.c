#include "input/timeline/timeline_geometry.h"

#include "app_state.h"
#include "ui/layout.h"
#include "ui/panes.h"
#include "ui/timeline_view.h"
#include "engine/groups.h"

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

void timeline_get_scroll_bounds(const AppState* state, float visible_seconds, float* out_min_start, float* out_max_start) {
    if (!out_min_start || !out_max_start) {
        return;
    }
    float total_seconds = timeline_total_seconds(state);
    float padding = visible_seconds * 0.5f;
    if (padding < 0.5f) {
        padding = 0.5f;
    }
    float min_start = -padding;
    float max_start = (total_seconds > 0.0f ? total_seconds - visible_seconds : 0.0f) + padding;
    // Short projects must admit a zero-based view instead of forcing negative pre-roll.
    if (max_start < 0.0f) max_start = 0.0f;
    if (max_start < min_start) {
        max_start = min_start;
    }
    *out_min_start = min_start;
    *out_max_start = max_start;
}

bool timeline_compute_geometry(const AppState* state, const Pane* timeline, TimelineGeometry* out_geom) {
    if (!state || !timeline || !out_geom) {
        return false;
    }
    int controls_height = timeline_view_controls_height_for_width(timeline->rect.w);
    int ruler_height = timeline_view_ruler_height();
    TimelineGeometry geom = {0};
    geom.track_spacing = 2;
    geom.header_width = timeline_view_track_header_width();
    geom.visible_seconds = clamp_scalar(state->timeline_visible_seconds,
                                        TIMELINE_MIN_VISIBLE_SECONDS,
                                        TIMELINE_MAX_VISIBLE_SECONDS);
    float vertical_scale = clamp_scalar(state->timeline_vertical_scale,
                                        TIMELINE_MIN_VERTICAL_SCALE,
                                        TIMELINE_MAX_VERTICAL_SCALE);
    geom.track_height = (int)(TIMELINE_BASE_TRACK_HEIGHT * vertical_scale);
    if (geom.track_height < 36) {
        geom.track_height = 36;
    }
    int viewport_top = timeline->rect.y + controls_height + ruler_height;
    int viewport_height = timeline->rect.y + timeline->rect.h - viewport_top;
    if (viewport_height < 0) viewport_height = 0;
    geom.viewport = (SDL_Rect){timeline->rect.x, viewport_top, timeline->rect.w - 16, viewport_height};
    int count = state->engine ? engine_get_track_count(state->engine) : 0;
    count=engine_group_visible_count(engine_get_tracks(state->engine),count);
    int total_height = count > 0 ? count * (geom.track_height + geom.track_spacing) - geom.track_spacing : 0;
    geom.vertical_max = fmaxf(0.0f, (float)(total_height - viewport_height));
    geom.vertical_scroll = clamp_scalar(state->timeline_vertical_scroll, 0.0f, geom.vertical_max);
    geom.track_top = viewport_top - (int)lroundf(geom.vertical_scroll);
    if (geom.vertical_max > 0.0f && viewport_height > 0) {
        geom.scrollbar = (SDL_Rect){timeline->rect.x + timeline->rect.w - 14, viewport_top, 12, viewport_height};
        int thumb_height = (int)((float)viewport_height * viewport_height / total_height);
        if (thumb_height < 24) thumb_height = 24;
        if (thumb_height > viewport_height) thumb_height = viewport_height;
        int travel = viewport_height - thumb_height;
        geom.scroll_thumb = (SDL_Rect){geom.scrollbar.x,
            viewport_top + (int)lroundf(geom.vertical_scroll / geom.vertical_max * travel), 12, thumb_height};
    }
    geom.content_left = timeline->rect.x + geom.header_width + TIMELINE_BORDER_MARGIN;
    int content_right = timeline->rect.x + timeline->rect.w - TIMELINE_BORDER_MARGIN - 16;
    geom.content_width = content_right - geom.content_left;
    if (geom.content_width <= 0) {
        return false;
    }
    float min_start = 0.0f;
    float max_start = 0.0f;
    timeline_get_scroll_bounds(state, geom.visible_seconds, &min_start, &max_start);
    float window_start = clamp_scalar(state->timeline_window_start_seconds, min_start, max_start);
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

int timeline_seconds_to_x(const TimelineGeometry* geom, float seconds) {
    if (!geom || geom->pixels_per_second <= 0.0f) {
        return 0;
    }
    float local = (seconds - geom->window_start_seconds) * geom->pixels_per_second;
    return geom->content_left + (int)lroundf(local);
}

int timeline_track_at_position(const AppState* state, int y, int track_height, int track_spacing) {
    if (!state) {
        return -1;
    }
    const Pane* timeline = ui_layout_get_pane(state, 1);
    if (!timeline) {
        return -1;
    }
    TimelineGeometry geom;
    if (!timeline_compute_geometry(state, timeline, &geom) ||
        y < geom.viewport.y || y >= geom.viewport.y + geom.viewport.h) return -1;
    int lane_height = track_height + track_spacing;
    if (lane_height <= 0) return -1;
    int relative = y - geom.track_top;
    int index = relative / lane_height;
    int count = state->engine ? engine_get_track_count(state->engine) : 0;
    if (index < 0 || index >= count || relative % lane_height >= track_height) return -1;
    return engine_group_visible_track(engine_get_tracks(state->engine),count,index);
}

// Normalizes a vertical request against the live pane geometry.
void timeline_set_vertical_scroll(AppState* state, float pixels) {
    const Pane* pane = state ? ui_layout_get_pane(state, 1) : NULL;
    TimelineGeometry geom;
    if (!pane || !timeline_compute_geometry(state, pane, &geom)) return;
    state->timeline_vertical_scroll = clamp_scalar(pixels, 0.0f, geom.vertical_max);
}

// Keeps a newly focused row inside the visible lane area.
void timeline_reveal_track(AppState* state, int track_index) {
    const Pane* pane = state ? ui_layout_get_pane(state, 1) : NULL;
    TimelineGeometry geom;
    if (!pane || !state->engine || track_index < 0 || track_index >= engine_get_track_count(state->engine) ||
        !timeline_compute_geometry(state, pane, &geom)) return;
    int row=engine_group_visible_row(engine_get_tracks(state->engine),engine_get_track_count(state->engine),track_index);
    const EngineTrack* rows=engine_get_tracks(state->engine);int count=engine_get_track_count(state->engine);
    for(int depth=0;row<0 && track_index>=0 && depth<count;++depth) {
        track_index=engine_group_index(rows,count,rows[track_index].parent_id);
        row=engine_group_visible_row(rows,count,track_index);
    }
    if(row<0)return;
    float top = (float)(row * (geom.track_height + geom.track_spacing));
    float scroll = geom.vertical_scroll;
    if (top < scroll) scroll = top;
    else if (top + geom.track_height > scroll + geom.viewport.h)
        scroll = top + geom.track_height - geom.viewport.h;
    timeline_set_vertical_scroll(state, scroll);
}

// Anchors row zoom to the same fractional lane under the pointer.
void timeline_zoom_rows(AppState* state, float scale, int anchor_y) {
    const Pane* pane = state ? ui_layout_get_pane(state, 1) : NULL;
    TimelineGeometry before, after;
    if (!pane || !timeline_compute_geometry(state, pane, &before)) return;
    int local = anchor_y - before.viewport.y;
    if (local < 0) local = 0;
    if (local > before.viewport.h) local = before.viewport.h;
    float row = (before.vertical_scroll + local) / (before.track_height + before.track_spacing);
    state->timeline_vertical_scale = clamp_scalar(scale, TIMELINE_MIN_VERTICAL_SCALE, TIMELINE_MAX_VERTICAL_SCALE);
    if (timeline_compute_geometry(state, pane, &after))
        timeline_set_vertical_scroll(state, row * (after.track_height + after.track_spacing) - local);
}

// Projects a stable track into its visible row without moving engine indices.
int timeline_track_lane_y(const AppState* state,int track,int origin,int height,int spacing) {
    int row=engine_group_visible_row(engine_get_tracks(state->engine),engine_get_track_count(state->engine),track);
    return row<0 ? -100000 : origin+row*(height+spacing);
}
// Adds hierarchy indentation and a fold target to the existing header geometry.
void timeline_track_header_layout(const AppState* state,int track,const SDL_Rect* rect,int y,int height,int width,TimelineTrackHeaderLayout* out) {
    timeline_view_compute_track_header_layout(rect,y,height,width,out);
    const EngineTrack* tracks=engine_get_tracks(state->engine);int count=engine_get_track_count(state->engine);
    if(!engine_groups_present(tracks,count))return;
    int indent=engine_group_depth(tracks,count,track)*10;
    if(indent>30)indent=30;
    if(tracks[track].is_group)out->fold_rect=(SDL_Rect){out->text_x+indent,out->text_y,16,18};
    out->text_x+=indent+18;
}
