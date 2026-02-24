#pragma once

#include "effects/param_spec.h"

// HardClipParamSpecs defines UI metadata for the HardClip effect parameters.
static const EffectParamSpec kHardClipParamSpecs[] = {
    {
        .id = "threshold_db",
        .display_name = "Threshold",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -36.0f,
        .max_value = 0.0f,
        .default_value = -6.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "makeup_db",
        .display_name = "Makeup",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -12.0f,
        .max_value = 12.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
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

#define HARDCLIP_PARAM_SPEC_COUNT (sizeof(kHardClipParamSpecs) / sizeof(kHardClipParamSpecs[0]))

// SoftSatParamSpecs defines UI metadata for the SoftSaturation effect parameters.
static const EffectParamSpec kSoftSatParamSpecs[] = {
    {
        .id = "drive",
        .display_name = "Drive",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = 0.0f,
        .max_value = 24.0f,
        .default_value = 6.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "makeup_db",
        .display_name = "Makeup",
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

#define SOFTSAT_PARAM_SPEC_COUNT (sizeof(kSoftSatParamSpecs) / sizeof(kSoftSatParamSpecs[0]))

// BitCrusherParamSpecs defines UI metadata for the BitCrusher effect parameters.
static const EffectParamSpec kBitCrusherParamSpecs[] = {
    {
        .id = "bits",
        .display_name = "Bits",
        .type = FX_PARAM_TYPE_INT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 2.0f,
        .max_value = 16.0f,
        .default_value = 8.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .step = 1.0f
    },
    {
        .id = "srrate",
        .display_name = "Sample Rate",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.01f,
        .max_value = 1.0f,
        .default_value = 0.25f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
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

#define BITCRUSHER_PARAM_SPEC_COUNT (sizeof(kBitCrusherParamSpecs) / sizeof(kBitCrusherParamSpecs[0]))

// OverdriveParamSpecs defines UI metadata for the Overdrive effect parameters.
static const EffectParamSpec kOverdriveParamSpecs[] = {
    {
        .id = "drive_db",
        .display_name = "Drive",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = 0.0f,
        .max_value = 36.0f,
        .default_value = 12.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "tone",
        .display_name = "Tone",
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

#define OVERDRIVE_PARAM_SPEC_COUNT (sizeof(kOverdriveParamSpecs) / sizeof(kOverdriveParamSpecs[0]))

// WaveshaperParamSpecs defines UI metadata for the Waveshaper effect parameters.
static const EffectParamSpec kWaveshaperParamSpecs[] = {
    {
        .id = "curve",
        .display_name = "Curve",
        .type = FX_PARAM_TYPE_ENUM,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 4.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_DROPDOWN,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .enum_count = 5,
        .enum_labels = {"Tanh", "Arctan", "SoftClip", "Foldback", "SineFold"}
    },
    {
        .id = "drive_db",
        .display_name = "Drive",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = 0.0f,
        .max_value = 36.0f,
        .default_value = 12.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "bias",
        .display_name = "Bias",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = -1.0f,
        .max_value = 1.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "out_db",
        .display_name = "Out",
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

#define WAVESHAPER_PARAM_SPEC_COUNT (sizeof(kWaveshaperParamSpecs) / sizeof(kWaveshaperParamSpecs[0]))

// DecimatorParamSpecs defines UI metadata for the Decimator effect parameters.
static const EffectParamSpec kDecimatorParamSpecs[] = {
    {
        .id = "hold_n",
        .display_name = "Hold",
        .type = FX_PARAM_TYPE_INT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 1.0f,
        .max_value = 64.0f,
        .default_value = 6.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .step = 1.0f
    },
    {
        .id = "bit_depth",
        .display_name = "Bit Depth",
        .type = FX_PARAM_TYPE_INT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 4.0f,
        .max_value = 24.0f,
        .default_value = 10.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .step = 1.0f
    },
    {
        .id = "jitter",
        .display_name = "Jitter",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.15f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "post_lowpass_hz",
        .display_name = "Post LP",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 1000.0f,
        .max_value = 20000.0f,
        .default_value = 9000.0f,
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

#define DECIMATOR_PARAM_SPEC_COUNT (sizeof(kDecimatorParamSpecs) / sizeof(kDecimatorParamSpecs[0]))
