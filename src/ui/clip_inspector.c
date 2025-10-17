#include "ui/clip_inspector.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "ui/font5x7.h"
#include "ui/layout.h"

#include <math.h>
#include <string.h>

#define INSPECTOR_GAIN_MIN 0.0f
#define INSPECTOR_GAIN_MAX 4.0f
#define INSPECTOR_MARGIN 16
#define INSPECTOR_SECTION_SPACING 18
#define INSPECTOR_SLIDER_HEIGHT 18
#define INSPECTOR_SLIDER_GAP 28
#define INSPECTOR_PRESET_HEIGHT 24
#define INSPECTOR_PRESET_SPACING 10

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
    SDL_SetRenderDrawColor(renderer, track_bg.r, track_bg.g, track_bg.b, track_bg.a);
    SDL_RenderFillRect(renderer, track_rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, track_rect);

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    SDL_Rect fill_rect = *track_rect;
    fill_rect.w = (int)(t * (float)track_rect->w);
    if (fill_rect.w < 0) fill_rect.w = 0;
    SDL_Color fill_color = {120, 180, 255, 200};
    SDL_SetRenderDrawColor(renderer, fill_color.r, fill_color.g, fill_color.b, fill_color.a);
    SDL_RenderFillRect(renderer, &fill_rect);

    SDL_Rect handle_rect = {
        track_rect->x + fill_rect.w - 4,
        track_rect->y - 4,
        8,
        track_rect->h + 8,
    };
    if (handle_rect.x < track_rect->x - 4) {
        handle_rect.x = track_rect->x - 4;
    }
    if (handle_rect.x + handle_rect.w > track_rect->x + track_rect->w + 4) {
        handle_rect.x = track_rect->x + track_rect->w - 4;
    }
    SDL_SetRenderDrawColor(renderer, 180, 210, 255, 255);
    SDL_RenderFillRect(renderer, &handle_rect);
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

    int text_w = (int)strlen(label) * 6 * 2;
    int text_h = 7 * 2;
    int text_x = rect->x + (rect->w - text_w) / 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    ui_draw_text(renderer, text_x, text_y, label, text, 2);
}

void clip_inspector_compute_layout(const AppState* state, ClipInspectorLayout* layout) {
    if (!state || !layout) {
        return;
    }
    zero_layout(layout);
    EngineRuntimeConfig runtime_cfg = clip_inspector_active_config(state);
    int preset_count = runtime_cfg.fade_preset_count;
    if (preset_count < 0) {
        preset_count = 0;
    }
    if (preset_count > INSPECTOR_FADE_PRESET_COUNT) {
        preset_count = INSPECTOR_FADE_PRESET_COUNT;
    }
    layout->fade_preset_count = preset_count;
    for (int i = 0; i < preset_count; ++i) {
        layout->fade_presets_ms[i] = runtime_cfg.fade_preset_ms[i];
    }
    for (int i = preset_count; i < INSPECTOR_FADE_PRESET_COUNT; ++i) {
        layout->fade_presets_ms[i] = 0.0f;
    }
    const Pane* mixer = ui_layout_get_pane(state, 2);
    if (!mixer) {
        return;
    }
    layout->panel_rect = mixer->rect;

    int content_x = mixer->rect.x + INSPECTOR_MARGIN;
    int content_y = mixer->rect.y + INSPECTOR_MARGIN;
    int content_width = mixer->rect.w - 2 * INSPECTOR_MARGIN;

    layout->name_rect = (SDL_Rect){content_x, content_y, content_width, 28};
    content_y += layout->name_rect.h + INSPECTOR_SECTION_SPACING;

    int left_col_width = content_width / 2;
    int right_col_width = content_width - left_col_width - INSPECTOR_MARGIN;
    if (left_col_width < 180) {
        left_col_width = 180;
        right_col_width = content_width - left_col_width - INSPECTOR_MARGIN;
    }
    if (right_col_width < 160) {
        right_col_width = 160;
        left_col_width = content_width - right_col_width - INSPECTOR_MARGIN;
        if (left_col_width < 140) {
            left_col_width = 140;
        }
    }
    int right_x = content_x + left_col_width + INSPECTOR_MARGIN;
    content_y += INSPECTOR_SLIDER_GAP;

    layout->gain_track_rect = (SDL_Rect){content_x, content_y, left_col_width, INSPECTOR_SLIDER_HEIGHT};
    layout->gain_fill_rect = layout->gain_track_rect;
    layout->gain_handle_rect = layout->gain_track_rect;
    content_y += INSPECTOR_SLIDER_HEIGHT + INSPECTOR_SLIDER_GAP;

    layout->fade_in_track_rect = (SDL_Rect){content_x, content_y, left_col_width, INSPECTOR_SLIDER_HEIGHT};
    layout->fade_in_fill_rect = layout->fade_in_track_rect;
    layout->fade_in_handle_rect = layout->fade_in_track_rect;
    content_y += INSPECTOR_SLIDER_HEIGHT + INSPECTOR_SLIDER_GAP;

    layout->fade_out_track_rect = (SDL_Rect){content_x, content_y, left_col_width, INSPECTOR_SLIDER_HEIGHT};
    layout->fade_out_fill_rect = layout->fade_out_track_rect;
    layout->fade_out_handle_rect = layout->fade_out_track_rect;

    int button_width = 0;
    int button_row_width = 0;
    if (layout->fade_preset_count > 0) {
        button_width = (right_col_width - (layout->fade_preset_count - 1) * INSPECTOR_PRESET_SPACING);
        if (button_width < 0) button_width = 0;
        button_width /= layout->fade_preset_count;
        if (button_width < 42) {
            button_width = 42;
        }
        button_row_width = button_width * layout->fade_preset_count + INSPECTOR_PRESET_SPACING * (layout->fade_preset_count - 1);
    }
    int button_start_x = right_x;
    if (layout->fade_preset_count > 0 && button_row_width > 0) {
        button_start_x = right_x + (right_col_width - button_row_width) / 2;
    }
    if (button_start_x < right_x) {
        button_start_x = right_x;
    }

    int fade_in_button_y = layout->fade_in_track_rect.y + layout->fade_in_track_rect.h + (INSPECTOR_SECTION_SPACING / 2);
    int fade_out_button_y = layout->fade_out_track_rect.y + layout->fade_out_track_rect.h + (INSPECTOR_SECTION_SPACING / 2);
    for (int i = 0; i < layout->fade_preset_count; ++i) {
        int bx = button_start_x + i * (button_width + INSPECTOR_PRESET_SPACING);
        layout->fade_in_buttons[i] = (SDL_Rect){bx, fade_in_button_y, button_width, INSPECTOR_PRESET_HEIGHT};
        layout->fade_out_buttons[i] = (SDL_Rect){bx, fade_out_button_y, button_width, INSPECTOR_PRESET_HEIGHT};
    }
}

