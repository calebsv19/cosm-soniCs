#include "ui/timeline_view.h"

#include "ui/font5x7.h"
#include "engine.h"
#include "engine/sampler.h"

#include <math.h>
#include <stdio.h>

#define TRACK_HEIGHT 80
#define CLIP_COLOR_R 85
#define CLIP_COLOR_G 125
#define CLIP_COLOR_B 210
#define TRACK_BG_R 38
#define TRACK_BG_G 44
#define TRACK_BG_B 58

void timeline_view_render(SDL_Renderer* renderer, const SDL_Rect* rect, const Engine* engine) {
    if (!renderer || !rect || !engine) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, TRACK_BG_R, TRACK_BG_G, TRACK_BG_B, 255);
    SDL_RenderFillRect(renderer, rect);

    int track_y = rect->y + 16;
    int track_height = TRACK_HEIGHT;

    SDL_SetRenderDrawColor(renderer, 60, 60, 72, 255);
    SDL_RenderDrawRect(renderer, &(SDL_Rect){rect->x + 12, track_y, rect->w - 24, track_height});

    const EngineTrack* tracks = engine_get_tracks(engine);
    int track_count = engine_get_track_count(engine);
    if (!tracks || track_count == 0) {
        return;
    }

    const EngineTrack* track = &tracks[0];
    int sample_rate = engine_get_config(engine)->sample_rate;
    double pixels_per_second = (double)(rect->w - 48) / 10.0;

    for (int i = 0; i < track->clip_count; ++i) {
        const EngineClip* clip = &track->clips[i];
        if (!clip->sampler) {
            continue;
        }

        uint64_t start_frame = engine_sampler_get_start_frame(clip->sampler);
        uint64_t frame_count = engine_sampler_get_frame_count(clip->sampler);
        double clip_sec = (double)frame_count / (double)sample_rate;
        double start_sec = (double)start_frame / (double)sample_rate;

        int clip_x = rect->x + 24 + (int)(start_sec * pixels_per_second);
        int clip_w = (int)(clip_sec * pixels_per_second);
        if (clip_w < 4) {
            clip_w = 4;
        }
        SDL_Rect clip_rect = {
            clip_x,
            track_y + 8,
            clip_w,
            track_height - 16,
        };

        SDL_SetRenderDrawColor(renderer, CLIP_COLOR_R, CLIP_COLOR_G, CLIP_COLOR_B, 220);
        SDL_RenderFillRect(renderer, &clip_rect);

        SDL_SetRenderDrawColor(renderer, 20, 20, 26, 255);
        SDL_RenderDrawRect(renderer, &clip_rect);

        SDL_Color text = {200, 200, 210, 255};
        char label[64];
        snprintf(label, sizeof(label), "Clip %d", i + 1);
        ui_draw_text(renderer, clip_rect.x + 8, clip_rect.y + 8, label, text, 2);
    }
}
