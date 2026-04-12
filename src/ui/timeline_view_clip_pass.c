#include "ui/timeline_view_clip_pass.h"

#include "audio/media_registry.h"
#include "engine/engine.h"
#include "ui/font.h"
#include "ui/kit_viz_waveform_adapter.h"
#include "ui/render_utils.h"
#include "ui/timeline_view.h"
#include "ui/timeline_waveform.h"
#include "ui/waveform_render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CLIP_HANDLE_WIDTH 8

static bool timeline_clip_is_selected(const AppState* state, int track_index, int clip_index) {
    if (!state) {
        return false;
    }
    if (state->selection_count > 0) {
        for (int i = 0; i < state->selection_count; ++i) {
            if (state->selection[i].track_index == track_index && state->selection[i].clip_index == clip_index) {
                return true;
            }
        }
        return false;
    }
    return state->selected_track_index == track_index && state->selected_clip_index == clip_index;
}

static const char* timeline_basename(const char* path, char* scratch, size_t scratch_len) {
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

static const char* timeline_clip_display_name(const AppState* state,
                                              const EngineClip* clip,
                                              char* scratch,
                                              size_t scratch_len) {
    if (!clip) {
        return "Clip";
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
                return timeline_basename(entry->path, scratch, scratch_len);
            }
        }
    }
    const char* media_path = engine_clip_get_media_path(clip);
    if (media_path && media_path[0] != '\0') {
        return timeline_basename(media_path, scratch, scratch_len);
    }
    return "Clip";
}

static void timeline_draw_text_in_rect_clipped(SDL_Renderer* renderer,
                                               const SDL_Rect* rect,
                                               const char* text,
                                               SDL_Color color,
                                               int pad_x,
                                               int pad_y,
                                               float scale) {
    int max_w;
    if (!renderer || !rect || !text || text[0] == '\0') {
        return;
    }
    max_w = rect->w - pad_x * 2;
    if (max_w <= 0) {
        return;
    }
    ui_draw_text_clipped(renderer,
                         rect->x + pad_x,
                         rect->y + pad_y,
                         text,
                         color,
                         scale,
                         max_w);
}

