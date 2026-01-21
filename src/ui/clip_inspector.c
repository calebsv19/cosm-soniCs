#include "ui/clip_inspector.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/audio_source.h"
#include "engine/sampler.h"
#include "audio/media_registry.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/render_utils.h"
#include "ui/waveform_render.h"

#include <math.h>
#include <string.h>

#define INSPECTOR_GAIN_MIN 0.0f
#define INSPECTOR_GAIN_MAX 4.0f
#define INSPECTOR_MARGIN 12
#define INSPECTOR_COL_GAP 16
#define INSPECTOR_ROW_HEIGHT 18
#define INSPECTOR_ROW_GAP 6
#define INSPECTOR_SECTION_GAP 10
#define INSPECTOR_LABEL_SCALE 1.0f
#define INSPECTOR_VALUE_SCALE 1.0f
#define INSPECTOR_SLIDER_HEIGHT 6
#define INSPECTOR_PRESET_GAP 6
#define INSPECTOR_WAVE_HEADER_HEIGHT 26
#define INSPECTOR_WAVE_HEADER_PAD 8
#define INSPECTOR_WAVE_SECTION_GAP 10
#define INSPECTOR_WAVE_HEIGHT 140

static EngineRuntimeConfig clip_inspector_active_config(const AppState* state) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    if (!state) {
        return cfg;
    }
    cfg = state->runtime_cfg;
    if (state->engine) {
        const EngineRuntimeConfig* engine_cfg = engine_get_config(state->engine);
        if (engine_cfg) {
            cfg = *engine_cfg;
        }
    }
    return cfg;
}

static void zero_layout(ClipInspectorLayout* layout) {
    if (!layout) {
        return;
    }
    SDL_zero(*layout);
}

static void draw_slider(SDL_Renderer* renderer, const SDL_Rect* track_rect, float t) {
    if (!renderer || !track_rect || track_rect->w <= 0 || track_rect->h <= 0) {
        return;
    }
    SDL_Color track_bg = {36, 36, 44, 255};
    SDL_Color track_border = {90, 90, 110, 255};
    SDL_Color fill_color = {80, 120, 170, 220};
    SDL_SetRenderDrawColor(renderer, track_bg.r, track_bg.g, track_bg.b, track_bg.a);
    SDL_RenderFillRect(renderer, track_rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, track_rect);

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    SDL_Rect fill_rect = *track_rect;
    fill_rect.w = (int)(t * (float)track_rect->w);
    if (fill_rect.w < 0) fill_rect.w = 0;
    SDL_SetRenderDrawColor(renderer, fill_color.r, fill_color.g, fill_color.b, fill_color.a);
    SDL_RenderFillRect(renderer, &fill_rect);

    SDL_Rect handle_rect = {
        track_rect->x + fill_rect.w - 3,
        track_rect->y - 2,
        6,
        track_rect->h + 4,
    };
    if (handle_rect.x < track_rect->x - 2) {
        handle_rect.x = track_rect->x - 2;
    }
    if (handle_rect.x + handle_rect.w > track_rect->x + track_rect->w + 2) {
        handle_rect.x = track_rect->x + track_rect->w - 2;
    }
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, &handle_rect);
}

static void draw_button(SDL_Renderer* renderer, const SDL_Rect* rect, const char* label, bool active) {
    if (!renderer || !rect || !label) {
        return;
    }
    SDL_Color base = {48, 52, 62, 255};
    SDL_Color highlight = {120, 160, 220, 255};
    SDL_Color border = {90, 95, 110, 255};
    SDL_Color text = {220, 220, 230, 255};

    SDL_Color fill = active ? highlight : base;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    float scale = 1.0f;
    int text_w = ui_measure_text_width(label, scale);
    int text_h = ui_font_line_height(scale);
    int text_x = rect->x + (rect->w - text_w) / 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text, scale);
}

static void clip_inspector_draw_edit_box(SDL_Renderer* renderer, const SDL_Rect* rect) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, 36, 38, 46, 255);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, 100, 110, 130, 255);
    SDL_RenderDrawRect(renderer, rect);
}

static int clip_inspector_name_visible_end(const char* text,
                                           int start,
                                           int available_width,
                                           float scale) {
    if (!text || available_width <= 0) {
        return start;
    }
    int len = (int)strlen(text);
    if (start < 0) start = 0;
    if (start > len) start = len;
    const char* ellipsis = "...";
    int ellipsis_w = ui_measure_text_width(ellipsis, scale);
    int end = len;
    while (end > start) {
        int left_w = start > 0 ? ellipsis_w : 0;
        int right_w = end < len ? ellipsis_w : 0;
        char scratch[ENGINE_CLIP_NAME_MAX];
        int count = end - start;
        if (count >= (int)sizeof(scratch)) count = (int)sizeof(scratch) - 1;
        memcpy(scratch, text + start, (size_t)count);
        scratch[count] = '\0';
        int text_w = ui_measure_text_width(scratch, scale);
        if (left_w + text_w + right_w <= available_width) {
            break;
        }
        end--;
    }
    return end;
}

