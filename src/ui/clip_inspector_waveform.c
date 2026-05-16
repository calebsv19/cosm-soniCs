#include "ui/clip_inspector_waveform.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/font.h"
#include "ui/kit_viz_waveform_adapter.h"
#include "ui/render_utils.h"
#include "ui/waveform_render.h"

#include <math.h>

static void clip_inspector_automation_colors(SDL_Color* line_color,
                                             SDL_Color* point_color,
                                             SDL_Color* point_selected_color) {
    DawThemePalette theme = {0};
    if (daw_shared_theme_resolve_palette(&theme)) {
        if (line_color) {
            *line_color = theme.accent_primary;
            line_color->a = 220;
        }
        if (point_color) {
            *point_color = theme.text_muted;
        }
        if (point_selected_color) {
            *point_selected_color = theme.text_primary;
        }
        return;
    }
    if (line_color) *line_color = (SDL_Color){170, 210, 230, 220};
    if (point_color) *point_color = (SDL_Color){120, 150, 170, 255};
    if (point_selected_color) *point_selected_color = (SDL_Color){230, 240, 255, 255};
}

static const char* waveform_result_label(DawKitVizWaveformResult result) {
    switch (result) {
        case DAW_KIT_VIZ_WAVEFORM_RENDERED: return "kit_viz";
        case DAW_KIT_VIZ_WAVEFORM_INVALID_REQUEST: return "fallback:invalid";
        case DAW_KIT_VIZ_WAVEFORM_MISSING_CACHE: return "fallback:cache";
        case DAW_KIT_VIZ_WAVEFORM_SAMPLING_FAILED: return "fallback:sample";
        default: return "fallback:unknown";
    }
}

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
    if (engine_automation_target_is_instrument_param(target) && clip->kind != ENGINE_CLIP_KIND_MIDI) {
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
    SDL_Color line_color = {0};
    SDL_Color point_color = {0};
    SDL_Color point_selected_color = {0};
    clip_inspector_automation_colors(&line_color, &point_color, &point_selected_color);
    SDL_SetRenderDrawColor(renderer, line_color.r, line_color.g, line_color.b, line_color.a);
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
                SDL_SetRenderDrawColor(renderer,
                                       point_selected_color.r,
                                       point_selected_color.g,
                                       point_selected_color.b,
                                       point_selected_color.a);
            } else {
                SDL_SetRenderDrawColor(renderer,
                                       point_color.r,
                                       point_color.g,
                                       point_color.b,
                                       point_color.a);
            }
            SDL_RenderFillRect(renderer, &dot);
        }
    }
}

void clip_inspector_render_waveform_panel(SDL_Renderer* renderer,
                                          AppState* state,
                                          const ClipInspectorLayout* layout,
                                          const EngineClip* clip,
                                          uint64_t clip_frames,
                                          uint64_t fade_in_frames,
                                          uint64_t fade_out_frames,
                                          const char* source_path,
                                          const DawThemePalette* theme) {
    if (!renderer || !state || !layout || !clip || !theme ||
        layout->right_waveform_rect.w <= 0 || layout->right_waveform_rect.h <= 0) {
        return;
    }

    SDL_SetRenderDrawColor(renderer,
                           theme->timeline_fill.r,
                           theme->timeline_fill.g,
                           theme->timeline_fill.b,
                           theme->timeline_fill.a);
    SDL_RenderFillRect(renderer, &layout->right_waveform_rect);
    SDL_SetRenderDrawColor(renderer,
                           theme->pane_border.r,
                           theme->pane_border.g,
                           theme->pane_border.b,
                           theme->pane_border.a);
    SDL_RenderDrawRect(renderer, &layout->right_waveform_rect);

    if (!clip->media || clip->media->frame_count == 0) {
        ui_draw_text(renderer,
                     layout->right_waveform_rect.x + 8,
                     layout->right_waveform_rect.y + 8,
                     "No waveform loaded.",
                     theme->text_muted,
                     1.0f);
        return;
    }

    bool view_source = state->inspector.waveform.view_source;
    uint64_t total = clip->media->frame_count;
    uint64_t view_start = 0;
    uint64_t view_frames = 0;
    DawKitVizWaveformResult waveform_result = DAW_KIT_VIZ_WAVEFORM_INVALID_REQUEST;
    if (!clip_inspector_get_waveform_view(state, clip, clip_frames, &view_start, &view_frames)) {
        return;
    }

    if (source_path && source_path[0] != '\0') {
        bool rendered = false;
        if (state->inspector.waveform.use_kit_viz_waveform) {
            DawKitVizWaveformRequest request = {
                .renderer = renderer,
                .cache = &state->waveform_cache,
                .clip = clip->media,
                .source_path = source_path,
                .target_rect = &layout->right_waveform_rect,
                .view_start_frame = view_start,
                .view_frame_count = view_frames,
                .color = theme->slider_handle,
            };
            waveform_result = daw_kit_viz_render_waveform_ex(&request);
            rendered = (waveform_result == DAW_KIT_VIZ_WAVEFORM_RENDERED);
        } else {
            waveform_result = DAW_KIT_VIZ_WAVEFORM_INVALID_REQUEST;
        }
        if (!rendered) {
            (void)waveform_render_view(renderer,
                                       &state->waveform_cache,
                                       clip->media,
                                       source_path,
                                       &layout->right_waveform_rect,
                                       view_start,
                                       view_frames,
                                       theme->slider_handle);
        }

        if (waveform_result != DAW_KIT_VIZ_WAVEFORM_RENDERED) {
            SDL_Color dbg = {130, 140, 160, 255};
            ui_draw_text(renderer,
                         layout->right_waveform_rect.x + 8,
                         layout->right_waveform_rect.y + 8,
                         waveform_result_label(waveform_result),
                         dbg,
                         0.8f);
        }
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
                SDL_SetRenderDrawColor(renderer,
                                       theme->selection_fill.r,
                                       theme->selection_fill.g,
                                       theme->selection_fill.b,
                                       theme->selection_fill.a);
                SDL_RenderFillRect(renderer, &highlight);
                SDL_SetRenderDrawColor(renderer,
                                       theme->accent_primary.r,
                                       theme->accent_primary.g,
                                       theme->accent_primary.b,
                                       theme->accent_primary.a);
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
                SDL_SetRenderDrawColor(renderer,
                                       theme->accent_warning.r,
                                       theme->accent_warning.g,
                                       theme->accent_warning.b,
                                       theme->accent_warning.a);
                SDL_RenderDrawLine(renderer,
                                   px,
                                   layout->right_waveform_rect.y,
                                   px,
                                   layout->right_waveform_rect.y + layout->right_waveform_rect.h);
            }
        }
    }
}
