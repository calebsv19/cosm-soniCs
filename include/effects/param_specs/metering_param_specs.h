#pragma once

#include "effects/param_spec.h"

// CorrelationMeterParamSpecs defines future-facing control metadata for the correlation meter.
static const EffectParamSpec kCorrelationMeterParamSpecs[] = {
    {
        .id = "window_ms",
        .display_name = "Window",
        .group = "Analysis",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 10.0f,
        .max_value = 2000.0f,
        .default_value = 300.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_HIDDEN
    },
    {
        .id = "smooth_ms",
        .display_name = "Smoothing",
        .group = "Analysis",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 5.0f,
        .max_value = 1000.0f,
        .default_value = 120.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_HIDDEN
    }
};

#define CORRELATION_METER_PARAM_SPEC_COUNT (sizeof(kCorrelationMeterParamSpecs) / sizeof(kCorrelationMeterParamSpecs[0]))

// MidSideMeterParamSpecs defines future-facing control metadata for the mid/side meter.
static const EffectParamSpec kMidSideMeterParamSpecs[] = {
    {
        .id = "window_ms",
        .display_name = "Window",
        .group = "Analysis",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 10.0f,
        .max_value = 2000.0f,
        .default_value = 200.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_HIDDEN
    },
    {
        .id = "smooth_ms",
        .display_name = "Smoothing",
        .group = "Analysis",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 5.0f,
        .max_value = 1000.0f,
        .default_value = 80.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_HIDDEN
    }
};

#define MID_SIDE_METER_PARAM_SPEC_COUNT (sizeof(kMidSideMeterParamSpecs) / sizeof(kMidSideMeterParamSpecs[0]))

// VectorScopeParamSpecs defines future-facing control metadata for vectorscope analysis.
static const EffectParamSpec kVectorScopeParamSpecs[] = {
    {
        .id = "mode",
        .display_name = "Mode",
        .group = "Scope",
        .type = FX_PARAM_TYPE_ENUM,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_DROPDOWN,
        .flags = FX_PARAM_FLAG_HIDDEN,
        .enum_count = 2,
        .enum_labels = {"Mid/Side", "Left/Right"}
    },
    {
        .id = "decay_ms",
        .display_name = "Decay",
        .group = "Scope",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 10.0f,
        .max_value = 2000.0f,
        .default_value = 350.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_HIDDEN
    }
};

#define VECTOR_SCOPE_PARAM_SPEC_COUNT (sizeof(kVectorScopeParamSpecs) / sizeof(kVectorScopeParamSpecs[0]))

// PeakRmsMeterParamSpecs defines future-facing control metadata for peak/RMS metering.
static const EffectParamSpec kPeakRmsMeterParamSpecs[] = {
    {
        .id = "rms_window_ms",
        .display_name = "RMS Window",
        .group = "Analysis",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 10.0f,
        .max_value = 1000.0f,
        .default_value = 300.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_HIDDEN
    },
    {
        .id = "peak_hold_ms",
        .display_name = "Peak Hold",
        .group = "Analysis",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.0f,
        .max_value = 2000.0f,
        .default_value = 300.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_HIDDEN
    },
    {
        .id = "range_db",
        .display_name = "Range",
        .group = "Display",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = 24.0f,
        .max_value = 120.0f,
        .default_value = 60.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_HIDDEN
    }
};

#define PEAK_RMS_METER_PARAM_SPEC_COUNT (sizeof(kPeakRmsMeterParamSpecs) / sizeof(kPeakRmsMeterParamSpecs[0]))

// LufsMeterParamSpecs defines future-facing control metadata for LUFS analysis.
static const EffectParamSpec kLufsMeterParamSpecs[] = {
    {
        .id = "mode",
        .display_name = "Mode",
        .group = "LUFS",
        .type = FX_PARAM_TYPE_ENUM,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 2.0f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_DROPDOWN,
        .flags = FX_PARAM_FLAG_HIDDEN,
        .enum_count = 3,
        .enum_labels = {"Integrated", "Short-Term", "Momentary"}
    },
    {
        .id = "gate_db",
        .display_name = "Gate",
        .group = "LUFS",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -70.0f,
        .max_value = -10.0f,
        .default_value = -70.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_HIDDEN
    }
};

#define LUFS_METER_PARAM_SPEC_COUNT (sizeof(kLufsMeterParamSpecs) / sizeof(kLufsMeterParamSpecs[0]))

// SpectrogramMeterParamSpecs defines future-facing control metadata for spectrogram displays.
static const EffectParamSpec kSpectrogramMeterParamSpecs[] = {
    {
        .id = "floor_db",
        .display_name = "Floor",
        .group = "Display",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -120.0f,
        .max_value = -20.0f,
        .default_value = -80.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_HIDDEN
    },
    {
        .id = "ceil_db",
        .display_name = "Ceiling",
        .group = "Display",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -24.0f,
        .max_value = 12.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_HIDDEN
    },
    {
        .id = "palette",
        .display_name = "Palette",
        .group = "Display",
        .type = FX_PARAM_TYPE_ENUM,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 2.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_DROPDOWN,
        .flags = FX_PARAM_FLAG_HIDDEN,
        .enum_count = 3,
        .enum_labels = {"White/Black", "Black/White", "Heat"}
    }
};

#define SPECTROGRAM_METER_PARAM_SPEC_COUNT (sizeof(kSpectrogramMeterParamSpecs) / sizeof(kSpectrogramMeterParamSpecs[0]))