static void clip_inspector_build_name_view(const char* text,
                                           int start,
                                           int available_width,
                                           float scale,
                                           char* out,
                                           size_t out_size,
                                           int* out_start,
                                           int* out_end,
                                           int* out_left_ellipsis_w) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!text) {
        return;
    }
    int len = (int)strlen(text);
    if (start < 0) start = 0;
    if (start > len) start = len;
    int end = clip_inspector_name_visible_end(text, start, available_width, scale);
    int visible_len = end - start;
    int attempts = 0;
    while (start > 0 && visible_len < CLIP_INSPECTOR_NAME_MIN_VISIBLE_CHARS && attempts < 64) {
        start--;
        end = clip_inspector_name_visible_end(text, start, available_width, scale);
        visible_len = end - start;
        attempts++;
    }
    if (visible_len <= 0 && len > 0) {
        end = start + 1;
        if (end > len) end = len;
        visible_len = end - start;
    }
    const char* ellipsis = "...";
    int ellipsis_w = ui_measure_text_width(ellipsis, scale);
    if (out_left_ellipsis_w) {
        *out_left_ellipsis_w = start > 0 ? ellipsis_w : 0;
    }
    if (out_start) {
        *out_start = start;
    }
    if (start > 0) {
        SDL_strlcpy(out, ellipsis, out_size);
    }
    char scratch[ENGINE_CLIP_NAME_MAX];
    int count = end - start;
    if (count < 0) count = 0;
    if (count >= (int)sizeof(scratch)) count = (int)sizeof(scratch) - 1;
    memcpy(scratch, text + start, (size_t)count);
    scratch[count] = '\0';
    SDL_strlcat(out, scratch, out_size);
    if (end < len) {
        SDL_strlcat(out, ellipsis, out_size);
    }
    if (out_end) {
        *out_end = end;
    }
}

static const char* inspector_basename(const char* path, char* scratch, size_t scratch_len) {
    if (!scratch || scratch_len == 0) {
        return "";
    }
    scratch[0] = '\0';
    if (!path || path[0] == '\0') {
        return scratch;
    }
    const char* base = strrchr(path, '/');
#if defined(_WIN32)
    const char* alt = strrchr(path, '\\');
    if (!base || (alt && alt > base)) {
        base = alt;
    }
#endif
    base = base ? base + 1 : path;
    SDL_strlcpy(scratch, base, scratch_len);
    char* dot = strrchr(scratch, '.');
    if (dot) {
        *dot = '\0';
    }
    return scratch;
}

static const char* inspector_clip_display_name(const AppState* state,
                                               const EngineClip* clip,
                                               char* scratch,
                                               size_t scratch_len) {
    if (!clip) {
        return "(unnamed clip)";
    }
    if (clip->name[0] != '\0') {
        return clip->name;
    }
    const char* media_id = engine_clip_get_media_id(clip);
    if (state && media_id && media_id[0] != '\0') {
        const MediaRegistryEntry* entry = media_registry_find_by_id(&state->media_registry, media_id);
        if (entry) {
            if (entry->name[0] != '\0') {
                return entry->name;
            }
            if (entry->path[0] != '\0') {
                return inspector_basename(entry->path, scratch, scratch_len);
            }
        }
    }
    const char* media_path = engine_clip_get_media_path(clip);
    if (media_path && media_path[0] != '\0') {
        return inspector_basename(media_path, scratch, scratch_len);
    }
    return "(unnamed clip)";
}

// Assigns label/value rectangles for a left-column inspector row.
static void clip_inspector_set_row(ClipInspectorLayout* layout,
                                   ClipInspectorRowType row,
                                   int label_x,
                                   int label_w,
                                   int value_x,
                                   int value_w,
                                   int y,
                                   int h) {
    if (!layout || row < 0 || row >= CLIP_INSPECTOR_ROW_COUNT) {
        return;
    }
    layout->rows[row].label_rect = (SDL_Rect){label_x, y, label_w, h};
    layout->rows[row].value_rect = (SDL_Rect){value_x, y, value_w, h};
}

// Computes the waveform view range used by the inspector panel.
bool clip_inspector_get_waveform_view(const AppState* state,
                                      const EngineClip* clip,
                                      uint64_t clip_frames,
                                      uint64_t* view_start,
                                      uint64_t* view_frames) {
    if (!state || !clip || !view_start || !view_frames) {
        return false;
    }
    bool view_source = state->inspector.waveform.view_source;
    uint64_t total = 0;
    if (clip->media && clip->media->frame_count > 0) {
        total = clip->media->frame_count;
    }
    if (view_source) {
        if (total == 0) {
            return false;
        }
        float zoom = state->inspector.waveform.zoom;
        if (zoom < 1.0f) zoom = 1.0f;
        uint64_t frames = (uint64_t)llround((double)total / (double)zoom);
        if (frames < 1) frames = 1;
        if (frames > total) frames = total;
        float scroll = state->inspector.waveform.scroll;
        if (scroll < 0.0f) scroll = 0.0f;
        if (scroll > 1.0f) scroll = 1.0f;
        uint64_t max_start = total > frames ? total - frames : 0;
        *view_start = (uint64_t)llround((double)max_start * (double)scroll);
        *view_frames = frames;
        return true;
    }

    *view_start = clip->offset_frames;
    *view_frames = clip_frames > 0 ? clip_frames : 1;
    return true;
}

// Draws a fade overlay into the inspector waveform using the active curve.
static void clip_inspector_draw_fade_overlay(SDL_Renderer* renderer,
                                             const SDL_Rect* rect,
                                             uint64_t view_start,
                                             uint64_t view_frames,
                                             uint64_t fade_start,
                                             uint64_t fade_frames,
                                             EngineFadeCurve curve,
                                             bool invert,
                                             Uint8 alpha) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0 || view_frames == 0 || fade_frames == 0) {
        return;
    }
    uint64_t view_end = view_start + view_frames;
    uint64_t fade_end = fade_start + fade_frames;
    if (fade_end <= view_start || fade_start >= view_end) {
        return;
    }
    double start_t = (double)(fade_start > view_start ? fade_start - view_start : 0) / (double)view_frames;
    double end_t = (double)(fade_end > view_start ? fade_end - view_start : 0) / (double)view_frames;
    if (start_t < 0.0) start_t = 0.0;
    if (end_t > 1.0) end_t = 1.0;
    if (end_t <= start_t) {
        return;
    }
    int x0 = rect->x + (int)floor(start_t * (double)rect->w);
    int x1 = rect->x + (int)ceil(end_t * (double)rect->w);
    if (x1 <= x0) x1 = x0 + 1;
    if (x0 < rect->x) x0 = rect->x;
    if (x1 > rect->x + rect->w) x1 = rect->x + rect->w;

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
    for (int px = x0; px < x1; ++px) {
        double frame = (double)view_start +
                       ((double)(px - rect->x) + 0.5) / (double)rect->w * (double)view_frames;
        float t = (float)((frame - (double)fade_start) / (double)fade_frames);
        float gain = ui_fade_curve_eval(curve, t);
        float overlay = invert ? (1.0f - gain) : gain;
        if (overlay <= 0.0f) {
            continue;
        }
        int h = (int)lroundf(overlay * (float)rect->h);
        if (h <= 0) {
            continue;
        }
        SDL_RenderDrawLine(renderer, px, rect->y, px, rect->y + h);
    }
}

