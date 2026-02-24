#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Describes the storage type for an effect parameter.
typedef enum {
    FX_PARAM_TYPE_FLOAT = 0,
    FX_PARAM_TYPE_INT,
    FX_PARAM_TYPE_BOOL,
    FX_PARAM_TYPE_ENUM
} FxParamType;

// Describes the unit label and conversion expectations for an effect parameter.
typedef enum {
    FX_PARAM_UNIT_GENERIC = 0,
    FX_PARAM_UNIT_DB,
    FX_PARAM_UNIT_HZ,
    FX_PARAM_UNIT_MS,
    FX_PARAM_UNIT_SECONDS,
    FX_PARAM_UNIT_PERCENT,
    FX_PARAM_UNIT_RATIO
} FxParamUnit;

// Describes how UI-normalized values are mapped to the parameter range.
typedef enum {
    FX_PARAM_CURVE_LINEAR = 0,
    FX_PARAM_CURVE_LOG
} FxParamCurve;

// Hints the preferred UI widget for a parameter.
typedef enum {
    FX_PARAM_UI_SLIDER = 0,
    FX_PARAM_UI_KNOB,
    FX_PARAM_UI_TOGGLE,
    FX_PARAM_UI_DROPDOWN
} FxParamUiHint;

// Flags describing automation and visibility behavior for parameters.
typedef enum {
    FX_PARAM_FLAG_AUTOMATABLE = (1u << 0),
    FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE = (1u << 1),
    FX_PARAM_FLAG_READ_ONLY_METER = (1u << 2),
    FX_PARAM_FLAG_HIDDEN = (1u << 3)
} FxParamFlags;

// Describes a single effect parameter's metadata for UI and automation.
typedef struct {
    const char* id;
    const char* display_name;
    const char* group;
    const char* section;
    FxParamType type;
    FxParamUnit unit;
    float min_value;
    float max_value;
    float default_value;
    FxParamCurve curve;
    float step;
    FxParamUiHint ui_hint;
    uint32_t flags;
    float smoothing_ms;
    uint32_t enum_count;
    const char* enum_labels[8];
} EffectParamSpec;

#ifdef __cplusplus
} // extern "C"
#endif
