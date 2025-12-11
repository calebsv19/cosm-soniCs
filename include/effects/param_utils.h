#pragma once

#include <stdbool.h>
#include "effects/effects_manager.h"
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

// Conversion helpers between native units and beat-based values.
float fx_param_native_to_beats(FxParamKind kind, float native_value, const TempoState* tempo);
float fx_param_beats_to_native(FxParamKind kind, float beat_value, const TempoState* tempo);

// Quantize beat values to a stable musical grid (default 1/16 notes).
float fx_param_quantize_beats(float beat_value);