// Draws automation lines and points in the inspector waveform view.
static void clip_inspector_draw_automation(SDL_Renderer* renderer,
                                           const SDL_Rect* rect,
                                           uint64_t view_start,
                                           uint64_t view_frames,
                                           const EngineClip* clip,
                                           uint64_t clip_frames,
                                           const AutomationUIState* automation_ui,
                                           EngineAutomationTarget target,
                                           int track_index,
                                           int clip_index) {
    if (!renderer || !rect || !clip || rect->w <= 0 || rect->h <= 0 || view_frames == 0 || clip_frames == 0) {
        return;
    }
    const EngineAutomationLane* lane = NULL;
    for (int i = 0; i < clip->automation_lane_count; ++i) {
        if (clip->automation_lanes[i].target == target) {
            lane = &clip->automation_lanes[i];
            break;
        }
    }
    int baseline = rect->y + rect->h / 2;
    int range = rect->h / 2 - 4;
    if (range < 4) {
        range = 4;
    }
    uint64_t clip_start = clip->offset_frames;
    uint64_t clip_end = clip_start + clip_frames;
    float prev_value = 0.0f;
    uint64_t prev_frame = clip_start;
    SDL_SetRenderDrawColor(renderer, 170, 210, 230, 220);
    if (lane && lane->point_count > 0) {
        for (int i = 0; i < lane->point_count; ++i) {
            const EngineAutomationPoint* point = &lane->points[i];
            uint64_t abs_frame = clip_start + point->frame;
            if (abs_frame > clip_end) {
                abs_frame = clip_end;
            }
            double t0 = (double)(prev_frame > view_start ? prev_frame - view_start : 0) / (double)view_frames;
            double t1 = (double)(abs_frame > view_start ? abs_frame - view_start : 0) / (double)view_frames;
            if (t0 < 0.0) t0 = 0.0;
            if (t0 > 1.0) t0 = 1.0;
            if (t1 < 0.0) t1 = 0.0;
            if (t1 > 1.0) t1 = 1.0;
            int x0 = rect->x + (int)llround(t0 * (double)rect->w);
            int y0 = baseline - (int)llround((double)prev_value * (double)range);
            int x1 = rect->x + (int)llround(t1 * (double)rect->w);
            int y1 = baseline - (int)llround((double)point->value * (double)range);
            SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
            prev_frame = abs_frame;
            prev_value = point->value;
        }
    }
    double t_end = (double)(clip_end > view_start ? clip_end - view_start : 0) / (double)view_frames;
    if (t_end < 0.0) t_end = 0.0;
    if (t_end > 1.0) t_end = 1.0;
    int x_end = rect->x + (int)llround(t_end * (double)rect->w);
    int x_prev = rect->x + (int)llround((double)(prev_frame > view_start ? prev_frame - view_start : 0) / (double)view_frames * (double)rect->w);
    int y_prev = baseline - (int)llround((double)prev_value * (double)range);
    SDL_RenderDrawLine(renderer, x_prev, y_prev, x_end, baseline);

    if (lane && lane->point_count > 0) {
        for (int i = 0; i < lane->point_count; ++i) {
            const EngineAutomationPoint* point = &lane->points[i];
            uint64_t abs_frame = clip_start + point->frame;
            if (abs_frame > clip_end) {
                abs_frame = clip_end;
            }
            if (abs_frame < view_start || abs_frame > view_start + view_frames) {
                continue;
            }
            double t = (double)(abs_frame - view_start) / (double)view_frames;
            int x = rect->x + (int)llround(t * (double)rect->w);
            int y = baseline - (int)llround((double)point->value * (double)range);
            SDL_Rect dot = {x - 3, y - 3, 6, 6};
            bool selected = automation_ui &&
                            automation_ui->track_index == track_index &&
                            automation_ui->clip_index == clip_index &&
                            automation_ui->point_index == i &&
                            automation_ui->target == target;
            if (selected) {
                SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 120, 150, 170, 255);
            }
            SDL_RenderFillRect(renderer, &dot);
        }
    }
}

