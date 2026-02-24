#pragma once

#include "effects/param_spec.h"

// SvfParamSpecs defines UI metadata for the SVF effect parameters.
static const EffectParamSpec kSvfParamSpecs[] = {
    {
        .id = "mode",
        .display_name = "Mode",
        .group = "Filter",
        .type = FX_PARAM_TYPE_ENUM,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 3.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_DROPDOWN,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .enum_count = 4,
        .enum_labels = {"LP", "HP", "BP", "Notch"}
    },
    {
        .id = "cutoff_hz",
        .display_name = "Cutoff",
        .group = "Filter",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 20.0f,
        .max_value = 20000.0f,
        .default_value = 1200.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "q",
        .display_name = "Q",
        .group = "Filter",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.3f,
        .max_value = 12.0f,
        .default_value = 0.707f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "gain_db",
        .display_name = "Gain",
        .group = "Filter",
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

#define SVF_PARAM_SPEC_COUNT (sizeof(kSvfParamSpecs) / sizeof(kSvfParamSpecs[0]))

// AutoWahParamSpecs defines UI metadata for the auto-wah effect parameters.
static const EffectParamSpec kAutoWahParamSpecs[] = {
    {
        .id = "min_hz",
        .display_name = "Min Hz",
        .group = "Filter",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 200.0f,
        .max_value = 2000.0f,
        .default_value = 300.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "max_hz",
        .display_name = "Max Hz",
        .group = "Filter",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 800.0f,
        .max_value = 6000.0f,
        .default_value = 2500.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "q",
        .display_name = "Q",
        .group = "Filter",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.5f,
        .max_value = 8.0f,
        .default_value = 2.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "attack_ms",
        .display_name = "Attack",
        .group = "Filter",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.1f,
        .max_value = 20.0f,
        .default_value = 2.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "release_ms",
        .display_name = "Release",
        .group = "Filter",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 5.0f,
        .max_value = 400.0f,
        .default_value = 120.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Filter",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.8f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define AUTOWAH_PARAM_SPEC_COUNT (sizeof(kAutoWahParamSpecs) / sizeof(kAutoWahParamSpecs[0]))

// StereoWidthParamSpecs defines UI metadata for the stereo width effect parameters.
static const EffectParamSpec kStereoWidthParamSpecs[] = {
    {
        .id = "width",
        .display_name = "Width",
        .group = "Stereo",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 2.0f,
        .default_value = 1.2f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "balance",
        .display_name = "Balance",
        .group = "Stereo",
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

#define STEREO_WIDTH_PARAM_SPEC_COUNT (sizeof(kStereoWidthParamSpecs) / sizeof(kStereoWidthParamSpecs[0]))

// TiltEqParamSpecs defines UI metadata for the tilt EQ effect parameters.
static const EffectParamSpec kTiltEqParamSpecs[] = {
    {
        .id = "pivot_hz",
        .display_name = "Pivot",
        .group = "Tone",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 100.0f,
        .max_value = 5000.0f,
        .default_value = 1000.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "tilt_db",
        .display_name = "Tilt",
        .group = "Tone",
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
        .id = "mix",
        .display_name = "Mix",
        .group = "Tone",
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

#define TILT_EQ_PARAM_SPEC_COUNT (sizeof(kTiltEqParamSpecs) / sizeof(kTiltEqParamSpecs[0]))

// PhaserParamSpecs defines UI metadata for the phaser effect parameters.
static const EffectParamSpec kPhaserParamSpecs[] = {
    {
        .id = "rate_hz",
        .display_name = "Rate",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 0.05f,
        .max_value = 5.0f,
        .default_value = 0.6f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "depth",
        .display_name = "Depth",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.9f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "center_hz",
        .display_name = "Center",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 200.0f,
        .max_value = 1800.0f,
        .default_value = 700.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "stages",
        .display_name = "Stages",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_INT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 4.0f,
        .max_value = 8.0f,
        .default_value = 6.0f,
        .step = 2.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE
    },
    {
        .id = "feedback",
        .display_name = "Feedback",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = -0.95f,
        .max_value = 0.95f,
        .default_value = 0.5f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Modulation",
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

#define PHASER_PARAM_SPEC_COUNT (sizeof(kPhaserParamSpecs) / sizeof(kPhaserParamSpecs[0]))

// FormantFilterParamSpecs defines UI metadata for the formant filter effect parameters.
static const EffectParamSpec kFormantFilterParamSpecs[] = {
    {
        .id = "vowel_idx",
        .display_name = "Vowel",
        .group = "Formant",
        .type = FX_PARAM_TYPE_ENUM,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 4.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_DROPDOWN,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .enum_count = 5,
        .enum_labels = {"A", "E", "I", "O", "U"}
    },
    {
        .id = "mod_rate_hz",
        .display_name = "Mod Rate",
        .group = "Formant",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 0.0f,
        .max_value = 5.0f,
        .default_value = 0.6f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "mod_depth",
        .display_name = "Mod Depth",
        .group = "Formant",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.5f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "q1",
        .display_name = "Q1",
        .group = "Formant",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 1.0f,
        .max_value = 20.0f,
        .default_value = 5.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "q2",
        .display_name = "Q2",
        .group = "Formant",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 1.0f,
        .max_value = 20.0f,
        .default_value = 10.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Formant",
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

#define FORMANT_FILTER_PARAM_SPEC_COUNT (sizeof(kFormantFilterParamSpecs) / sizeof(kFormantFilterParamSpecs[0]))

// CombFFParamSpecs defines UI metadata for the feed-forward comb filter parameters.
static const EffectParamSpec kCombFFParamSpecs[] = {
    {
        .id = "delay_ms",
        .display_name = "Delay",
        .group = "Comb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 1.0f,
        .max_value = 50.0f,
        .default_value = 10.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "gain",
        .display_name = "Gain",
        .group = "Comb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = -1.0f,
        .max_value = 1.0f,
        .default_value = 0.7f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Comb",
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

#define COMB_FF_PARAM_SPEC_COUNT (sizeof(kCombFFParamSpecs) / sizeof(kCombFFParamSpecs[0]))
