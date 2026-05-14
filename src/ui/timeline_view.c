#include "ui/timeline_view.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "audio/media_registry.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/beat_grid.h"
#include "ui/kit_viz_waveform_adapter.h"
#include "ui/shared_theme_font_adapter.h"
#include "ui/time_grid.h"
#include "ui/timeline_waveform.h"
#include "ui/waveform_render.h"
#include "ui/timeline_view_internal.h"
#include "ui/timeline_view_overlays.h"
#include "ui/timeline_view_clip_pass.h"
#include "ui/timeline_view_grid.h"
#include "ui/timeline_view_controls.h"
#include "ui/timeline_view_runtime_overlays.h"
#include "time/tempo.h"
#include "input/timeline/timeline_geometry.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static SDL_Color mix_color(SDL_Color base, SDL_Color accent, float accent_weight) {
    if (accent_weight < 0.0f) {
        accent_weight = 0.0f;
    }
    if (accent_weight > 1.0f) {
        accent_weight = 1.0f;
    }
    float base_weight = 1.0f - accent_weight;
    SDL_Color out = {
        (Uint8)lroundf((float)base.r * base_weight + (float)accent.r * accent_weight),
        (Uint8)lroundf((float)base.g * base_weight + (float)accent.g * accent_weight),
        (Uint8)lroundf((float)base.b * base_weight + (float)accent.b * accent_weight),
        (Uint8)lroundf((float)base.a * base_weight + (float)accent.a * accent_weight)
    };
    return out;
}

static void resolve_timeline_theme(TimelineTheme* out_theme) {
    DawThemePalette theme = {0};
    if (!out_theme) {
        return;
    }
    if (daw_shared_theme_resolve_palette(&theme)) {
        out_theme->bg = theme.timeline_fill;
        out_theme->header_fill = theme.control_fill;
        out_theme->header_border = theme.pane_border;
        out_theme->text = theme.text_primary;
        out_theme->text_muted = theme.text_muted;
        out_theme->button_fill = theme.control_fill;
        out_theme->button_hover_fill = theme.control_hover_fill;
        out_theme->button_disabled_fill = theme.slider_track;
        out_theme->button_border = theme.control_border;
        out_theme->lane_header_fill = theme.control_fill;
        out_theme->lane_header_fill_active = theme.control_hover_fill;
        out_theme->lane_header_border = theme.control_border;
        out_theme->clip_fill = mix_color(theme.timeline_fill, theme.selection_fill, 0.18f);
        out_theme->clip_fill.a = 238;
        out_theme->clip_border = theme.timeline_border;
        out_theme->clip_border_selected = theme.slider_handle;
        out_theme->clip_border_selected.a = 220;
        out_theme->clip_text = theme.text_primary;
        out_theme->waveform = theme.slider_handle;
        out_theme->waveform.a = 200;
        out_theme->loop_fill = theme.accent_primary;
        out_theme->loop_fill.a = 50;
        out_theme->loop_border = theme.accent_primary;
        out_theme->loop_border.a = 180;
        out_theme->loop_handle_start = theme.slider_handle;
        out_theme->loop_handle_end = theme.accent_primary;
        out_theme->loop_handle_border = theme.timeline_border;
        out_theme->loop_label = theme.text_primary;
        out_theme->playhead = theme.accent_error;
        out_theme->toggle_active_loop = theme.accent_warning;
        out_theme->toggle_active_snap = theme.accent_primary;
        out_theme->toggle_active_auto = theme.control_active_fill;
        out_theme->toggle_active_tempo = theme.control_active_fill;
        out_theme->toggle_active_label = theme.slider_handle;
        return;
    }
    *out_theme = (TimelineTheme){
        .bg = {38, 44, 58, 255},
        .header_fill = {28, 32, 40, 255},
        .header_border = {50, 55, 70, 255},
        .text = {220, 220, 230, 255},
        .text_muted = {140, 140, 150, 255},
        .button_fill = {50, 58, 70, 255},
        .button_hover_fill = {72, 82, 100, 255},
        .button_disabled_fill = {34, 36, 40, 255},
        .button_border = {90, 95, 110, 255},
        .lane_header_fill = {30, 30, 38, 255},
        .lane_header_fill_active = {62, 62, 86, 255},
        .lane_header_border = {70, 70, 90, 255},
        .clip_fill = {36, 40, 52, 230},
        .clip_border = {20, 20, 26, 255},
        .clip_border_selected = {200, 220, 255, 220},
        .clip_text = {200, 200, 210, 255},
        .waveform = {120, 140, 170, 200},
        .loop_fill = {60, 120, 140, 50},
        .loop_border = {90, 170, 190, 180},
        .loop_handle_start = {170, 220, 200, 255},
        .loop_handle_end = {160, 190, 230, 255},
        .loop_handle_border = {40, 70, 90, 255},
        .loop_label = {210, 220, 235, 255},
        .playhead = {240, 110, 110, 255},
        .toggle_active_loop = {130, 200, 180, 255},
        .toggle_active_snap = {130, 160, 210, 255},
        .toggle_active_auto = {110, 150, 200, 255},
        .toggle_active_tempo = {120, 170, 210, 255},
        .toggle_active_label = {150, 170, 220, 255}
    };
}

