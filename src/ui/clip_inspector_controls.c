#include "ui/clip_inspector_controls.h"

#include "app_state.h"
#include "engine/audio_source.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "ui/font.h"
#include "ui/render_utils.h"

#include <math.h>
#include <string.h>

#define INSPECTOR_GAIN_MIN 0.0f
#define INSPECTOR_GAIN_MAX 4.0f
#define INSPECTOR_LABEL_SCALE 1.0f
#define INSPECTOR_VALUE_SCALE 1.0f

static void draw_slider(SDL_Renderer* renderer, const SDL_Rect* track_rect, float t, const DawThemePalette* theme) {
    if (!renderer || !track_rect || track_rect->w <= 0 || track_rect->h <= 0) {
        return;
    }
    SDL_Color track_bg = theme ? theme->slider_track : (SDL_Color){36, 36, 44, 255};
    SDL_Color track_border = theme ? theme->control_border : (SDL_Color){90, 90, 110, 255};
    SDL_Color fill_color = theme ? theme->slider_handle : (SDL_Color){80, 120, 170, 220};
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

static void draw_button(SDL_Renderer* renderer,
                        const SDL_Rect* rect,
                        const char* label,
                        bool active,
                        const DawThemePalette* theme) {
    if (!renderer || !rect || !label) {
        return;
    }
    SDL_Color base = theme ? theme->control_fill : (SDL_Color){48, 52, 62, 255};
    SDL_Color highlight = theme ? theme->control_active_fill : (SDL_Color){120, 160, 220, 255};
    SDL_Color border = theme ? theme->control_border : (SDL_Color){90, 95, 110, 255};
    SDL_Color text = theme ? theme->text_primary : (SDL_Color){220, 220, 230, 255};

    SDL_Color fill = active ? highlight : base;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    float scale = 1.0f;
    int text_w = ui_measure_text_width(label, scale);
    int text_h = ui_font_line_height(scale);
    int text_pad = 4;
    int text_x = rect->x + (rect->w - text_w) / 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    int max_text_w = rect->w - text_pad * 2;
    if (text_w <= max_text_w) {
        ui_draw_text(renderer, text_x, text_y, label, text, scale);
    } else if (max_text_w > 0) {
        ui_draw_text_clipped(renderer, rect->x + text_pad, text_y, label, text, scale, max_text_w);
    }
}

static void clip_inspector_draw_edit_box(SDL_Renderer* renderer,
                                         const SDL_Rect* rect,
                                         const DawThemePalette* theme) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    SDL_Color fill = theme ? theme->slider_track : (SDL_Color){36, 38, 46, 255};
    SDL_Color border = theme ? theme->control_border : (SDL_Color){100, 110, 130, 255};
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
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

void clip_inspector_render_controls_panel(SDL_Renderer* renderer,
                                          AppState* state,
                                          const ClipInspectorLayout* layout,
                                          const EngineClip* clip,
                                          int sample_rate,
                                          uint64_t clip_frames,
                                          uint64_t total_frames,
                                          const char* display_name,
                                          const char* source_name,
                                          const DawThemePalette* theme) {
    if (!renderer || !state || !layout || !clip || !theme || sample_rate <= 0 || clip_frames == 0) {
        return;
    }

    SDL_Color label = theme->text_muted;
    SDL_Color value = theme->text_primary;
    SDL_Color muted = theme->text_muted;
    SDL_Color box_bg = theme->slider_track;
    SDL_Color box_border = theme->control_border;

    double sr = (double)sample_rate;
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

    float sample_rate_f = (float)sample_rate;
    float fade_in_ms = (float)fade_in_frames * 1000.0f / sample_rate_f;
    float fade_out_ms = (float)fade_out_frames * 1000.0f / sample_rate_f;
    snprintf(line, sizeof(line), "Fade In %.0f ms", fade_in_ms);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_FADE_IN], line, "", label, muted);
    snprintf(line, sizeof(line), "Fade Out %.0f ms", fade_out_ms);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_FADE_OUT], line, "", label, muted);

    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_NAME], "Name", "", label, value);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_SOURCE], "Source", source_name, label, value);

    int channels = 0;
    if (clip->media) {
        channels = clip->media->channels;
    } else if (clip->source) {
        channels = clip->source->channels;
    }
    char format_line[64];
    const char* channel_label = "Unknown";
    if (channels == 1) channel_label = "Mono";
    else if (channels == 2) channel_label = "Stereo";
    else if (channels > 2) channel_label = "Multi";
    snprintf(format_line,
             sizeof(format_line),
             "%.1fkHz 32-bit float %s",
             (double)sample_rate / 1000.0,
             channel_label);
    clip_inspector_draw_row(renderer, &layout->rows[CLIP_INSPECTOR_ROW_FORMAT], "Format", format_line, label, value);

    if (state->inspector.edit.editing_timeline_start) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_START].value_rect, theme);
    }
    if (state->inspector.edit.editing_timeline_end) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_END].value_rect, theme);
    }
    if (state->inspector.edit.editing_timeline_length) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_LENGTH].value_rect, theme);
    }
    if (state->inspector.edit.editing_source_start) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_SOURCE_START].value_rect, theme);
    }
    if (state->inspector.edit.editing_source_end) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_SOURCE_END].value_rect, theme);
    }
    if (state->inspector.edit.editing_playback_rate) {
        clip_inspector_draw_edit_box(renderer, &layout->rows[CLIP_INSPECTOR_ROW_PLAYBACK_RATE].value_rect, theme);
    }

    clip_inspector_draw_row(renderer,
                            &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_START],
                            "Timeline Start",
                            timeline_start_text,
                            label,
                            value);
    clip_inspector_draw_row(renderer,
                            &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_END],
                            "Timeline End",
                            timeline_end_text,
                            label,
                            value);
    clip_inspector_draw_row(renderer,
                            &layout->rows[CLIP_INSPECTOR_ROW_TIMELINE_LENGTH],
                            "Timeline Length",
                            timeline_length_text,
                            label,
                            value);
    clip_inspector_draw_row(renderer,
                            &layout->rows[CLIP_INSPECTOR_ROW_SOURCE_START],
                            "Source Start",
                            source_start_text,
                            label,
                            value);
    clip_inspector_draw_row(renderer,
                            &layout->rows[CLIP_INSPECTOR_ROW_SOURCE_END],
                            "Source End",
                            source_end_text,
                            label,
                            value);
    clip_inspector_draw_row(renderer,
                            &layout->rows[CLIP_INSPECTOR_ROW_PLAYBACK_RATE],
                            "Playback Rate",
                            playback_rate_text,
                            label,
                            value);

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
        SDL_SetRenderDrawColor(renderer, value.r, value.g, value.b, value.a);
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
        SDL_SetRenderDrawColor(renderer, value.r, value.g, value.b, value.a);
        SDL_RenderDrawLine(renderer, caret_x, caret_y, caret_x, caret_y + edit_rect->h - 4);
    }

    draw_slider(renderer,
                &layout->gain_track_rect,
                (gain_value - INSPECTOR_GAIN_MIN) / (INSPECTOR_GAIN_MAX - INSPECTOR_GAIN_MIN),
                theme);
    draw_slider(renderer, &layout->fade_in_track_rect, fade_in_ratio, theme);
    draw_slider(renderer, &layout->fade_out_track_rect, fade_out_ratio, theme);

    if (layout->phase_left_rect.w > 0 && layout->phase_left_rect.h > 0 &&
        layout->phase_right_rect.w > 0 && layout->phase_right_rect.h > 0) {
        draw_button(renderer, &layout->phase_left_rect, "L", state->inspector.phase_invert_l, theme);
        draw_button(renderer, &layout->phase_right_rect, "R", state->inspector.phase_invert_r, theme);
    }
    if (layout->normalize_toggle_rect.w > 0 && layout->normalize_toggle_rect.h > 0) {
        draw_button(renderer,
                    &layout->normalize_toggle_rect,
                    state->inspector.normalize ? "On" : "Off",
                    state->inspector.normalize,
                    theme);
    }
    if (layout->reverse_toggle_rect.w > 0 && layout->reverse_toggle_rect.h > 0) {
        draw_button(renderer,
                    &layout->reverse_toggle_rect,
                    state->inspector.reverse ? "On" : "Off",
                    state->inspector.reverse,
                    theme);
    }
}
