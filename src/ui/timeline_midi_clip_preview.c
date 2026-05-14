#include "ui/timeline_midi_clip_preview.h"

#include "ui/render_utils.h"

#include <math.h>

static uint64_t midi_note_end_frame(const EngineMidiNote* note) {
    if (!note) {
        return 0;
    }
    if (UINT64_MAX - note->start_frame < note->duration_frames) {
        return UINT64_MAX;
    }
    return note->start_frame + note->duration_frames;
}

static bool midi_preview_pitch_bounds(const EngineClip* clip, uint8_t* out_low, uint8_t* out_high) {
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    int count = engine_clip_midi_note_count(clip);
    if (!notes || count <= 0 || !out_low || !out_high) {
        return false;
    }

    uint8_t low = notes[0].note;
    uint8_t high = notes[0].note;
    for (int i = 1; i < count; ++i) {
        if (notes[i].note < low) {
            low = notes[i].note;
        }
        if (notes[i].note > high) {
            high = notes[i].note;
        }
    }

    *out_low = low;
    *out_high = high;
    return true;
}

static SDL_Color midi_preview_velocity_color(float velocity) {
    if (velocity < 0.0f) velocity = 0.0f;
    if (velocity > 1.0f) velocity = 1.0f;
    static const SDL_Color stops[] = {
        {124, 72, 210, 170},
        {47, 111, 230, 170},
        {24, 166, 126, 170},
        {236, 205, 76, 170},
        {238, 134, 50, 170},
        {226, 54, 62, 170},
    };
    const int stop_count = (int)(sizeof(stops) / sizeof(stops[0]));
    float scaled = velocity * (float)(stop_count - 1);
    int index = (int)scaled;
    if (index >= stop_count - 1) {
        return stops[stop_count - 1];
    }
    float t = scaled - (float)index;
    SDL_Color a = stops[index];
    SDL_Color b = stops[index + 1];
    SDL_Color out = {
        (Uint8)((float)a.r + ((float)b.r - (float)a.r) * t),
        (Uint8)((float)a.g + ((float)b.g - (float)a.g) * t),
        (Uint8)((float)a.b + ((float)b.b - (float)a.b) * t),
        170
    };
    return out;
}

int timeline_midi_clip_preview_frame_to_x(const SDL_Rect* clip_rect,
                                          uint64_t frame,
                                          uint64_t visible_start_frame,
                                          uint64_t visible_frame_count) {
    if (!clip_rect || clip_rect->w <= 0 || visible_frame_count == 0) {
        return clip_rect ? clip_rect->x : 0;
    }
    uint64_t visible_end_frame = visible_start_frame + visible_frame_count;
    if (visible_end_frame < visible_start_frame) {
        visible_end_frame = UINT64_MAX;
    }
    uint64_t clamped = frame;
    if (clamped < visible_start_frame) {
        clamped = visible_start_frame;
    }
    if (clamped > visible_end_frame) {
        clamped = visible_end_frame;
    }
    double t = (double)(clamped - visible_start_frame) / (double)visible_frame_count;
    if (t < 0.0) {
        t = 0.0;
    } else if (t > 1.0) {
        t = 1.0;
    }
    int x = clip_rect->x + (int)floor(t * (double)clip_rect->w);
    int right = clip_rect->x + clip_rect->w;
    if (x < clip_rect->x) {
        x = clip_rect->x;
    } else if (x > right) {
        x = right;
    }
    return x;
}

void timeline_midi_clip_preview_render(SDL_Renderer* renderer,
                                       const EngineClip* clip,
                                       const SDL_Rect* clip_rect,
                                       uint64_t visible_start_frame,
                                       uint64_t visible_frame_count,
                                       const TimelineTheme* theme) {
    if (!renderer || !clip || !clip_rect || !theme || clip_rect->w <= 0 || clip_rect->h <= 0 ||
        engine_clip_get_kind(clip) != ENGINE_CLIP_KIND_MIDI || visible_frame_count == 0) {
        return;
    }

    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    int count = engine_clip_midi_note_count(clip);
    uint8_t low = 0;
    uint8_t high = 0;
    if (!notes || count <= 0 || !midi_preview_pitch_bounds(clip, &low, &high)) {
        return;
    }

    uint64_t visible_end_frame = visible_start_frame + visible_frame_count;
    if (visible_end_frame < visible_start_frame) {
        visible_end_frame = UINT64_MAX;
    }

    int pad_top = clip_rect->h > 28 ? 16 : 3;
    int pad_bottom = 4;
    int inner_x = clip_rect->x;
    int inner_y = clip_rect->y + pad_top;
    int inner_w = clip_rect->w;
    int inner_h = clip_rect->h - pad_top - pad_bottom;
    if (inner_w <= 0 || inner_h <= 0) {
        return;
    }

    int pitch_span = (int)high - (int)low + 1;
    if (pitch_span < 1) {
        pitch_span = 1;
    }

    ui_set_blend_mode(renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < count; ++i) {
        const EngineMidiNote* note = &notes[i];
        uint64_t note_end = midi_note_end_frame(note);
        if (note_end <= visible_start_frame || note->start_frame >= visible_end_frame) {
            continue;
        }

        uint64_t clipped_start = note->start_frame > visible_start_frame ? note->start_frame : visible_start_frame;
        uint64_t clipped_end = note_end < visible_end_frame ? note_end : visible_end_frame;
        if (clipped_end <= clipped_start) {
            continue;
        }

        int x0 = timeline_midi_clip_preview_frame_to_x(clip_rect,
                                                       clipped_start,
                                                       visible_start_frame,
                                                       visible_frame_count);
        int x1 = timeline_midi_clip_preview_frame_to_x(clip_rect,
                                                       clipped_end,
                                                       visible_start_frame,
                                                       visible_frame_count);
        if (x1 < clip_rect->x + clip_rect->w) {
            x1 += 1;
        }
        if (x1 <= x0) {
            x1 = x0 + 1;
        }
        if (x0 < inner_x) {
            x0 = inner_x;
        }
        if (x1 > inner_x + inner_w) {
            x1 = inner_x + inner_w;
        }

        int pitch_row = (int)high - (int)note->note;
        if (pitch_row < 0) {
            pitch_row = 0;
        } else if (pitch_row >= pitch_span) {
            pitch_row = pitch_span - 1;
        }
        int y0 = inner_y + (int)floor(((double)pitch_row / (double)pitch_span) * (double)inner_h);
        int y1 = inner_y + (int)ceil(((double)(pitch_row + 1) / (double)pitch_span) * (double)inner_h);
        if (y1 <= y0) {
            y1 = y0 + 1;
        }
        if (y1 - y0 < 2 && inner_h >= 2) {
            y1 = y0 + 2;
        }
        if (y1 > inner_y + inner_h) {
            y1 = inner_y + inner_h;
        }

        SDL_Rect rect = {x0, y0, x1 - x0, y1 - y0};
        SDL_Color note_color = midi_preview_velocity_color(note->velocity);
        SDL_SetRenderDrawColor(renderer, note_color.r, note_color.g, note_color.b, note_color.a);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, theme->text.r, theme->text.g, theme->text.b, 72);
        SDL_RenderDrawRect(renderer, &rect);
    }
    ui_set_blend_mode(renderer, SDL_BLENDMODE_NONE);
}