#define CLIP_COLOR_R 36
#define CLIP_COLOR_G 40
#define CLIP_COLOR_B 52
#define TRACK_BG_R 38
#define TRACK_BG_G 44
#define TRACK_BG_B 58
#define CLIP_HANDLE_WIDTH 8

#define WAVEFORM_COLOR_R 120
#define WAVEFORM_COLOR_G 140
#define WAVEFORM_COLOR_B 170

static inline float clamp_float(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

void timeline_view_render(SDL_Renderer* renderer, const SDL_Rect* rect, AppState* state) {
    TimelineTheme theme = {0};
    if (!renderer || !rect || !state || !state->engine) {
        return;
    }
    resolve_timeline_theme(&theme);

    SDL_SetRenderDrawColor(renderer, theme.bg.r, theme.bg.g, theme.bg.b, theme.bg.a);
    SDL_RenderFillRect(renderer, rect);

    float visible_seconds = clamp_float(state->timeline_visible_seconds, TIMELINE_MIN_VISIBLE_SECONDS, TIMELINE_MAX_VISIBLE_SECONDS);
    float vertical_scale = clamp_float(state->timeline_vertical_scale, TIMELINE_MIN_VERTICAL_SCALE, TIMELINE_MAX_VERTICAL_SCALE);
    float min_window_start = 0.0f;
    float max_window_start = 0.0f;
    timeline_get_scroll_bounds(state, visible_seconds, &min_window_start, &max_window_start);
    float window_start = clamp_float(state->timeline_window_start_seconds, min_window_start, max_window_start);
    float window_end = window_start + visible_seconds;

    const int controls_height = timeline_view_controls_height_for_width(rect->w);
    const int ruler_height = timeline_view_ruler_height();
    const int track_top_offset = controls_height + ruler_height;
    const int track_y = rect->y + track_top_offset;
    int track_height = (int)(TIMELINE_BASE_TRACK_HEIGHT * vertical_scale);
    if (track_height < 32) {
        track_height = 32;
    }
    const int track_spacing = 12;
    const int header_width = timeline_view_track_header_width();
    const int content_left = rect->x + header_width + TIMELINE_BORDER_MARGIN;
    const int content_right = rect->x + rect->w - TIMELINE_BORDER_MARGIN;
    const int content_width = content_right - content_left;
    if (content_width <= 0) {
        return;
    }

    const Engine* engine = state->engine;
    const EngineTrack* tracks = engine_get_tracks(engine);
    const int track_count = engine_get_track_count(engine);
    const int sample_rate = engine_get_config(engine)->sample_rate;

    SDL_Rect header_rect = {rect->x, rect->y, rect->w, controls_height};
    SDL_SetRenderDrawColor(renderer, theme.header_fill.r, theme.header_fill.g, theme.header_fill.b, theme.header_fill.a);
    SDL_RenderFillRect(renderer, &header_rect);
    SDL_SetRenderDrawColor(renderer, theme.header_border.r, theme.header_border.g, theme.header_border.b, theme.header_border.a);
    SDL_RenderDrawLine(renderer, rect->x, rect->y + controls_height - 1, rect->x + rect->w, rect->y + controls_height - 1);

    TimelineControlsUI* controls = (TimelineControlsUI*)&state->timeline_controls;
    timeline_view_controls_compute_layout(rect->x, rect->y, rect->w, controls);

    bool remove_enabled = track_count > 0;
    bool midi_region_enabled = track_count > 0;
    timeline_view_draw_button(renderer, &controls->add_rect, "+", controls->add_hovered, true, &theme);
    timeline_view_draw_button(renderer, &controls->remove_rect, "-", controls->remove_hovered, remove_enabled, &theme);
    timeline_view_draw_button(renderer, &controls->midi_region_rect, "+ MIDI", controls->midi_region_hovered, midi_region_enabled, &theme);
    timeline_view_draw_button(renderer, &controls->loop_toggle_rect, "LOOP", controls->loop_toggle_hovered, true, &theme);
    timeline_view_draw_button(renderer, &controls->snap_toggle_rect, "SNAP", controls->snap_toggle_hovered, true, &theme);
    timeline_view_draw_button(renderer, &controls->automation_toggle_rect, "AUTO", controls->automation_toggle_hovered, true, &theme);
    const char* target_label = state->automation_ui.target == ENGINE_AUTOMATION_TARGET_PAN ? "PAN" : "VOL";
    timeline_view_draw_button(renderer, &controls->automation_target_rect, target_label, controls->automation_target_hovered, true, &theme);
    timeline_view_draw_button(renderer, &controls->tempo_toggle_rect, "TEMPO", controls->tempo_toggle_hovered, true, &theme);
    timeline_view_draw_button(renderer, &controls->automation_label_toggle_rect, "VAL", controls->automation_label_toggle_hovered, true, &theme);
    if (state->loop_enabled) {
        SDL_SetRenderDrawColor(renderer, theme.toggle_active_loop.r, theme.toggle_active_loop.g, theme.toggle_active_loop.b, theme.toggle_active_loop.a);
        SDL_RenderDrawRect(renderer, &controls->loop_toggle_rect);
    }
    if (state->timeline_snap_enabled) {
        SDL_SetRenderDrawColor(renderer, theme.toggle_active_snap.r, theme.toggle_active_snap.g, theme.toggle_active_snap.b, theme.toggle_active_snap.a);
        SDL_RenderDrawRect(renderer, &controls->snap_toggle_rect);
    }
    if (state->timeline_automation_mode) {
        SDL_SetRenderDrawColor(renderer, theme.toggle_active_auto.r, theme.toggle_active_auto.g, theme.toggle_active_auto.b, theme.toggle_active_auto.a);
        SDL_RenderDrawRect(renderer, &controls->automation_toggle_rect);
    }
    if (state->timeline_tempo_overlay_enabled) {
        SDL_SetRenderDrawColor(renderer, theme.toggle_active_tempo.r, theme.toggle_active_tempo.g, theme.toggle_active_tempo.b, theme.toggle_active_tempo.a);
        SDL_RenderDrawRect(renderer, &controls->tempo_toggle_rect);
    }
    if (state->timeline_automation_labels_enabled) {
        SDL_SetRenderDrawColor(renderer, theme.toggle_active_label.r, theme.toggle_active_label.g, theme.toggle_active_label.b, theme.toggle_active_label.a);
        SDL_RenderDrawRect(renderer, &controls->automation_label_toggle_rect);
    }

    controls->loop_start_rect = (SDL_Rect){0,0,0,0};
    controls->loop_end_rect = (SDL_Rect){0,0,0,0};

    const float pixels_per_second = (float)content_width / visible_seconds;
    int grid_lanes = track_count > 0 ? track_count : 1;

    bool have_loop_controls = state->loop_enabled && sample_rate > 0 && state->loop_end_frame > state->loop_start_frame;
    bool have_loop_region = false;
    SDL_Rect loop_region_rect = {0, 0, 0, 0};
    if (have_loop_controls) {
        float loop_start_sec = (float)state->loop_start_frame / (float)sample_rate;
        float loop_end_sec = (float)state->loop_end_frame / (float)sample_rate;
        float loop_start_local = loop_start_sec - window_start;
        float loop_end_local = loop_end_sec - window_start;
        if (loop_start_local < 0.0f) loop_start_local = 0.0f;
        if (loop_end_local < 0.0f) loop_end_local = 0.0f;
        if (loop_start_local > visible_seconds) loop_start_local = visible_seconds;
        if (loop_end_local > visible_seconds) loop_end_local = visible_seconds;
        int loop_start_x = content_left + (int)roundf(loop_start_local * pixels_per_second);
        int loop_end_x = content_left + (int)roundf(loop_end_local * pixels_per_second);
        if (loop_start_x < content_left) loop_start_x = content_left;
        if (loop_start_x > content_left + content_width) loop_start_x = content_left + content_width;
        if (loop_end_x < content_left) loop_end_x = content_left;
        if (loop_end_x > content_left + content_width) loop_end_x = content_left + content_width;
        if (loop_end_x < loop_start_x) {
            int tmp = loop_end_x;
            loop_end_x = loop_start_x;
            loop_start_x = tmp;
        }
        int top_zone_y = rect->y + controls_height + 2;
        int top_zone_h = ruler_height - 4;
        if (top_zone_h < 10) top_zone_h = 10;
        int handle_width = 10;
        int handle_height = top_zone_h - 4;
        if (handle_height < 8) handle_height = 8;
        int handle_y = top_zone_y + (top_zone_h - handle_height) / 2;
        controls->loop_start_rect = (SDL_Rect){loop_start_x - handle_width / 2, handle_y, handle_width, handle_height};
        controls->loop_end_rect = (SDL_Rect){loop_end_x - handle_width / 2, handle_y, handle_width, handle_height};

        if (loop_end_x > loop_start_x) {
            have_loop_region = true;
            int loop_top = rect->y + controls_height;
            int loop_bottom = track_y + grid_lanes * (track_height + track_spacing) - track_spacing;
            int loop_height = loop_bottom - loop_top;
            if (loop_height < 0) loop_height = 0;
            loop_region_rect = (SDL_Rect){
                loop_start_x,
                loop_top,
                loop_end_x - loop_start_x,
                loop_height
            };
        }
    }

    if (have_loop_region) {
        SDL_Color loop_fill = theme.loop_fill;
        SDL_Color loop_border = theme.loop_border;
        SDL_SetRenderDrawColor(renderer, loop_fill.r, loop_fill.g, loop_fill.b, loop_fill.a);
        SDL_RenderFillRect(renderer, &loop_region_rect);
        SDL_SetRenderDrawColor(renderer, loop_border.r, loop_border.g, loop_border.b, loop_border.a);
        SDL_RenderDrawRect(renderer, &loop_region_rect);
    }

    state->tempo_map.sample_rate = (double)sample_rate;
    BeatGridCache beat_cache;
    TimeGridCache time_cache;
    beat_grid_cache_init(&beat_cache);
    time_grid_cache_init(&time_cache);
    bool beat_mode_ready = state->timeline_view_in_beats
        && state->tempo_map.event_count > 0
        && state->tempo_map.sample_rate > 0.0;
    if (beat_mode_ready) {
        beat_grid_cache_build(&beat_cache,
                              content_left,
                              pixels_per_second,
                              visible_seconds,
                              window_start,
                              state->timeline_show_all_grid_lines,
                              &state->tempo_map,
                              &state->time_signature_map);
    } else {
        time_grid_cache_build(&time_cache,
                              content_left,
                              pixels_per_second,
                              visible_seconds,
                              window_start,
                              state->timeline_show_all_grid_lines);
    }
    for (int lane = 0; lane < grid_lanes; ++lane) {
        int lane_top = track_y + lane * (track_height + track_spacing);
        if (beat_cache.active) {
            beat_grid_cache_draw(renderer,
                                 content_left,
                                 content_width,
                                 lane_top,
                                 track_height,
                                 &beat_cache);
        } else if (time_cache.active) {
            time_grid_cache_draw(renderer,
                                 content_left,
                                 content_width,
                                 lane_top,
                                 track_height,
                                 &time_cache);
        } else {
            timeline_view_draw_grid(renderer,
                                    content_left,
                                    content_width,
                                    lane_top,
                                    track_height,
                                    pixels_per_second,
                                    visible_seconds,
                                    state->timeline_show_all_grid_lines,
                                    window_start,
                                    state->timeline_view_in_beats,
                                    &state->tempo_map,
                                    &state->time_signature_map);
        }
    }
    beat_grid_cache_free(&beat_cache);
    time_grid_cache_free(&time_cache);

    timeline_view_render_track_clip_pass(renderer,
                                         rect,
                                         state,
                                         &theme,
                                         tracks,
                                         track_count,
                                         track_y,
                                         track_height,
                                         track_spacing,
                                         header_width,
                                         content_left,
                                         content_width,
                                         window_start,
                                         window_end,
                                         pixels_per_second,
                                         sample_rate);

    if (state->timeline_tempo_overlay_enabled) {
        SDL_Rect tempo_rect = {0, 0, 0, 0};
        if (timeline_view_compute_tempo_overlay_rect(rect,
                                                     track_y,
                                                     track_height,
                                                     content_left,
                                                     content_width,
                                                     &tempo_rect)) {
            timeline_view_draw_tempo_overlay(renderer,
                                             &tempo_rect,
                                             &state->tempo_map,
                                             state->tempo_overlay_ui.event_index,
                                             window_start,
                                             window_end,
                                             content_left,
                                             (float)pixels_per_second,
                                             state->timeline_automation_labels_enabled);
        }
    }

    if (state->timeline_automation_mode) {
        timeline_view_draw_automation_overlay(renderer,
                                              rect,
                                              track_y,
                                              track_height,
                                              track_spacing,
                                              track_count,
                                              content_left,
                                              content_width);
        if (tracks && track_count > 0 && sample_rate > 0) {
            for (int t = 0; t < track_count; ++t) {
                const EngineTrack* track = &tracks[t];
                int lane_top = track_y + t * (track_height + track_spacing);
                if (!track || track->clip_count <= 0) {
                    continue;
                }
                for (int i = 0; i < track->clip_count; ++i) {
                    const EngineClip* clip = &track->clips[i];
                    if (!clip) {
                        continue;
                    }
                    uint64_t frame_count = clip->duration_frames;
                    uint64_t media_frames = (clip->media && clip->media->frame_count > 0) ? clip->media->frame_count : 0;
                    uint64_t clip_available = media_frames > clip->offset_frames ? media_frames - clip->offset_frames : 0;
                    if (frame_count == 0 || (clip_available > 0 && frame_count > clip_available)) {
                        frame_count = clip_available;
                    }
                    if (frame_count == 0) {
                        continue;
                    }
                    const double clip_sec = (double)frame_count / (double)sample_rate;
                    const double start_sec = (double)clip->timeline_start_frames / (double)sample_rate;
                    int clip_x = content_left + (int)round((start_sec - (double)window_start) * pixels_per_second);
                    int clip_w = (int)round(clip_sec * pixels_per_second);
                    if (clip_w < 4) {
                        clip_w = 4;
                    }
                    SDL_Rect clip_rect = {0, 0, 0, 0};
                    timeline_view_compute_lane_clip_rect(lane_top,
                                                         track_height,
                                                         clip_x,
                                                         clip_w,
                                                         &clip_rect);
                    timeline_view_draw_clip_automation(renderer,
                                                       &clip_rect,
                                                       clip,
                                                       frame_count,
                                                       state->automation_ui.target,
                                                       &state->automation_ui,
                                                       t,
                                                       i,
                                                       sample_rate,
                                                       window_start,
                                                       window_end,
                                                       content_left,
                                                       content_left + content_width,
                                                       (float)pixels_per_second);
                }
            }
        }
    }

    if (have_loop_controls) {
        SDL_Color start_color = controls->adjusting_loop_start ? theme.text : theme.loop_handle_start;
        SDL_Color end_color = controls->adjusting_loop_end ? theme.text : theme.loop_handle_end;
        SDL_Color start_border = (controls->loop_start_hovered || controls->adjusting_loop_start)
                                     ? theme.clip_border_selected
                                     : theme.loop_handle_border;
        SDL_Color end_border = (controls->loop_end_hovered || controls->adjusting_loop_end)
                                   ? theme.clip_border_selected
                                   : theme.loop_handle_border;
        SDL_Rect start_draw = controls->loop_start_rect;
        SDL_Rect end_draw = controls->loop_end_rect;
        int shrink_w = 4;
        int shrink_h = 4;
        if (start_draw.w > shrink_w) {
            start_draw.x += shrink_w / 2;
            start_draw.w -= shrink_w;
        }
        if (start_draw.h > shrink_h) {
            start_draw.y += shrink_h / 2;
            start_draw.h -= shrink_h;
        }
        if (end_draw.w > shrink_w) {
            end_draw.x += shrink_w / 2;
            end_draw.w -= shrink_w;
        }
        if (end_draw.h > shrink_h) {
            end_draw.y += shrink_h / 2;
            end_draw.h -= shrink_h;
        }
        SDL_SetRenderDrawColor(renderer, start_color.r, start_color.g, start_color.b, start_color.a);
        SDL_RenderFillRect(renderer, &start_draw);
        SDL_SetRenderDrawColor(renderer, start_border.r, start_border.g, start_border.b, start_border.a);
        SDL_RenderDrawRect(renderer, &start_draw);
        SDL_SetRenderDrawColor(renderer, end_color.r, end_color.g, end_color.b, end_color.a);
        SDL_RenderFillRect(renderer, &end_draw);
        SDL_SetRenderDrawColor(renderer, end_border.r, end_border.g, end_border.b, end_border.a);
        SDL_RenderDrawRect(renderer, &end_draw);

        // Loop labels (time or beat) above handles.
        char start_label[32];
        char end_label[32];
        float loop_start_sec = (float)state->loop_start_frame / (float)sample_rate;
        float loop_end_sec = (float)state->loop_end_frame / (float)sample_rate;
        timeline_view_format_label(loop_start_sec,
                                   state->timeline_view_in_beats,
                                   &state->tempo_map,
                                   &state->time_signature_map,
                                   start_label,
                                   sizeof(start_label));
        timeline_view_format_label(loop_end_sec,
                                   state->timeline_view_in_beats,
                                   &state->tempo_map,
                                   &state->time_signature_map,
                                   end_label,
                                   sizeof(end_label));
        SDL_Color loop_label_color = theme.loop_label;
        int label_scale = 1;
        int start_w = ui_measure_text_width(start_label, label_scale);
        int end_w = ui_measure_text_width(end_label, label_scale);
        int label_y = controls->loop_start_rect.y - ui_font_line_height(label_scale) - 2;
        if (label_y < rect->y) label_y = rect->y;
        int start_x = controls->loop_start_rect.x + (controls->loop_start_rect.w - start_w) / 2;
        int end_x = controls->loop_end_rect.x + (controls->loop_end_rect.w - end_w) / 2;
        ui_draw_text(renderer, start_x, label_y, start_label, loop_label_color, label_scale);
        ui_draw_text(renderer, end_x, label_y, end_label, loop_label_color, label_scale);
    }

    timeline_view_render_runtime_overlays(renderer,
                                          rect,
                                          state,
                                          &theme,
                                          sample_rate,
                                          window_start,
                                          window_end,
                                          visible_seconds,
                                          content_left,
                                          content_width,
                                          track_y,
                                          track_height,
                                          track_spacing,
                                          track_count,
                                          header_width,
                                          pixels_per_second);
}