void clip_inspector_render(SDL_Renderer* renderer, const AppState* state, const ClipInspectorLayout* layout) {
    if (!renderer || !state || !layout) {
        return;
    }

    SDL_Color label = {210, 210, 220, 255};

    if (!state->inspector.visible || !state->engine) {
        ui_draw_text(renderer, layout->panel_rect.x + 16, layout->panel_rect.y + 48, "Select a clip to edit.", label, 2);
        return;
    }

    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || state->inspector.track_index < 0 || state->inspector.track_index >= track_count) {
        ui_draw_text(renderer, layout->panel_rect.x + 16, layout->panel_rect.y + 48, "Clip unavailable.", label, 2);
        return;
    }
    const EngineTrack* track = &tracks[state->inspector.track_index];
    if (!track || state->inspector.clip_index < 0 || state->inspector.clip_index >= track->clip_count) {
        ui_draw_text(renderer, layout->panel_rect.x + 16, layout->panel_rect.y + 48, "Clip unavailable.", label, 2);
        return;
    }

    const EngineClip* clip = &track->clips[state->inspector.clip_index];
    SDL_Rect name_rect = layout->name_rect;

    SDL_Color box_bg = {40, 40, 48, 255};
    SDL_Color box_border = {100, 100, 120, 255};
    SDL_SetRenderDrawColor(renderer, box_bg.r, box_bg.g, box_bg.b, box_bg.a);
    SDL_RenderFillRect(renderer, &name_rect);
    SDL_SetRenderDrawColor(renderer, box_border.r, box_border.g, box_border.b, box_border.a);
    SDL_RenderDrawRect(renderer, &name_rect);

    const char* display_name = clip->name[0] ? clip->name : "(unnamed clip)";
    if (state->inspector.editing_name) {
        display_name = state->inspector.name;
    }
    ui_draw_text(renderer, name_rect.x + 8, name_rect.y + 8, display_name, label, 2);

    if (state->inspector.editing_name) {
        int caret_x = name_rect.x + 8 + (int)strlen(state->inspector.name) * 6 * 2;
        int caret_y = name_rect.y + 6;
        SDL_SetRenderDrawColor(renderer, 220, 220, 240, 255);
        SDL_RenderDrawLine(renderer, caret_x, caret_y, caret_x, caret_y + 20);
    }

    EngineRuntimeConfig runtime_cfg = clip_inspector_active_config(state);

    float gain_value = clip->gain;
    if (state->inspector.adjusting_gain) {
        gain_value = state->inspector.gain;
    }
    if (gain_value < INSPECTOR_GAIN_MIN) gain_value = INSPECTOR_GAIN_MIN;
    if (gain_value > INSPECTOR_GAIN_MAX) gain_value = INSPECTOR_GAIN_MAX;

    SDL_Rect track_rect = layout->gain_track_rect;
    float t = (gain_value - INSPECTOR_GAIN_MIN) / (INSPECTOR_GAIN_MAX - INSPECTOR_GAIN_MIN);
    float gain_db = 20.0f * log10f(gain_value > 0.000001f ? gain_value : 0.000001f);
    char line[128];
    snprintf(line, sizeof(line), "Gain %.2f (%.1f dB)", gain_value, gain_db);
    draw_slider(renderer, &track_rect, t);
    ui_draw_text(renderer, track_rect.x, track_rect.y - 20, line, label, 2);

    uint64_t clip_frames = clip->duration_frames;
    if (clip_frames == 0 && clip->sampler) {
        clip_frames = engine_sampler_get_frame_count(clip->sampler);
    }
    if (clip_frames == 0) {
        clip_frames = 1;
    }

    uint64_t fade_in_frames = state->inspector.adjusting_fade_in ? state->inspector.fade_in_frames : clip->fade_in_frames;
    uint64_t fade_out_frames = state->inspector.adjusting_fade_out ? state->inspector.fade_out_frames : clip->fade_out_frames;
    if (fade_in_frames > clip_frames) fade_in_frames = clip_frames;
    if (fade_out_frames > clip_frames) fade_out_frames = clip_frames;

    float fade_in_ratio = (float)fade_in_frames / (float)clip_frames;
    float fade_out_ratio = (float)fade_out_frames / (float)clip_frames;

    draw_slider(renderer, &layout->fade_in_track_rect, fade_in_ratio);
    draw_slider(renderer, &layout->fade_out_track_rect, fade_out_ratio);

    float sample_rate = runtime_cfg.sample_rate > 0 ? (float)runtime_cfg.sample_rate : 48000.0f;
    float fade_in_ms = (float)fade_in_frames * 1000.0f / sample_rate;
    float fade_out_ms = (float)fade_out_frames * 1000.0f / sample_rate;

    snprintf(line, sizeof(line), "Fade In %.1f ms", fade_in_ms);
    ui_draw_text(renderer, layout->fade_in_track_rect.x, layout->fade_in_track_rect.y - 20, line, label, 2);
    snprintf(line, sizeof(line), "Fade Out %.1f ms", fade_out_ms);
    ui_draw_text(renderer, layout->fade_out_track_rect.x, layout->fade_out_track_rect.y - 20, line, label, 2);

    if (layout->fade_preset_count > 0 && layout->fade_in_buttons[0].w > 0) {
        int label_x = layout->fade_in_buttons[0].x;
        int label_y = layout->fade_in_buttons[0].y - 22;
        ui_draw_text(renderer, label_x, label_y, "Presets (ms)", label, 2);

        for (int i = 0; i < layout->fade_preset_count; ++i) {
            float preset_ms = layout->fade_presets_ms[i];
            char preset_label[8];
            if (preset_ms < 0.05f) {
                strcpy(preset_label, "0");
            } else {
                snprintf(preset_label, sizeof(preset_label), "%.0f", preset_ms);
            }
            bool active_in = fabsf(fade_in_ms - preset_ms) < 0.6f;
            bool active_out = fabsf(fade_out_ms - preset_ms) < 0.6f;
            draw_button(renderer, &layout->fade_in_buttons[i], preset_label, active_in);
            draw_button(renderer, &layout->fade_out_buttons[i], preset_label, active_out);
        }
    }

    if (runtime_cfg.sample_rate > 0) {
        double start_sec = (double)clip->timeline_start_frames / (double)runtime_cfg.sample_rate;
        double dur_sec = (double)clip->duration_frames / (double)runtime_cfg.sample_rate;
        if (clip->duration_frames == 0) {
            dur_sec = (double)engine_sampler_get_frame_count(clip->sampler) / (double)runtime_cfg.sample_rate;
        }
        snprintf(line, sizeof(line), "Start %.3fs   Length %.3fs", start_sec, dur_sec);
        int info_y = layout->panel_rect.y + layout->panel_rect.h - INSPECTOR_MARGIN;
        ui_draw_text(renderer, layout->name_rect.x, info_y, line, label, 2);
    }
}
