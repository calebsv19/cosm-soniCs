#pragma once

#include "effects/param_spec.h"

// DelayParamSpecs defines UI metadata for the Delay effect parameters.
static const EffectParamSpec kDelayParamSpecs[] = {
    {
        .id = "time_ms",
        .display_name = "Time",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 1.0f,
        .max_value = 2000.0f,
        .default_value = 400.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "feedback",
        .display_name = "Feedback",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 0.95f,
        .default_value = 0.35f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Delay",
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

#define DELAY_PARAM_SPEC_COUNT (sizeof(kDelayParamSpecs) / sizeof(kDelayParamSpecs[0]))

// PingPongParamSpecs defines UI metadata for the PingPongDelay effect parameters.
static const EffectParamSpec kPingPongParamSpecs[] = {
    {
        .id = "time_ms",
        .display_name = "Time",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 1.0f,
        .max_value = 2000.0f,
        .default_value = 450.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "feedback",
        .display_name = "Feedback",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 0.95f,
        .default_value = 0.45f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.4f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define PINGPONG_PARAM_SPEC_COUNT (sizeof(kPingPongParamSpecs) / sizeof(kPingPongParamSpecs[0]))

// MultiTapParamSpecs defines UI metadata for the MultiTapDelay effect parameters.
static const EffectParamSpec kMultiTapParamSpecs[] = {
    {
        .id = "base_time_ms",
        .display_name = "Base Time",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 1.0f,
        .max_value = 1500.0f,
        .default_value = 300.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "feedback",
        .display_name = "Feedback",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 0.9f,
        .default_value = 0.3f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.35f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "tap2_mul",
        .display_name = "Tap 2 Mul",
        .group = "Taps",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_RATIO,
        .min_value = 0.0f,
        .max_value = 4.0f,
        .default_value = 2.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "tap3_mul",
        .display_name = "Tap 3 Mul",
        .group = "Taps",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_RATIO,
        .min_value = 0.0f,
        .max_value = 4.0f,
        .default_value = 3.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "tap4_mul",
        .display_name = "Tap 4 Mul",
        .group = "Taps",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_RATIO,
        .min_value = 0.0f,
        .max_value = 4.0f,
        .default_value = 4.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "tap1_gain",
        .display_name = "Tap 1",
        .group = "Tap Gain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "tap2_gain",
        .display_name = "Tap 2",
        .group = "Tap Gain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.8f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "tap3_gain",
        .display_name = "Tap 3",
        .group = "Tap Gain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.6f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "tap4_gain",
        .display_name = "Tap 4",
        .group = "Tap Gain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.5f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define MULTITAP_PARAM_SPEC_COUNT (sizeof(kMultiTapParamSpecs) / sizeof(kMultiTapParamSpecs[0]))

// TapeEchoParamSpecs defines UI metadata for the TapeEcho effect parameters.
static const EffectParamSpec kTapeEchoParamSpecs[] = {
    {
        .id = "time_ms",
        .display_name = "Time",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 20.0f,
        .max_value = 1500.0f,
        .default_value = 450.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "feedback",
        .display_name = "Feedback",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 0.97f,
        .default_value = 0.55f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.35f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "wobble_hz",
        .display_name = "Wobble",
        .group = "Tape",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 0.0f,
        .max_value = 5.0f,
        .default_value = 0.4f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "wobble_depth_ms",
        .display_name = "Wobble Depth",
        .group = "Tape",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.0f,
        .max_value = 12.0f,
        .default_value = 3.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "highcut_hz",
        .display_name = "Highcut",
        .group = "Tone",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 1000.0f,
        .max_value = 12000.0f,
        .default_value = 6000.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "sat",
        .display_name = "Saturation",
        .group = "Tape",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.3f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define TAPE_ECHO_PARAM_SPEC_COUNT (sizeof(kTapeEchoParamSpecs) / sizeof(kTapeEchoParamSpecs[0]))

// DiffusionDelayParamSpecs defines UI metadata for the DiffusionDelay effect parameters.
static const EffectParamSpec kDiffusionDelayParamSpecs[] = {
    {
        .id = "delay_ms",
        .display_name = "Delay",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 20.0f,
        .max_value = 800.0f,
        .default_value = 120.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "feedback",
        .display_name = "Feedback",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 0.95f,
        .default_value = 0.6f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "diffusion",
        .display_name = "Diffusion",
        .group = "Diffusion",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 0.9f,
        .default_value = 0.6f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "stages",
        .display_name = "Stages",
        .group = "Diffusion",
        .type = FX_PARAM_TYPE_INT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 1.0f,
        .max_value = 4.0f,
        .default_value = 2.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .step = 1.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Delay",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.35f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "highcut_hz",
        .display_name = "Highcut",
        .group = "Tone",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 2000.0f,
        .max_value = 14000.0f,
        .default_value = 8000.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define DIFFUSION_DELAY_PARAM_SPEC_COUNT (sizeof(kDiffusionDelayParamSpecs) / sizeof(kDiffusionDelayParamSpecs[0]))
