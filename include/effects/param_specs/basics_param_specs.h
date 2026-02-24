#pragma once

#include "effects/param_spec.h"

// GainParamSpecs defines UI metadata for the Gain effect parameters.
static const EffectParamSpec kGainParamSpecs[] = {
    {
        .id = "gain_db",
        .display_name = "Gain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -96.0f,
        .max_value = 24.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define GAIN_PARAM_SPEC_COUNT (sizeof(kGainParamSpecs) / sizeof(kGainParamSpecs[0]))

// PanParamSpecs defines UI metadata for the Pan effect parameters.
static const EffectParamSpec kPanParamSpecs[] = {
    {
        .id = "pan",
        .display_name = "Pan",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = -1.0f,
        .max_value = 1.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define PAN_PARAM_SPEC_COUNT (sizeof(kPanParamSpecs) / sizeof(kPanParamSpecs[0]))

// DCBlockParamSpecs defines UI metadata for the DCBlock effect parameters.
static const EffectParamSpec kDcBlockParamSpecs[] = {
    {
        .id = "cutoff_hz",
        .display_name = "Cutoff",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 5.0f,
        .max_value = 60.0f,
        .default_value = 20.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define DCBLOCK_PARAM_SPEC_COUNT (sizeof(kDcBlockParamSpecs) / sizeof(kDcBlockParamSpecs[0]))

// MuteParamSpecs defines UI metadata for the Mute effect parameters.
static const EffectParamSpec kMuteParamSpecs[] = {
    {
        .id = "mute",
        .display_name = "Mute",
        .type = FX_PARAM_TYPE_BOOL,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_TOGGLE,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 0.0f
    },
    {
        .id = "ramp_ms",
        .display_name = "Ramp",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.1f,
        .max_value = 50.0f,
        .default_value = 5.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    }
};

#define MUTE_PARAM_SPEC_COUNT (sizeof(kMuteParamSpecs) / sizeof(kMuteParamSpecs[0]))

// MonoMakerParamSpecs defines UI metadata for the MonoMakerLow effect parameters.
static const EffectParamSpec kMonoMakerParamSpecs[] = {
    {
        .id = "crossover_hz",
        .display_name = "Crossover",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 60.0f,
        .max_value = 500.0f,
        .default_value = 150.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define MONOMAKER_PARAM_SPEC_COUNT (sizeof(kMonoMakerParamSpecs) / sizeof(kMonoMakerParamSpecs[0]))

// StereoBlendParamSpecs defines UI metadata for the StereoBlend effect parameters.
static const EffectParamSpec kStereoBlendParamSpecs[] = {
    {
        .id = "balance",
        .display_name = "Balance",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = -1.0f,
        .max_value = 1.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "keep_stereo",
        .display_name = "Keep Stereo",
        .type = FX_PARAM_TYPE_BOOL,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_TOGGLE,
        .flags = FX_PARAM_FLAG_AUTOMATABLE
    }
};

#define STEREOBLEND_PARAM_SPEC_COUNT (sizeof(kStereoBlendParamSpecs) / sizeof(kStereoBlendParamSpecs[0]))

// AutoTrimParamSpecs defines UI metadata for the AutoTrim effect parameters.
static const EffectParamSpec kAutoTrimParamSpecs[] = {
    {
        .id = "target_db",
        .display_name = "Target",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -36.0f,
        .max_value = -6.0f,
        .default_value = -18.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "speed_ms",
        .display_name = "Speed",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 5.0f,
        .max_value = 1000.0f,
        .default_value = 150.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "max_gain_db",
        .display_name = "Max Gain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = 0.0f,
        .max_value = 36.0f,
        .default_value = 24.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "gate_thresh_db",
        .display_name = "Gate Threshold",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -90.0f,
        .max_value = -30.0f,
        .default_value = -60.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    }
};

#define AUTOTRIM_PARAM_SPEC_COUNT (sizeof(kAutoTrimParamSpecs) / sizeof(kAutoTrimParamSpecs[0]))
