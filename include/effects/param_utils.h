#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "effects/effects_manager.h"
#include "effects/param_spec.h"
#include "time/tempo.h"

// Classification of FX parameters so we know when tempo sync is meaningful.
typedef enum {
    FX_PARAM_KIND_GENERIC = 0,
    FX_PARAM_KIND_TIME_MS,
    FX_PARAM_KIND_TIME_SECONDS,
    FX_PARAM_KIND_RATE_HZ
} FxParamKind;

// Heuristically derive the parameter kind from its name (e.g., "_ms", "rate_hz").
FxParamKind fx_param_kind_from_name(const char* name);
bool        fx_param_kind_is_syncable(FxParamKind kind);

// Returns true if a param spec can be tempo-synced.
bool fx_param_spec_is_syncable(const EffectParamSpec* spec);

// Conversion helpers between native units and beat-based values.
float fx_param_native_to_beats(FxParamKind kind, float native_value, const TempoState* tempo);
float fx_param_beats_to_native(FxParamKind kind, float beat_value, const TempoState* tempo);

// Conversion helpers between native units and beat-based values using param specs.
float fx_param_spec_native_to_beats(const EffectParamSpec* spec, float native_value, const TempoState* tempo);
float fx_param_spec_beats_to_native(const EffectParamSpec* spec, float beat_value, const TempoState* tempo);
// Derives beat-domain min/max bounds from a param spec and tempo state.
bool fx_param_spec_get_beat_bounds(const EffectParamSpec* spec,
                                   const TempoState* tempo,
                                   float* out_min_beats,
                                   float* out_max_beats);

// Quantize beat values to a stable musical grid (default 1/16 notes).
float fx_param_quantize_beats(float beat_value);

// Formats a parameter value for UI display based on spec metadata.
void fx_param_format_value(const EffectParamSpec* spec, float value, char* out, size_t out_size);

// Maps a normalized UI value to a parameter value using spec curve metadata.
float fx_param_map_ui_to_value(const EffectParamSpec* spec, float t);

// Maps a parameter value to a normalized UI value using spec curve metadata.
float fx_param_map_value_to_ui(const EffectParamSpec* spec, float value);

// Finds the index for a parameter id within a spec array.
int fx_param_spec_find_index(const EffectParamSpec* specs, uint32_t count, const char* id);
