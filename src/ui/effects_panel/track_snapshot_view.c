#include "ui/effects_panel.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/font.h"

#include <math.h>
#include <stdio.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void draw_slider(SDL_Renderer* renderer, const SDL_Rect* rect, float t) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    t = clampf(t, 0.0f, 1.0f);
    SDL_Color track_bg = {36, 36, 44, 255};
    SDL_Color track_border = {90, 90, 110, 255};
    SDL_Color fill = {80, 120, 170, 220};
    SDL_SetRenderDrawColor(renderer, track_bg.r, track_bg.g, track_bg.b, track_bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, rect);
    SDL_Rect fill_rect = *rect;
    fill_rect.w = (int)(t * (float)rect->w);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &fill_rect);
    SDL_Rect handle = {
        rect->x + fill_rect.w - 3,
        rect->y - 2,
        6,
        rect->h + 4
    };
    if (handle.x < rect->x - 2) handle.x = rect->x - 2;
    if (handle.x + handle.w > rect->x + rect->w + 2) handle.x = rect->x + rect->w - 2;
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, &handle);
}

static float linear_to_db(float linear) {
    if (linear < 0.000001f) linear = 0.000001f;
    return 20.0f * log10f(linear);
}

void effects_panel_render_track_snapshot(SDL_Renderer* renderer, const AppState* state, const EffectsPanelLayout* layout) {
    if (!renderer || !state || !layout) {
        return;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    const EffectsPanelTrackSnapshotLayout* snap = &layout->track_snapshot;
    SDL_Color label = {210, 210, 220, 255};
    SDL_Color text_dim = {150, 160, 180, 255};
    SDL_Color card_bg = {34, 36, 44, 255};
    SDL_Color card_border = {70, 75, 92, 255};
    SDL_Color button_bg = {44, 48, 58, 255};
    SDL_Color active_bg = {90, 120, 170, 200};

    const EngineTrack* track = NULL;
    if (state->engine && panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0) {
        int track_count = engine_get_track_count(state->engine);
        if (panel->target_track_index < track_count) {
            const EngineTrack* tracks = engine_get_tracks(state->engine);
            if (tracks) {
                track = &tracks[panel->target_track_index];
            }
        }
    }

    float gain_linear = (track ? track->gain : panel->track_snapshot.gain);
    if (gain_linear <= 0.0f) gain_linear = 1.0f;
    float gain_db = linear_to_db(gain_linear);
    if (gain_db < -20.0f) gain_db = -20.0f;
    if (gain_db > 20.0f) gain_db = 20.0f;
    float gain_t = (gain_db + 20.0f) / 40.0f;

    float pan_value = track ? track->pan : panel->track_snapshot.pan;
    if (pan_value < -1.0f) pan_value = -1.0f;
    if (pan_value > 1.0f) pan_value = 1.0f;
    float pan_t = (pan_value + 1.0f) * 0.5f;

    bool muted = track ? track->muted : panel->track_snapshot.muted;
    bool solo = track ? track->solo : panel->track_snapshot.solo;

    if (snap->eq_rect.w > 0 && snap->eq_rect.h > 0) {
        SDL_SetRenderDrawColor(renderer, card_bg.r, card_bg.g, card_bg.b, card_bg.a);
        SDL_RenderFillRect(renderer, &snap->eq_rect);
        SDL_SetRenderDrawColor(renderer, card_border.r, card_border.g, card_border.b, card_border.a);
        SDL_RenderDrawRect(renderer, &snap->eq_rect);
        ui_draw_text(renderer,
                     snap->eq_rect.x + 6,
                     snap->eq_rect.y + 6,
                     "EQ",
                     label,
                     1.4f);
        ui_draw_text(renderer,
                     snap->eq_rect.x + 6,
                     snap->eq_rect.y + 24,
                     "Double-click to open",
                     text_dim,
                     1.0f);
    }

    if (snap->gain_rect.w > 0 && snap->gain_rect.h > 0) {
        draw_slider(renderer, &snap->gain_rect, gain_t);
        if (snap->gain_label_rect.w > 0 && snap->gain_label_rect.h > 0) {
            char line[64];
            snprintf(line, sizeof(line), "Gain %.1f dB", gain_db);
            ui_draw_text(renderer,
                         snap->gain_label_rect.x,
                         snap->gain_label_rect.y,
                         line,
                         label,
                         1.0f);
        }
    }

    if (snap->pan_rect.w > 0 && snap->pan_rect.h > 0) {
        draw_slider(renderer, &snap->pan_rect, pan_t);
        if (snap->pan_label_rect.w > 0 && snap->pan_label_rect.h > 0) {
            char line[64];
            snprintf(line, sizeof(line), "Pan %.2f", pan_value);
            ui_draw_text(renderer,
                         snap->pan_label_rect.x,
                         snap->pan_label_rect.y,
                         line,
                         label,
                         1.0f);
        }
    }

    if (snap->mute_rect.w > 0 && snap->mute_rect.h > 0) {
        SDL_SetRenderDrawColor(renderer,
                               muted ? active_bg.r : button_bg.r,
                               muted ? active_bg.g : button_bg.g,
                               muted ? active_bg.b : button_bg.b,
                               muted ? active_bg.a : button_bg.a);
        SDL_RenderFillRect(renderer, &snap->mute_rect);
        SDL_SetRenderDrawColor(renderer, card_border.r, card_border.g, card_border.b, card_border.a);
        SDL_RenderDrawRect(renderer, &snap->mute_rect);
        ui_draw_text(renderer,
                     snap->mute_rect.x + 8,
                     snap->mute_rect.y + 6,
                     "Mute",
                     label,
                     1.2f);
    }

    if (snap->solo_rect.w > 0 && snap->solo_rect.h > 0) {
        SDL_SetRenderDrawColor(renderer,
                               solo ? active_bg.r : button_bg.r,
                               solo ? active_bg.g : button_bg.g,
                               solo ? active_bg.b : button_bg.b,
                               solo ? active_bg.a : button_bg.a);
        SDL_RenderFillRect(renderer, &snap->solo_rect);
        SDL_SetRenderDrawColor(renderer, card_border.r, card_border.g, card_border.b, card_border.a);
        SDL_RenderDrawRect(renderer, &snap->solo_rect);
        ui_draw_text(renderer,
                     snap->solo_rect.x + 8,
                     snap->solo_rect.y + 6,
                     "Solo",
                     label,
                     1.2f);
    }
}
