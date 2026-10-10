#pragma once
#include "daw/fx_actions.h"
#include "engine/engine.h"
#define DAW_COMPOSITION_SECONDS 10
#define DAW_COMPOSITION_TAIL_SECONDS 5
#define DAW_COMPOSITION_POINTS 256
#define DAW_COMPOSITION_BINS 128
// Selects a bounded authored range and optional stable track/region, with explicit dry processing.
typedef struct DawCompositionRequest {
    bool has_target, dry;
    DawActionTarget target;
    uint64_t start, end, tail, window;
} DawCompositionRequest;
// Describes original-level whole-range metrics and bounded waveform, envelope and tonal spectrum plots.
typedef struct DawCompositionAnalysis {
    uint64_t window_frame, clipped_samples;
    double peak, rms, dc;
    float waveform[DAW_COMPOSITION_POINTS], envelope_rms[DAW_COMPOSITION_POINTS];
    float frequency_hz[DAW_COMPOSITION_BINS], spectrum_db[DAW_COMPOSITION_BINS];
} DawCompositionAnalysis;
// Owns the production preview buffer and captured chain provenance until the caller releases it.
typedef struct DawCompositionRender {
    EngineBounceBuffer audio;
    uint64_t generation;
    DawCompositionRequest request;
    DawFxSnapshot track_fx, master_fx;
    DawCompositionAnalysis analysis;
} DawCompositionRender;
// Captures, renders and measures the real project privately without normalization or authored mutation.
DawActionStatus daw_action_render_composition(struct AppState*, const DawCompositionRequest*, DawCompositionRender*);
// Frees caller-owned preview samples after analysis, publication or audition has consumed them.
void daw_composition_render_free(DawCompositionRender*);

// Distinguishes captured publication from overload, invalid data and occupied/private-storage failures.
typedef enum DawCompositionPublishStatus { DAW_COMPOSITION_PUBLISH_OK, DAW_COMPOSITION_PUBLISH_INVALID, DAW_COMPOSITION_PUBLISH_CLIPPING, DAW_COMPOSITION_PUBLISH_IO, DAW_COMPOSITION_PUBLISH_OCCUPIED } DawCompositionPublishStatus;
// Carries the selected original-level PCM artifact and its observed publication durability.
typedef struct DawCompositionPreview { char path[1024]; bool durable; } DawCompositionPreview;
// Publishes captured audio using a bounded controlled artifact ID without changing the project or sample levels.
DawCompositionPublishStatus daw_composition_publish(const DawCompositionRender*, const char* artifact, DawCompositionPreview*);