void clip_inspector_compute_layout(const AppState* state, ClipInspectorLayout* layout) {
    if (!state || !layout) return;

    zero_layout(layout);

    // ---------- Host pane ----------
    const Pane* mixer = ui_layout_get_pane(state, 2);
    if (!mixer) return;
    layout->panel_rect = mixer->rect;

    // ---------- Metrics / constants ----------
    const int M = INSPECTOR_MARGIN;
    const int col_gap = INSPECTOR_COL_GAP;
    const int row_h = INSPECTOR_ROW_HEIGHT;
    const int row_gap = INSPECTOR_ROW_GAP;
    const int section_gap = INSPECTOR_SECTION_GAP;
    const int slider_h = INSPECTOR_SLIDER_HEIGHT;
    const int content_x = mixer->rect.x + M;
    const int content_y = mixer->rect.y + M;
    const int content_w = mixer->rect.w - 2 * M;
    const int content_h = mixer->rect.h - 2 * M;
    if (content_w <= 0 || content_h <= 0) {
        return;
    }

    int left_w = (int)(content_w * 0.25f);
    int right_w = content_w - left_w - col_gap;
    const int min_left = 220;
    const int min_right = 220;
    if (left_w < min_left) left_w = min_left;
    right_w = content_w - left_w - col_gap;
    if (right_w < min_right) {
        right_w = min_right;
        left_w = content_w - right_w - col_gap;
        if (left_w < min_left) left_w = min_left;
    }

    layout->left_rect = (SDL_Rect){content_x, content_y, left_w, content_h};
    layout->right_rect = (SDL_Rect){content_x + left_w + col_gap, content_y, right_w, content_h};

    int right_inner_x = layout->right_rect.x + INSPECTOR_WAVE_HEADER_PAD;
    int right_inner_w = layout->right_rect.w - INSPECTOR_WAVE_HEADER_PAD * 2;
    if (right_inner_w < 0) right_inner_w = 0;
    int right_y = layout->right_rect.y + INSPECTOR_WAVE_HEADER_PAD;
    layout->right_header_rect = (SDL_Rect){right_inner_x, right_y, right_inner_w, INSPECTOR_WAVE_HEADER_HEIGHT};
    right_y += INSPECTOR_WAVE_HEADER_HEIGHT + INSPECTOR_WAVE_SECTION_GAP;
    layout->right_waveform_rect = (SDL_Rect){right_inner_x, right_y, right_inner_w, INSPECTOR_WAVE_HEIGHT};
    right_y += INSPECTOR_WAVE_HEIGHT + INSPECTOR_WAVE_SECTION_GAP;
    int detail_h = layout->right_rect.y + layout->right_rect.h - INSPECTOR_WAVE_HEADER_PAD - right_y;
    if (detail_h < 0) detail_h = 0;
    layout->right_detail_rect = (SDL_Rect){right_inner_x, right_y, right_inner_w, detail_h};

    int mode_w = 72;
    int mode_h = INSPECTOR_WAVE_HEADER_HEIGHT - 6;
    int mode_y = layout->right_header_rect.y + (layout->right_header_rect.h - mode_h) / 2;
    int mode_x = layout->right_header_rect.x + layout->right_header_rect.w - mode_w * 2 - INSPECTOR_PRESET_GAP;
    if (mode_x < layout->right_header_rect.x) mode_x = layout->right_header_rect.x;
    layout->right_mode_source_rect = (SDL_Rect){mode_x, mode_y, mode_w, mode_h};
    layout->right_mode_clip_rect = (SDL_Rect){mode_x + mode_w + INSPECTOR_PRESET_GAP, mode_y, mode_w, mode_h};

    int label_w = (int)(left_w * 0.38f);
    if (label_w < 80) label_w = 80;
    int label_x = layout->left_rect.x;
    int value_x = label_x + label_w + 8;
    int value_w = left_w - label_w - 8;
    if (value_w < 0) value_w = 0;

    int y = layout->left_rect.y;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_NAME, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_SOURCE, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_FORMAT, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + section_gap;

    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_TIMELINE_START, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_TIMELINE_END, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_TIMELINE_LENGTH, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_SOURCE_START, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_SOURCE_END, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_PLAYBACK_RATE, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + section_gap;

    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_GAIN, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_FADE_IN, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_FADE_OUT, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + section_gap;

    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_PHASE, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_NORMALIZE, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_REVERSE, label_x, label_w, value_x, value_w, y, row_h);

    layout->name_rect = layout->rows[CLIP_INSPECTOR_ROW_NAME].value_rect;
    layout->name_rect.h = row_h;

    SDL_Rect gain_row = layout->rows[CLIP_INSPECTOR_ROW_GAIN].value_rect;
    SDL_Rect fade_in_row = layout->rows[CLIP_INSPECTOR_ROW_FADE_IN].value_rect;
    SDL_Rect fade_out_row = layout->rows[CLIP_INSPECTOR_ROW_FADE_OUT].value_rect;
    int slider_y_offset = (row_h - slider_h) / 2;
    layout->gain_track_rect = (SDL_Rect){gain_row.x, gain_row.y + slider_y_offset, gain_row.w, slider_h};
    layout->gain_fill_rect = layout->gain_track_rect;
    layout->gain_handle_rect = layout->gain_track_rect;
    layout->fade_in_track_rect = (SDL_Rect){fade_in_row.x, fade_in_row.y + slider_y_offset, fade_in_row.w, slider_h};
    layout->fade_in_fill_rect = layout->fade_in_track_rect;
    layout->fade_in_handle_rect = layout->fade_in_track_rect;
    layout->fade_out_track_rect = (SDL_Rect){fade_out_row.x, fade_out_row.y + slider_y_offset, fade_out_row.w, slider_h};
    layout->fade_out_fill_rect = layout->fade_out_track_rect;
    layout->fade_out_handle_rect = layout->fade_out_track_rect;

}

// Draws a label/value row with clipped text.
static void clip_inspector_draw_row(SDL_Renderer* renderer,
                                    const ClipInspectorRow* row,
                                    const char* label_text,
                                    const char* value_text,
                                    SDL_Color label_color,
                                    SDL_Color value_color) {
    if (!renderer || !row || !label_text || !value_text) {
        return;
    }
    int label_y = row->label_rect.y + (row->label_rect.h - ui_font_line_height(INSPECTOR_LABEL_SCALE)) / 2;
    int value_y = row->value_rect.y + (row->value_rect.h - ui_font_line_height(INSPECTOR_VALUE_SCALE)) / 2;
    ui_draw_text_clipped(renderer,
                         row->label_rect.x,
                         label_y,
                         label_text,
                         label_color,
                         INSPECTOR_LABEL_SCALE,
                         row->label_rect.w);
    ui_draw_text_clipped(renderer,
                         row->value_rect.x,
                         value_y,
                         value_text,
                         value_color,
                         INSPECTOR_VALUE_SCALE,
                         row->value_rect.w);
}

