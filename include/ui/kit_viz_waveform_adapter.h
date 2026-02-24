#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include "audio/media_clip.h"
#include "ui/timeline_waveform.h"

typedef struct DawKitVizWaveformRequest {
    SDL_Renderer *renderer;
    WaveformCache *cache;
    const AudioMediaClip *clip;
    const char *source_path;
    const SDL_Rect *target_rect;
    uint64_t view_start_frame;
    uint64_t view_frame_count;
    SDL_Color color;
} DawKitVizWaveformRequest;

typedef enum DawKitVizWaveformResult {
    DAW_KIT_VIZ_WAVEFORM_RENDERED = 0,
    DAW_KIT_VIZ_WAVEFORM_INVALID_REQUEST,
    DAW_KIT_VIZ_WAVEFORM_MISSING_CACHE,
    DAW_KIT_VIZ_WAVEFORM_SAMPLING_FAILED
} DawKitVizWaveformResult;

// Extended render call that returns diagnostics-friendly result codes.
DawKitVizWaveformResult daw_kit_viz_render_waveform_ex(const DawKitVizWaveformRequest *request);

// DAW-facing adapter boundary for kit_viz waveform rendering.
// In Phase 6A Step 1 this delegates to existing waveform path to keep behavior stable.
bool daw_kit_viz_render_waveform(const DawKitVizWaveformRequest *request);
