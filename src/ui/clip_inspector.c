#include "ui/clip_inspector.h"

#include "app_state.h"
#include "engine.h"
#include "engine/sampler.h"
#include "ui/font5x7.h"
#include "ui/layout.h"

#include <math.h>
#include <string.h>

#define INSPECTOR_GAIN_MIN 0.0f
#define INSPECTOR_GAIN_MAX 4.0f

static void zero_layout(ClipInspectorLayout* layout) {
    if (!layout) {
        return;
    }
    SDL_zero(*layout);
}

void clip_inspector_compute_layout(const AppState* state, ClipInspectorLayout* layout) {
    if (!state || !layout) {
        return;
    }
    zero_layout(layout);
    const Pane* mixer = ui_layout_get_pane(state, 2);
    if (!mixer) {
        return;
    }
    layout->panel_rect = mixer->rect;

    int content_x = mixer->rect.x + 16;
    int content_y = mixer->rect.y + 48;

    layout->name_rect = (SDL_Rect){content_x, content_y, mixer->rect.w - 32, 28};
    content_y += 44;

    int slider_width = mixer->rect.w - 64;
    if (slider_width < 120) {
        slider_width = 120;
    }
    layout->gain_track_rect = (SDL_Rect){content_x, content_y, slider_width, 16};
    layout->gain_fill_rect = layout->gain_track_rect;
    layout->gain_handle_rect = layout->gain_track_rect;
}

void clip_inspector_render(SDL_Renderer* renderer, const AppState* state, const ClipInspectorLayout* layout) {
    if (!renderer || !state || !layout) {
        return;
    }

    SDL_Color label = {210, 210, 220, 255};
    ui_draw_text(renderer, layout->panel_rect.x + 12, layout->panel_rect.y + 12, "CLIP INSPECTOR", label, 2);

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

    float gain_value = clip->gain;
    if (state->inspector.adjusting_gain) {
        gain_value = state->inspector.gain;
    }
    if (gain_value < INSPECTOR_GAIN_MIN) gain_value = INSPECTOR_GAIN_MIN;
    if (gain_value > INSPECTOR_GAIN_MAX) gain_value = INSPECTOR_GAIN_MAX;

    SDL_Rect track_rect = layout->gain_track_rect;
    SDL_Color track_bg = {36, 36, 44, 255};
    SDL_Color track_border = {90, 90, 110, 255};
    SDL_SetRenderDrawColor(renderer, track_bg.r, track_bg.g, track_bg.b, track_bg.a);
    SDL_RenderFillRect(renderer, &track_rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, &track_rect);

    float t = (gain_value - INSPECTOR_GAIN_MIN) / (INSPECTOR_GAIN_MAX - INSPECTOR_GAIN_MIN);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    SDL_Rect fill_rect = track_rect;
    fill_rect.w = (int)(track_rect.w * t);
    if (fill_rect.w < 4) fill_rect.w = 4;
    SDL_SetRenderDrawColor(renderer, 120, 180, 255, 200);
    SDL_RenderFillRect(renderer, &fill_rect);

    SDL_Rect handle_rect = {
        track_rect.x + fill_rect.w - 4,
        track_rect.y - 4,
        8,
        track_rect.h + 8,
    };
    SDL_SetRenderDrawColor(renderer, 180, 210, 255, 255);
    SDL_RenderFillRect(renderer, &handle_rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, &handle_rect);

    float gain_db = 20.0f * log10f(gain_value > 0.000001f ? gain_value : 0.000001f);
    char line[128];
    snprintf(line, sizeof(line), "Gain: %.2f (%.1f dB)", gain_value, gain_db);
    ui_draw_text(renderer, track_rect.x, track_rect.y - 18, line, label, 2);

    const EngineRuntimeConfig* cfg = engine_get_config(state->engine);
    if (cfg && cfg->sample_rate > 0) {
        double start_sec = (double)clip->timeline_start_frames / (double)cfg->sample_rate;
        double dur_sec = (double)clip->duration_frames / (double)cfg->sample_rate;
        if (clip->duration_frames == 0) {
            dur_sec = (double)engine_sampler_get_frame_count(clip->sampler) / (double)cfg->sample_rate;
        }
        snprintf(line, sizeof(line), "Start: %.3fs   Length: %.3fs", start_sec, dur_sec);
        ui_draw_text(renderer, track_rect.x, track_rect.y + track_rect.h + 12, line, label, 2);
    }
}