void clip_inspector_render(SDL_Renderer* renderer, AppState* state, const ClipInspectorLayout* layout) {
    if (!renderer || !state || !layout) {
        return;
    }

    SDL_Color label = {180, 180, 190, 255};
    SDL_Color value = {220, 220, 230, 255};
    SDL_Color muted = {140, 140, 150, 255};
    SDL_Color box_bg = {34, 36, 44, 255};
    SDL_Color box_border = {80, 85, 100, 255};
    SDL_Color panel_bg = {26, 28, 34, 255};
    SDL_Color panel_border = {70, 75, 92, 255};
    SDL_Color wave_color = {120, 140, 170, 200};

    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || state->inspector.track_index < 0 || state->inspector.track_index >= track_count) {
        ui_draw_text(renderer, layout->panel_rect.x + 16, layout->panel_rect.y + 48, "Clip unavailable.", label, 1.0f);
        return;
    }
    const EngineTrack* track = &tracks[state->inspector.track_index];
    if (!track || state->inspector.clip_index < 0 || state->inspector.clip_index >= track->clip_count) {
        ui_draw_text(renderer, layout->panel_rect.x + 16, layout->panel_rect.y + 48, "Clip unavailable.", label, 1.0f);
        return;
    }

    const EngineClip* clip = &track->clips[state->inspector.clip_index];
    EngineRuntimeConfig runtime_cfg = clip_inspector_active_config(state);

    char name_buf[ENGINE_CLIP_NAME_MAX];
    const char* display_name = inspector_clip_display_name(state, clip, name_buf, sizeof(name_buf));
    if (state->inspector.editing_name) {
        display_name = state->inspector.name;
    }

    char source_buf[ENGINE_CLIP_PATH_MAX];
    const char* source_name = "";
    const char* source_path = NULL;
    const char* media_id = engine_clip_get_media_id(clip);
    if (state && media_id && media_id[0] != '\0') {
        const MediaRegistryEntry* entry = media_registry_find_by_id(&state->media_registry, media_id);
        if (entry && entry->path[0] != '\0') {
            source_path = entry->path;
            source_name = inspector_basename(entry->path, source_buf, sizeof(source_buf));
        }
    }
    if (source_name[0] == '\0') {
        const char* media_path = engine_clip_get_media_path(clip);
        if (media_path && media_path[0] != '\0') {
            source_path = media_path;
            source_name = inspector_basename(media_path, source_buf, sizeof(source_buf));
        }
    }
    if (source_name[0] == '\0') {
        source_name = "(no source)";
    }

    int sample_rate = 0;
    int channels = 0;
    if (clip->media) {
        sample_rate = clip->media->sample_rate;
        channels = clip->media->channels;
    } else if (clip->source) {
        sample_rate = clip->source->sample_rate;
        channels = clip->source->channels;
    }
    if (sample_rate <= 0) {
        sample_rate = runtime_cfg.sample_rate;
    }

    char format_line[64];
    const char* channel_label = "Unknown";
    if (channels == 1) channel_label = "Mono";
    else if (channels == 2) channel_label = "Stereo";
    else if (channels > 2) channel_label = "Multi";
    if (sample_rate > 0) {
        snprintf(format_line, sizeof(format_line), "%.1fkHz 32-bit float %s",
                 (double)sample_rate / 1000.0,
                 channel_label);
    } else {
        snprintf(format_line, sizeof(format_line), "32-bit float %s", channel_label);
    }

    uint64_t total_frames = 0;
    if (clip->media && clip->media->frame_count > 0) {
        total_frames = clip->media->frame_count;
    } else if (state->engine) {
        total_frames = engine_clip_get_total_frames(state->engine,
                                                    state->inspector.track_index,
                                                    state->inspector.clip_index);
    }

    uint64_t clip_frames = clip->duration_frames;
    if (clip_frames == 0 && total_frames > clip->offset_frames) {
        clip_frames = total_frames - clip->offset_frames;
    }
    if (clip_frames == 0 && clip->sampler) {
        clip_frames = engine_sampler_get_frame_count(clip->sampler);
    }
    if (clip_frames == 0) {
        clip_frames = 1;
    }

    double sr = runtime_cfg.sample_rate > 0 ? (double)runtime_cfg.sample_rate : 48000.0;
    double timeline_start_sec = (double)clip->timeline_start_frames / sr;
    double timeline_length_sec = (double)clip_frames / sr;
    double timeline_end_sec = timeline_start_sec + timeline_length_sec;
    double source_start_sec = (double)clip->offset_frames / sr;
    double source_end_sec = source_start_sec + timeline_length_sec;
    if (total_frames > 0) {
        double total_sec = (double)total_frames / sr;
        if (source_end_sec > total_sec) source_end_sec = total_sec;
    }

    char line[128];
    char timeline_start_line[32];
    char timeline_end_line[32];
    char timeline_length_line[32];
    char source_start_line[32];
    char source_end_line[32];
    snprintf(timeline_start_line, sizeof(timeline_start_line), "%.3fs", timeline_start_sec);
    snprintf(timeline_end_line, sizeof(timeline_end_line), "%.3fs", timeline_end_sec);
    snprintf(timeline_length_line, sizeof(timeline_length_line), "%.3fs", timeline_length_sec);
    snprintf(source_start_line, sizeof(source_start_line), "%.3fs", source_start_sec);
    snprintf(source_end_line, sizeof(source_end_line), "%.3fs", source_end_sec);

    char playback_rate_line[16];
    float playback_rate = state->inspector.playback_rate;
    if (playback_rate <= 0.0f) playback_rate = 1.0f;
    snprintf(playback_rate_line, sizeof(playback_rate_line), "%.2fx", playback_rate);

    const char* timeline_start_text = state->inspector.edit.editing_timeline_start
                                          ? state->inspector.edit.timeline_start
                                          : timeline_start_line;
    const char* timeline_end_text = state->inspector.edit.editing_timeline_end
                                        ? state->inspector.edit.timeline_end
                                        : timeline_end_line;
    const char* timeline_length_text = state->inspector.edit.editing_timeline_length
                                           ? state->inspector.edit.timeline_length
                                           : timeline_length_line;
    const char* source_start_text = state->inspector.edit.editing_source_start
                                        ? state->inspector.edit.source_start
                                        : source_start_line;
    const char* source_end_text = state->inspector.edit.editing_source_end
                                      ? state->inspector.edit.source_end
                                      : source_end_line;
    const char* playback_rate_text = state->inspector.edit.editing_playback_rate
                                         ? state->inspector.edit.playback_rate
                                         : playback_rate_line;

    float gain_value = clip->gain;
    if (state->inspector.adjusting_gain) {
        gain_value = state->inspector.gain;
    }
    if (gain_value < INSPECTOR_GAIN_MIN) gain_value = INSPECTOR_GAIN_MIN;
    if (gain_value > INSPECTOR_GAIN_MAX) gain_value = INSPECTOR_GAIN_MAX;
    float gain_db = 20.0f * log10f(gain_value > 0.000001f ? gain_value : 0.000001f);
    snprintf(line, sizeof(line), "Gain %.1f dB", gain_db);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_GAIN], line, "", label, muted);

    uint64_t fade_in_frames = state->inspector.adjusting_fade_in ? state->inspector.fade_in_frames : clip->fade_in_frames;
    uint64_t fade_out_frames = state->inspector.adjusting_fade_out ? state->inspector.fade_out_frames : clip->fade_out_frames;
    if (fade_in_frames > clip_frames) fade_in_frames = clip_frames;
    if (fade_out_frames > clip_frames) fade_out_frames = clip_frames;

    float fade_in_ratio = (float)fade_in_frames / (float)clip_frames;
    float fade_out_ratio = (float)fade_out_frames / (float)clip_frames;

    float sample_rate_f = runtime_cfg.sample_rate > 0 ? (float)runtime_cfg.sample_rate : 48000.0f;
    float fade_in_ms = (float)fade_in_frames * 1000.0f / sample_rate_f;
    float fade_out_ms = (float)fade_out_frames * 1000.0f / sample_rate_f;
    snprintf(line, sizeof(line), "Fade In %.0f ms", fade_in_ms);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_FADE_IN], line, "", label, muted);
    snprintf(line, sizeof(line), "Fade Out %.0f ms", fade_out_ms);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_FADE_OUT], line, "", label, muted);

    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_NAME], "Name", "", label, value);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_SOURCE], "Source", source_name, label, value);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_FORMAT], "Format", format_line, label, value);
    if (state->inspector.edit.editing_timeline_start) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_START].value_rect);
    }
    if (state->inspector.edit.editing_timeline_end) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_END].value_rect);
    }
    if (state->inspector.edit.editing_timeline_length) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_LENGTH].value_rect);
    }
    if (state->inspector.edit.editing_source_start) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_SOURCE_START].value_rect);
    }
    if (state->inspector.edit.editing_source_end) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_SOURCE_END].value_rect);
    }
    if (state->inspector.edit.editing_playback_rate) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_PLAYBACK_RATE].value_rect);
    }

    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_START], "Timeline Start", timeline_start_text, label, value);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_END], "Timeline End", timeline_end_text, label, value);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_LENGTH], "Timeline Length", timeline_length_text, label, value);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_SOURCE_START], "Source Start", source_start_text, label, value);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_SOURCE_END], "Source End", source_end_text, label, value);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_PLAYBACK_RATE], "Playback Rate", playback_rate_text, label, value);

    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_PHASE], "Phase Invert", "", label, muted);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_NORMALIZE], "Normalize", "", label, muted);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_REVERSE], "Reverse", "", label, muted);

    SDL_SetRenderDrawColor(renderer, box_bg.r, box_bg.g, box_bg.b, box_bg.a);
    SDL_RenderFillRect(renderer, &layout->name_rect);
    SDL_SetRenderDrawColor(renderer, box_border.r, box_border.g, box_border.b, box_border.a);
    SDL_RenderDrawRect(renderer, &layout->name_rect);
    int name_y = layout->name_rect.y + (layout->name_rect.h - ui_font_line_height(INSPECTOR_VALUE_SCALE)) / 2;
    int name_available = layout->name_rect.w - 12;
    if (name_available < 0) name_available = 0;
    if (state->inspector.editing_name) {
        char visible_name[ENGINE_CLIP_NAME_MAX + 8];
        int visible_start = 0;
        int visible_end = 0;
        int left_ellipsis_w = 0;
        clip_inspector_build_name_view(state->inspector.name,
                                       state->inspector.name_scroll,
                                       name_available,
                                       INSPECTOR_VALUE_SCALE,
                                       visible_name,
                                       sizeof(visible_name),
                                       &visible_start,
                                       &visible_end,
                                       &left_ellipsis_w);
        ui_draw_text_clipped(renderer,
                             layout->name_rect.x + 6,
                             name_y,
                             visible_name,
                             value,
                             INSPECTOR_VALUE_SCALE,
                             name_available);
        int cursor = state->inspector.name_cursor;
        if (cursor < visible_start) {
            cursor = visible_start;
        }
        if (cursor > visible_end) {
            cursor = visible_end;
        }
        char scratch[ENGINE_CLIP_NAME_MAX];
        int count = cursor - visible_start;
        if (count < 0) count = 0;
        if (count >= (int)sizeof(scratch)) count = (int)sizeof(scratch) - 1;
        memcpy(scratch, state->inspector.name + visible_start, (size_t)count);
        scratch[count] = '\0';
        int caret_x = layout->name_rect.x + 6 + left_ellipsis_w + ui_measure_text_width(scratch, INSPECTOR_VALUE_SCALE);
        int caret_y = layout->name_rect.y + 2;
        SDL_SetRenderDrawColor(renderer, 220, 220, 240, 255);
        SDL_RenderDrawLine(renderer, caret_x, caret_y, caret_x, caret_y + layout->name_rect.h - 4);
    } else {
        ui_draw_text_clipped(renderer,
                             layout->name_rect.x + 6,
                             name_y,
                             display_name,
                             value,
                             INSPECTOR_VALUE_SCALE,
                             name_available);
    }

    const SDL_Rect* edit_rect = NULL;
    const char* edit_text = NULL;
    if (state->inspector.edit.editing_timeline_start) {
        edit_rect = &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_START].value_rect;
        edit_text = state->inspector.edit.timeline_start;
    } else if (state->inspector.edit.editing_timeline_end) {
        edit_rect = &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_END].value_rect;
        edit_text = state->inspector.edit.timeline_end;
    } else if (state->inspector.edit.editing_timeline_length) {
        edit_rect = &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_LENGTH].value_rect;
        edit_text = state->inspector.edit.timeline_length;
    } else if (state->inspector.edit.editing_source_start) {
        edit_rect = &layout->rows[CLIP_INSPECTOR_ROW_SOURCE_START].value_rect;
        edit_text = state->inspector.edit.source_start;
    } else if (state->inspector.edit.editing_source_end) {
        edit_rect = &layout->rows[CLIP_INSPECTOR_ROW_SOURCE_END].value_rect;
        edit_text = state->inspector.edit.source_end;
    } else if (state->inspector.edit.editing_playback_rate) {
        edit_rect = &layout->rows[CLIP_INSPECTOR_ROW_PLAYBACK_RATE].value_rect;
        edit_text = state->inspector.edit.playback_rate;
    }
    if (edit_rect && edit_text) {
        int caret_x = edit_rect->x + 6 + ui_measure_text_width(edit_text, INSPECTOR_VALUE_SCALE);
        int caret_y = edit_rect->y + 2;
        SDL_SetRenderDrawColor(renderer, 220, 220, 240, 255);
        SDL_RenderDrawLine(renderer, caret_x, caret_y, caret_x, caret_y + edit_rect->h - 4);
    }

    draw_slider(renderer, &layout->gain_track_rect, (gain_value - INSPECTOR_GAIN_MIN) / (INSPECTOR_GAIN_MAX - INSPECTOR_GAIN_MIN));
    draw_slider(renderer, &layout->fade_in_track_rect, fade_in_ratio);
    draw_slider(renderer, &layout->fade_out_track_rect, fade_out_ratio);

    if (layout->rows[CLIP_INSPECTOR_ROW_PHASE].value_rect.w > 0) {
        SDL_Rect phase_rect = layout->rows[CLIP_INSPECTOR_ROW_PHASE].value_rect;
        int button_w = (phase_rect.w - INSPECTOR_PRESET_GAP) / 2;
        int button_h = phase_rect.h - 4;
        if (button_w < 0) button_w = 0;
        SDL_Rect left_btn = {phase_rect.x, phase_rect.y + 2, button_w, button_h};
        SDL_Rect right_btn = {phase_rect.x + button_w + INSPECTOR_PRESET_GAP, phase_rect.y + 2, button_w, button_h};
        draw_button(renderer, &left_btn, "L", state->inspector.phase_invert_l);
        draw_button(renderer, &right_btn, "R", state->inspector.phase_invert_r);
    }
    if (layout->rows[CLIP_INSPECTOR_ROW_NORMALIZE].value_rect.w > 0) {
        SDL_Rect toggle_rect = layout->rows[CLIP_INSPECTOR_ROW_NORMALIZE].value_rect;
        toggle_rect.h -= 4;
        toggle_rect.y += 2;
        toggle_rect.w = 52;
        draw_button(renderer, &toggle_rect, state->inspector.normalize ? "On" : "Off", state->inspector.normalize);
    }
    if (layout->rows[CLIP_INSPECTOR_ROW_REVERSE].value_rect.w > 0) {
        SDL_Rect toggle_rect = layout->rows[CLIP_INSPECTOR_ROW_REVERSE].value_rect;
        toggle_rect.h -= 4;
        toggle_rect.y += 2;
        toggle_rect.w = 52;
        draw_button(renderer, &toggle_rect, state->inspector.reverse ? "On" : "Off", state->inspector.reverse);
    }

    if (layout->right_rect.w > 0 && layout->right_rect.h > 0) {
        SDL_SetRenderDrawColor(renderer, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
        SDL_RenderFillRect(renderer, &layout->right_rect);
        SDL_SetRenderDrawColor(renderer, panel_border.r, panel_border.g, panel_border.b, panel_border.a);
        SDL_RenderDrawRect(renderer, &layout->right_rect);

        if (layout->right_header_rect.w > 0 && layout->right_header_rect.h > 0) {
            SDL_Rect header_bg = layout->right_header_rect;
            header_bg.x -= 4;
            header_bg.w += 8;
            SDL_SetRenderDrawColor(renderer, 32, 34, 40, 255);
            SDL_RenderFillRect(renderer, &header_bg);
            bool view_source = state->inspector.waveform.view_source;
            ui_draw_text(renderer,
                         layout->right_header_rect.x,
                         layout->right_header_rect.y + 4,
                         view_source ? "Source View" : "Clip View",
                         label,
                         1.0f);
            draw_button(renderer, &layout->right_mode_source_rect, "Source", view_source);
            draw_button(renderer, &layout->right_mode_clip_rect, "Clip", !view_source);
        }

        if (layout->right_waveform_rect.w > 0 && layout->right_waveform_rect.h > 0) {
            SDL_SetRenderDrawColor(renderer, 22, 24, 30, 255);
            SDL_RenderFillRect(renderer, &layout->right_waveform_rect);
            SDL_SetRenderDrawColor(renderer, panel_border.r, panel_border.g, panel_border.b, panel_border.a);
            SDL_RenderDrawRect(renderer, &layout->right_waveform_rect);

            if (clip->media && clip->media->samples && clip->media->frame_count > 0) {
                bool view_source = state->inspector.waveform.view_source;
                uint64_t total = clip->media->frame_count;
                uint64_t view_start = 0;
                uint64_t view_frames = 0;
                if (clip_inspector_get_waveform_view(state, clip, clip_frames, &view_start, &view_frames)) {
                    if (source_path && source_path[0] != '\0') {
                        waveform_render_view(renderer,
                                             &state->waveform_cache,
                                             clip->media,
                                             source_path,
                                             &layout->right_waveform_rect,
                                             view_start,
                                             view_frames,
                                             wave_color);
                    }

                    if (view_source) {
                        uint64_t window_start = clip->offset_frames;
                        uint64_t window_end = clip->offset_frames + clip_frames;
                        if (window_end > total) window_end = total;
                        if (window_start < window_end) {
                            double start_t = (double)(window_start > view_start ? window_start - view_start : 0) / (double)view_frames;
                            double end_t = (double)(window_end > view_start ? window_end - view_start : 0) / (double)view_frames;
                            if (start_t < 0.0) start_t = 0.0;
                            if (end_t > 1.0) end_t = 1.0;
                            if (end_t > start_t) {
                                int hx = layout->right_waveform_rect.x + (int)llround(start_t * (double)layout->right_waveform_rect.w);
                                int hw = (int)llround((end_t - start_t) * (double)layout->right_waveform_rect.w);
                                if (hw < 1) hw = 1;
                                SDL_Rect highlight = {hx, layout->right_waveform_rect.y, hw, layout->right_waveform_rect.h};
                                ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
                                SDL_SetRenderDrawColor(renderer, 120, 160, 220, 40);
                                SDL_RenderFillRect(renderer, &highlight);
                                SDL_SetRenderDrawColor(renderer, 140, 180, 240, 120);
                                SDL_RenderDrawRect(renderer, &highlight);
                                ui_set_blend_mode(renderer, SDL_BLENDMODE_NONE);
                            }
                        }
                    }

                    ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
                    uint64_t clip_start = clip->offset_frames;
                    uint64_t clip_end = clip_start + clip_frames;
                    bool fade_in_selected = state->inspector.adjusting_fade_in || state->inspector.fade_in_selected;
                    bool fade_out_selected = state->inspector.adjusting_fade_out || state->inspector.fade_out_selected;
                    Uint8 fade_in_alpha = fade_in_selected ? 44 : 28;
                    Uint8 fade_out_alpha = fade_out_selected ? 44 : 28;
                    if (fade_in_frames > 0) {
                        clip_inspector_draw_fade_overlay(renderer,
                                                         &layout->right_waveform_rect,
                                                         view_start,
                                                         view_frames,
                                                         clip_start,
                                                         fade_in_frames,
                                                         clip->fade_in_curve,
                                                         true,
                                                         fade_in_alpha);
                    }
                    if (fade_out_frames > 0 && clip_end > 0) {
                        uint64_t fade_start = clip_end > fade_out_frames ? clip_end - fade_out_frames : clip_start;
                        clip_inspector_draw_fade_overlay(renderer,
                                                         &layout->right_waveform_rect,
                                                         view_start,
                                                         view_frames,
                                                         fade_start,
                                                         fade_out_frames,
                                                         clip->fade_out_curve,
                                                         false,
                                                         fade_out_alpha);
                    }
                    ui_set_blend_mode(renderer, SDL_BLENDMODE_NONE);

                    if (state->timeline_automation_mode) {
                        clip_inspector_draw_automation(renderer,
                                                       &layout->right_waveform_rect,
                                                       view_start,
                                                       view_frames,
                                                       clip,
                                                       clip_frames,
                                                       &state->automation_ui,
                                                       state->automation_ui.target,
                                                       state->inspector.track_index,
                                                       state->inspector.clip_index);
                    }

                    if (state->engine) {
                        uint64_t transport_frame = engine_get_transport_frame(state->engine);
                        uint64_t clip_start_frame = clip->timeline_start_frames;
                        uint64_t clip_end_frame = clip_start_frame + clip_frames;
                        if (transport_frame >= clip_start_frame && transport_frame <= clip_end_frame) {
                            uint64_t local_frame = clip->offset_frames + (transport_frame - clip_start_frame);
                            if (local_frame >= view_start && local_frame <= view_start + view_frames) {
                                double t = (double)(local_frame - view_start) / (double)view_frames;
                                int px = layout->right_waveform_rect.x + (int)llround(t * (double)layout->right_waveform_rect.w);
                                SDL_SetRenderDrawColor(renderer, 255, 210, 110, 220);
                                SDL_RenderDrawLine(renderer,
                                                   px,
                                                   layout->right_waveform_rect.y,
                                                   px,
                                                   layout->right_waveform_rect.y + layout->right_waveform_rect.h);
                            }
                        }
                    }
                }
            } else {
                ui_draw_text(renderer,
                             layout->right_waveform_rect.x + 8,
                             layout->right_waveform_rect.y + 8,
                             "No waveform loaded.",
                             muted,
                             1.0f);
            }
        }

        if (layout->right_detail_rect.w > 0 && layout->right_detail_rect.h > 0) {
            SDL_SetRenderDrawColor(renderer, 24, 26, 32, 255);
            SDL_RenderFillRect(renderer, &layout->right_detail_rect);
            SDL_SetRenderDrawColor(renderer, panel_border.r, panel_border.g, panel_border.b, panel_border.a);
            SDL_RenderDrawRect(renderer, &layout->right_detail_rect);
            ui_draw_text(renderer,
                         layout->right_detail_rect.x + 8,
                         layout->right_detail_rect.y + 8,
                         "Clip overlays (coming soon)",
                         muted,
                         1.0f);
        }
    }
}