static void fit_label_ellipsis(const char* src, int max_px, float scale, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!src) return;
    int full_w = ui_measure_text_width(src, scale);
    if (full_w <= max_px) {
        strncpy(out, src, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    const char* ell = "...";
    int ell_w = ui_measure_text_width(ell, scale);
    int target_px = max_px - ell_w;
    if (target_px <= 0) {
        strncpy(out, ell, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    size_t len = strlen(src);
    for (size_t i = len; i > 0; --i) {
        char tmp[ENGINE_CLIP_NAME_MAX];
        snprintf(tmp, sizeof(tmp), "%.*s", (int)i, src);
        if (ui_measure_text_width(tmp, scale) <= target_px) {
            snprintf(out, out_size, "%s%s", tmp, ell);
            out[out_size - 1] = '\0';
            return;
        }
    }
    strncpy(out, ell, out_size - 1);
    out[out_size - 1] = '\0';
}

static void draw_toggle_button(SDL_Renderer* renderer,
                               const SDL_Rect* rect,
                               const char* label,
                               bool active,
                               bool hovered,
                               SDL_Color active_col,
                               const TimelineTheme* theme) {
    if (!renderer || !rect || !label) {
        return;
    }
    SDL_Color base = theme ? theme->button_fill : (SDL_Color){58, 62, 70, 255};
    SDL_Color active_color = active_col;
    SDL_Color text = theme ? theme->text : (SDL_Color){220, 220, 230, 255};
    SDL_Color border = theme ? theme->button_border : (SDL_Color){90, 95, 110, 255};

    SDL_Color fill = base;
    SDL_Color border_color = border;
    if (active) {
        fill = active_color;
        if (theme) {
            border_color = theme->clip_border_selected;
        }
    } else if (hovered) {
        if (theme) {
            border_color = theme->clip_border_selected;
        }
    }
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);

    SDL_SetRenderDrawColor(renderer, border_color.r, border_color.g, border_color.b, border_color.a);
    SDL_RenderDrawRect(renderer, rect);

    int scale = 1;
    int text_w = ui_measure_text_width(label, 1.0f);
    int text_h = ui_font_line_height(scale);
    int text_x = rect->x + (rect->w - text_w) / 2;
    if (text_x < rect->x + 2) text_x = rect->x + 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text, scale);
}

void timeline_view_render_track_clip_pass(SDL_Renderer* renderer,
                                          const SDL_Rect* timeline_rect,
                                          AppState* state,
                                          const TimelineTheme* theme,
                                          const EngineTrack* tracks,
                                          int track_count,
                                          int track_y,
                                          int track_height,
                                          int track_spacing,
                                          int header_width,
                                          int content_left,
                                          int content_width,
                                          float window_start,
                                          float window_end,
                                          float pixels_per_second,
                                          int sample_rate) {
    SDL_Color header_bg;
    SDL_Color header_border;
    SDL_Color header_text;
    int active_track;
    const TrackNameEditor* editor;
    SDL_Point mouse_point;

    if (!renderer || !timeline_rect || !state || !theme || !tracks ||
        track_count <= 0 || content_width <= 0 || sample_rate <= 0) {
        return;
    }

    header_bg = theme->lane_header_fill;
    header_border = theme->lane_header_border;
    header_text = theme->text;
    active_track = (state->selected_track_index >= 0 && state->selected_track_index < track_count)
                       ? state->selected_track_index
                       : -1;
    editor = &state->track_name_editor;
    mouse_point = (SDL_Point){state->mouse_x, state->mouse_y};

    for (int t = 0; t < track_count; ++t) {
        const EngineTrack* track = &tracks[t];
        int lane_top = track_y + t * (track_height + track_spacing);

        TimelineTrackHeaderLayout header_layout = {0};
        timeline_view_compute_track_header_layout(timeline_rect,
                                                  lane_top,
                                                  track_height,
                                                  header_width,
                                                  &header_layout);
        SDL_Rect header_rect = header_layout.header_rect;
        SDL_Color header_fill = header_bg;
        SDL_Color label_color = header_text;
        if (active_track == t) {
            header_fill = theme->lane_header_fill_active;
        }
        if (track && track->muted) {
            header_fill.r = (Uint8)((int)header_fill.r * 3 / 5);
            header_fill.g = (Uint8)((int)header_fill.g * 3 / 5);
            header_fill.b = (Uint8)((int)header_fill.b * 3 / 5);
            label_color = theme->text_muted;
        }
        SDL_SetRenderDrawColor(renderer, header_fill.r, header_fill.g, header_fill.b, header_fill.a);
        SDL_RenderFillRect(renderer, &header_rect);
        SDL_SetRenderDrawColor(renderer, header_border.r, header_border.g, header_border.b, header_border.a);
        SDL_RenderDrawRect(renderer, &header_rect);
        char default_label[32];
        const char* label = NULL;
        if (editor && editor->editing && editor->track_index == t) {
            label = editor->buffer;
        } else if (track && track->name[0]) {
            label = track->name;
        }
        if (!label || label[0] == '\0') {
            snprintf(default_label, sizeof(default_label), "Track %d", t + 1);
            label = default_label;
        }

        SDL_Rect mute_rect = header_layout.mute_rect;
        SDL_Rect solo_rect = header_layout.solo_rect;

        bool mute_hover = SDL_PointInRect(&mouse_point, &mute_rect);
        bool solo_hover = SDL_PointInRect(&mouse_point, &solo_rect);
        SDL_Color mute_active = {
            (Uint8)lroundf((float)theme->lane_header_fill.r * 0.82f + (float)theme->toggle_active_loop.r * 0.18f),
            (Uint8)lroundf((float)theme->lane_header_fill.g * 0.82f + (float)theme->toggle_active_loop.g * 0.18f),
            (Uint8)lroundf((float)theme->lane_header_fill.b * 0.82f + (float)theme->toggle_active_loop.b * 0.18f),
            242
        };
        SDL_Color solo_active = {
            (Uint8)lroundf((float)theme->lane_header_fill.r * 0.82f + (float)theme->toggle_active_snap.r * 0.18f),
            (Uint8)lroundf((float)theme->lane_header_fill.g * 0.82f + (float)theme->toggle_active_snap.g * 0.18f),
            (Uint8)lroundf((float)theme->lane_header_fill.b * 0.82f + (float)theme->toggle_active_snap.b * 0.18f),
            242
        };
        draw_toggle_button(renderer, &mute_rect, "M", track ? track->muted : false, mute_hover, mute_active, theme);
        draw_toggle_button(renderer, &solo_rect, "S", track ? track->solo : false, solo_hover, solo_active, theme);

        int text_x = header_layout.text_x;
        int text_max_x = header_layout.text_max_x;
        int available_px = text_max_x - text_x;
        char text_buf[ENGINE_CLIP_NAME_MAX];
        const char* display = label;
        if (available_px > 0) {
            fit_label_ellipsis(label, available_px, 1.0f, text_buf, sizeof(text_buf));
            display = text_buf;
        }
        int label_text_y = header_layout.text_y;
        ui_draw_text(renderer, text_x, label_text_y, display, label_color, 1);
        if (editor && editor->editing && editor->track_index == t) {
            float scale = 1.0f;
            int caret_limit = text_max_x;
            if (caret_limit < text_x) caret_limit = text_x;
            char temp[ENGINE_CLIP_NAME_MAX];
            int len = (int)strlen(editor->buffer);
            int caret_x = text_x;
            int target_index = editor->cursor;
            if (target_index < 0) target_index = 0;
            if (target_index > len) target_index = len;
            snprintf(temp, sizeof(temp), "%.*s", target_index, editor->buffer);
            caret_x = text_x + ui_measure_text_width(temp, scale);
            if (caret_x > caret_limit) {
                caret_x = caret_limit;
            }
            int caret_h = ui_font_line_height(scale);
            SDL_Rect caret_rect = {
                caret_x,
                label_text_y,
                2,
                caret_h
            };
            SDL_SetRenderDrawColor(renderer, theme->text.r, theme->text.g, theme->text.b, theme->text.a);
            SDL_RenderFillRect(renderer, &caret_rect);
        }

        if (!track || track->clip_count <= 0) {
            continue;
        }

        for (int i = 0; i < track->clip_count; ++i) {
            const EngineClip* clip = &track->clips[i];
            if (!clip) {
                continue;
            }

            const uint64_t start_frame = clip->timeline_start_frames;
            uint64_t frame_count = clip->duration_frames;
            uint64_t media_frames = (clip->media && clip->media->frame_count > 0) ? clip->media->frame_count : 0;
            uint64_t clip_available = media_frames > clip->offset_frames ? media_frames - clip->offset_frames : 0;
            if (frame_count == 0 || (clip_available > 0 && frame_count > clip_available)) {
                frame_count = clip_available;
            }
            const double clip_sec = (double)frame_count / (double)sample_rate;
            const double start_sec = (double)start_frame / (double)sample_rate;

            double clip_end_sec = start_sec + clip_sec;
            double visible_start_sec = start_sec;
            if (visible_start_sec < (double)window_start) {
                visible_start_sec = (double)window_start;
            }
            double visible_end_sec = clip_end_sec;
            double window_end_sec = (double)window_end;
            if (visible_end_sec > window_end_sec) {
                visible_end_sec = window_end_sec;
            }
            if (visible_end_sec <= visible_start_sec) {
                continue;
            }

            double visible_offset = visible_start_sec - (double)window_start;
            int clip_x = content_left + (int)round(visible_offset * pixels_per_second);
            int clip_w = (int)round((visible_end_sec - visible_start_sec) * pixels_per_second);
            if (clip_w < 4) {
                clip_w = 4;
            }

            SDL_Rect clip_rect = {0, 0, 0, 0};
            timeline_view_compute_lane_clip_rect(lane_top,
                                                 track_height,
                                                 clip_x,
                                                 clip_w,
                                                 &clip_rect);

            bool is_selected = timeline_clip_is_selected(state, t, i);
            SDL_SetRenderDrawColor(renderer, theme->clip_fill.r, theme->clip_fill.g, theme->clip_fill.b, theme->clip_fill.a);
            SDL_RenderFillRect(renderer, &clip_rect);

            if (is_selected) {
                SDL_SetRenderDrawColor(renderer,
                                       theme->clip_border_selected.r,
                                       theme->clip_border_selected.g,
                                       theme->clip_border_selected.b,
                                       theme->clip_border_selected.a);
            } else {
                SDL_SetRenderDrawColor(renderer, theme->clip_border.r, theme->clip_border.g, theme->clip_border.b, theme->clip_border.a);
            }
            SDL_RenderDrawRect(renderer, &clip_rect);

            const char* media_path = engine_clip_get_media_path(clip);
            if (clip->media && clip->media->frame_count > 0 &&
                media_path && media_path[0] != '\0') {
                int wave_label_pad = 4;
                float label_scale = 1.3f;
                int label_h = ui_font_line_height(label_scale);
                SDL_Rect waveform_rect = clip_rect;
                waveform_rect.y += label_h + wave_label_pad;
                waveform_rect.h -= label_h + wave_label_pad + 2;
                if (waveform_rect.h < 8) {
                    waveform_rect = clip_rect;
                }
                if (waveform_rect.w > 0 && waveform_rect.h > 0) {
                    double visible_duration = visible_end_sec - visible_start_sec;
                    if (visible_duration > 0.0) {
                        uint64_t local_visible_start = clip->offset_frames;
                        double local_offset_sec = visible_start_sec - start_sec;
                        if (local_offset_sec > 0.0) {
                            local_visible_start += (uint64_t)llround(local_offset_sec * (double)sample_rate);
                        }
                        uint64_t max_frame = clip->offset_frames + frame_count;
                        if (local_visible_start > max_frame) {
                            local_visible_start = max_frame;
                        }
                        uint64_t visible_frames = (uint64_t)llround(visible_duration * (double)sample_rate);
                        if (visible_frames == 0) {
                            visible_frames = 1;
                        }
                        if (local_visible_start + visible_frames > max_frame) {
                            visible_frames = max_frame > local_visible_start ? max_frame - local_visible_start : 0;
                        }
                        if (visible_frames > 0) {
                            SDL_Color wave_color = theme->waveform;
                            bool rendered = false;
                            if (state->inspector.waveform.use_kit_viz_waveform) {
                                DawKitVizWaveformRequest request = {
                                    .renderer = renderer,
                                    .cache = &state->waveform_cache,
                                    .clip = clip->media,
                                    .source_path = media_path,
                                    .target_rect = &waveform_rect,
                                    .view_start_frame = local_visible_start,
                                    .view_frame_count = visible_frames,
                                    .color = wave_color,
                                };
                                rendered = daw_kit_viz_render_waveform(&request);
                            }
                            if (!rendered) {
                                waveform_render_view(renderer,
                                                     &state->waveform_cache,
                                                     clip->media,
                                                     media_path,
                                                     &waveform_rect,
                                                     local_visible_start,
                                                     visible_frames,
                                                     wave_color);
                            }
                        }
                    }
                }
            }

            int fade_in_px = (int)round((double)clip->fade_in_frames / (double)sample_rate * pixels_per_second);
            int fade_out_px = (int)round((double)clip->fade_out_frames / (double)sample_rate * pixels_per_second);
            int clip_clip_left_px = (int)round((visible_start_sec - start_sec) * pixels_per_second);
            int clip_offset_px = clip_clip_left_px;
            if (clip_offset_px < 0) clip_offset_px = 0;
            double clip_total_px = (clip_end_sec - start_sec) * pixels_per_second;
            EngineFadeCurve fade_in_curve = clip->fade_in_curve;
            EngineFadeCurve fade_out_curve = clip->fade_out_curve;

            ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
            int fade_in_draw = 0;
            if (fade_in_px > 0 && clip_offset_px < fade_in_px) {
                fade_in_draw = fade_in_px - clip_offset_px;
                if (fade_in_draw > clip_rect.w) fade_in_draw = clip_rect.w;
            }
            if (fade_in_draw > 0 && fade_in_px > 0) {
                SDL_SetRenderDrawColor(renderer, theme->text.r, theme->text.g, theme->text.b, 28);
                for (int fx = 0; fx < fade_in_draw; ++fx) {
                    float tf = (float)(clip_offset_px + fx) / (float)fade_in_px;
                    float gain = ui_fade_curve_eval(fade_in_curve, tf);
                    float overlay = 1.0f - gain;
                    int h = (int)(overlay * clip_rect.h);
                    SDL_RenderDrawLine(renderer,
                                       clip_rect.x + fx,
                                       clip_rect.y,
                                       clip_rect.x + fx,
                                       clip_rect.y + h);
                }
            }

            int fade_out_draw = 0;
            double fade_out_start_px = clip_total_px - (double)fade_out_px;
            if (fade_out_px > 0 && clip_total_px > 0.0) {
                int visible_right = clip_offset_px + clip_rect.w;
                if ((double)visible_right > fade_out_start_px) {
                    int start_x = (int)floor(fade_out_start_px - (double)clip_offset_px);
                    if (start_x < 0) start_x = 0;
                    if (start_x < clip_rect.w) {
                        fade_out_draw = clip_rect.w - start_x;
                    }
                }
            }
            if (fade_out_draw > 0 && fade_out_px > 0) {
                SDL_SetRenderDrawColor(renderer, theme->text.r, theme->text.g, theme->text.b, 28);
                int start_x = clip_rect.w - fade_out_draw;
                for (int fx = 0; fx < fade_out_draw; ++fx) {
                    int local_px = clip_offset_px + start_x + fx;
                    float tf = (float)((double)local_px - fade_out_start_px) / (float)fade_out_px;
                    float gain = ui_fade_curve_eval(fade_out_curve, tf);
                    int h = (int)(gain * clip_rect.h);
                    int px = clip_rect.x + start_x + fx;
                    SDL_RenderDrawLine(renderer,
                                       px,
                                       clip_rect.y,
                                       px,
                                       clip_rect.y + h);
                }
            }
            ui_set_blend_mode(renderer, SDL_BLENDMODE_NONE);

            SDL_Color text_color = theme->clip_text;
            char name_buf[ENGINE_CLIP_NAME_MAX];
            const char* name = timeline_clip_display_name(state, clip, name_buf, sizeof(name_buf));
            int text_h = ui_font_line_height(1.0f);
            int clip_label_pad_x = text_h / 2;
            if (clip_label_pad_x < 6) {
                clip_label_pad_x = 6;
            }
            int label_pad_y = text_h / 6;
            if (label_pad_y < 2) {
                label_pad_y = 2;
            }
            timeline_draw_text_in_rect_clipped(renderer,
                                               &clip_rect,
                                               name,
                                               text_color,
                                               clip_label_pad_x,
                                               label_pad_y,
                                               1.0f);

            if (is_selected) {
                SDL_SetRenderDrawColor(renderer,
                                       theme->clip_border_selected.r,
                                       theme->clip_border_selected.g,
                                       theme->clip_border_selected.b,
                                       theme->clip_border_selected.a);
                SDL_Rect left_handle = {
                    clip_rect.x - 2,
                    clip_rect.y,
                    CLIP_HANDLE_WIDTH,
                    clip_rect.h,
                };
                SDL_Rect right_handle = {
                    clip_rect.x + clip_rect.w - CLIP_HANDLE_WIDTH + 2,
                    clip_rect.y,
                    CLIP_HANDLE_WIDTH,
                    clip_rect.h,
                };
                SDL_RenderFillRect(renderer, &left_handle);
                SDL_RenderFillRect(renderer, &right_handle);
            }
        }
    }
}
