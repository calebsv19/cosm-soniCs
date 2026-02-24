#pragma once

#include "effects/param_spec.h"

// ReverbParamSpecs defines UI metadata for the Reverb effect parameters.
static const EffectParamSpec kReverbParamSpecs[] = {
    {
        .id = "size",
        .display_name = "Size",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.5f,
        .max_value = 1.5f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "decay_rt60",
        .display_name = "Decay",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_SECONDS,
        .min_value = 0.1f,
        .max_value = 10.0f,
        .default_value = 2.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 30.0f
    },
    {
        .id = "damping",
        .display_name = "Damping",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.3f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "predelay_ms",
        .display_name = "Predelay",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.0f,
        .max_value = 100.0f,
        .default_value = 20.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.2f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define REVERB_PARAM_SPEC_COUNT (sizeof(kReverbParamSpecs) / sizeof(kReverbParamSpecs[0]))

// EarlyReflectionsParamSpecs defines UI metadata for the EarlyReflections effect parameters.
static const EffectParamSpec kEarlyReflectionsParamSpecs[] = {
    {
        .id = "predelay_ms",
        .display_name = "Predelay",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.0f,
        .max_value = 100.0f,
        .default_value = 10.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "width",
        .display_name = "Width",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.5f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.25f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define EARLY_REFLECTIONS_PARAM_SPEC_COUNT (sizeof(kEarlyReflectionsParamSpecs) / sizeof(kEarlyReflectionsParamSpecs[0]))

// PlateLiteParamSpecs defines UI metadata for the PlateLite effect parameters.
static const EffectParamSpec kPlateLiteParamSpecs[] = {
    {
        .id = "size",
        .display_name = "Size",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.6f,
        .max_value = 1.4f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "decay_rt60",
        .display_name = "Decay",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_SECONDS,
        .min_value = 0.2f,
        .max_value = 8.0f,
        .default_value = 2.5f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 30.0f
    },
    {
        .id = "highcut_hz",
        .display_name = "Highcut",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 2000.0f,
        .max_value = 16000.0f,
        .default_value = 12000.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.25f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define PLATE_LITE_PARAM_SPEC_COUNT (sizeof(kPlateLiteParamSpecs) / sizeof(kPlateLiteParamSpecs[0]))

// GatedReverbParamSpecs defines UI metadata for the GatedReverb effect parameters.
static const EffectParamSpec kGatedReverbParamSpecs[] = {
    {
        .id = "size",
        .display_name = "Size",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.6f,
        .max_value = 1.4f,
        .default_value = 0.9f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "decay_rt60",
        .display_name = "Decay",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_SECONDS,
        .min_value = 0.2f,
        .max_value = 4.0f,
        .default_value = 1.2f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 30.0f
    },
    {
        .id = "thresh_db",
        .display_name = "Threshold",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -60.0f,
        .max_value = 0.0f,
        .default_value = -24.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "hold_ms",
        .display_name = "Hold",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.0f,
        .max_value = 500.0f,
        .default_value = 120.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "release_ms",
        .display_name = "Release",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 5.0f,
        .max_value = 800.0f,
        .default_value = 220.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Reverb",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.35f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define GATED_REVERB_PARAM_SPEC_COUNT (sizeof(kGatedReverbParamSpecs) / sizeof(kGatedReverbParamSpecs[0]))
