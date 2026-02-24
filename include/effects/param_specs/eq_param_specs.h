#pragma once

#include "effects/param_spec.h"

// BiquadEqParamSpecs defines UI metadata for the BiquadEQ effect parameters.
static const EffectParamSpec kBiquadEqParamSpecs[] = {
    {
        .id = "type",
        .display_name = "Type",
        .group = "Band",
        .type = FX_PARAM_TYPE_ENUM,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 2.0f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_DROPDOWN,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .enum_count = 3,
        .enum_labels = {"LowShelf", "Peak", "HighShelf"}
    },
    {
        .id = "freq_hz",
        .display_name = "Freq",
        .group = "Band",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 10.0f,
        .max_value = 20000.0f,
        .default_value = 1000.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "q",
        .display_name = "Q",
        .group = "Band",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.1f,
        .max_value = 24.0f,
        .default_value = 0.707f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "gain_db",
        .display_name = "Gain",
        .group = "Band",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -24.0f,
        .max_value = 24.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define BIQUAD_EQ_PARAM_SPEC_COUNT (sizeof(kBiquadEqParamSpecs) / sizeof(kBiquadEqParamSpecs[0]))

// EqFixed3ParamSpecs defines UI metadata for the fixed 3-band EQ effect parameters.
static const EffectParamSpec kEqFixed3ParamSpecs[] = {
    {
        .id = "low_gain_db",
        .display_name = "Low Gain",
        .group = "Low",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -15.0f,
        .max_value = 15.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mid_gain_db",
        .display_name = "Mid Gain",
        .group = "Mid",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -15.0f,
        .max_value = 15.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "high_gain_db",
        .display_name = "High Gain",
        .group = "High",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -15.0f,
        .max_value = 15.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mid_q",
        .display_name = "Mid Q",
        .group = "Mid",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.3f,
        .max_value = 4.0f,
        .default_value = 0.707f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define EQ_FIXED3_PARAM_SPEC_COUNT (sizeof(kEqFixed3ParamSpecs) / sizeof(kEqFixed3ParamSpecs[0]))

// EqNotchParamSpecs defines UI metadata for the EQ_Notch effect parameters.
static const EffectParamSpec kEqNotchParamSpecs[] = {
    {
        .id = "freq_hz",
        .display_name = "Freq",
        .group = "Notch",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 40.0f,
        .max_value = 18000.0f,
        .default_value = 60.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "q",
        .display_name = "Q",
        .group = "Notch",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 1.0f,
        .max_value = 30.0f,
        .default_value = 10.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "depth_db",
        .display_name = "Depth",
        .group = "Notch",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -30.0f,
        .max_value = 0.0f,
        .default_value = -24.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define EQ_NOTCH_PARAM_SPEC_COUNT (sizeof(kEqNotchParamSpecs) / sizeof(kEqNotchParamSpecs[0]))

// EqTiltParamSpecs defines UI metadata for the EQ_Tilt effect parameters.
static const EffectParamSpec kEqTiltParamSpecs[] = {
    {
        .id = "tilt_db",
        .display_name = "Tilt",
        .group = "Tilt",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -12.0f,
        .max_value = 12.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "pivot_hz",
        .display_name = "Pivot",
        .group = "Tilt",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 200.0f,
        .max_value = 2000.0f,
        .default_value = 650.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define EQ_TILT_PARAM_SPEC_COUNT (sizeof(kEqTiltParamSpecs) / sizeof(kEqTiltParamSpecs[0]))
