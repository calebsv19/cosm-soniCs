#pragma once

#include <SDL2/SDL.h>

#include "config.h"

#define INSPECTOR_FADE_PRESET_COUNT CONFIG_FADE_PRESET_MAX

struct AppState;

// Defines a label/value row in the clip inspector left column.
typedef struct {
    SDL_Rect label_rect;
    SDL_Rect value_rect;
} ClipInspectorRow;

typedef enum {
    CLIP_INSPECTOR_ROW_NAME = 0,
    CLIP_INSPECTOR_ROW_SOURCE,
    CLIP_INSPECTOR_ROW_FORMAT,
    CLIP_INSPECTOR_ROW_TIMELINE_START,
    CLIP_INSPECTOR_ROW_TIMELINE_END,
    CLIP_INSPECTOR_ROW_TIMELINE_LENGTH,
    CLIP_INSPECTOR_ROW_SOURCE_START,
    CLIP_INSPECTOR_ROW_SOURCE_END,
    CLIP_INSPECTOR_ROW_PLAYBACK_RATE,
    CLIP_INSPECTOR_ROW_GAIN,
    CLIP_INSPECTOR_ROW_FADE_IN,
    CLIP_INSPECTOR_ROW_FADE_OUT,
    CLIP_INSPECTOR_ROW_PHASE,
    CLIP_INSPECTOR_ROW_NORMALIZE,
    CLIP_INSPECTOR_ROW_REVERSE,
    CLIP_INSPECTOR_ROW_COUNT
} ClipInspectorRowType;

// Stores the computed geometry for the clip inspector panel.
typedef struct {
    SDL_Rect panel_rect;
    SDL_Rect left_rect;
    SDL_Rect right_rect;
    SDL_Rect right_header_rect;
    SDL_Rect right_mode_source_rect;
    SDL_Rect right_mode_clip_rect;
    SDL_Rect right_waveform_rect;
    SDL_Rect right_detail_rect;
    SDL_Rect name_rect;
    SDL_Rect gain_track_rect;
    SDL_Rect gain_fill_rect;
    SDL_Rect gain_handle_rect;
    SDL_Rect fade_in_track_rect;
    SDL_Rect fade_in_fill_rect;
    SDL_Rect fade_in_handle_rect;
    SDL_Rect fade_out_track_rect;
    SDL_Rect fade_out_fill_rect;
    SDL_Rect fade_out_handle_rect;
    SDL_Rect fade_in_buttons[INSPECTOR_FADE_PRESET_COUNT];
    SDL_Rect fade_out_buttons[INSPECTOR_FADE_PRESET_COUNT];
    SDL_Rect fade_preset_label_rect;
    ClipInspectorRow rows[CLIP_INSPECTOR_ROW_COUNT];
    int fade_preset_count;
    float fade_presets_ms[INSPECTOR_FADE_PRESET_COUNT];
} ClipInspectorLayout;

enum { CLIP_INSPECTOR_NAME_MIN_VISIBLE_CHARS = 10 };

void clip_inspector_compute_layout(const struct AppState* state, ClipInspectorLayout* layout);
// Renders the clip inspector panel, including waveform view content.
void clip_inspector_render(SDL_Renderer* renderer, struct AppState* state, const ClipInspectorLayout* layout);
