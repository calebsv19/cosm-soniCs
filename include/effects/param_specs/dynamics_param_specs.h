#pragma once

#include "effects/param_spec.h"

// LimiterParamSpecs defines UI metadata for the Limiter effect parameters.
static const EffectParamSpec kLimiterParamSpecs[] = {
    {
        .id = "ceiling_db",
        .display_name = "Ceiling",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -24.0f,
        .max_value = 0.0f,
        .default_value = -0.3f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "lookahead_ms",
        .display_name = "Lookahead",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.0f,
        .max_value = 3.0f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "release_ms",
        .display_name = "Release",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 5.0f,
        .max_value = 200.0f,
        .default_value = 50.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    }
};

#define LIMITER_PARAM_SPEC_COUNT (sizeof(kLimiterParamSpecs) / sizeof(kLimiterParamSpecs[0]))

// GateParamSpecs defines UI metadata for the Gate effect parameters.
static const EffectParamSpec kGateParamSpecs[] = {
    {
        .id = "threshold_db",
        .display_name = "Threshold",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -90.0f,
        .max_value = 0.0f,
        .default_value = -50.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "ratio",
        .display_name = "Ratio",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_RATIO,
        .min_value = 1.0f,
        .max_value = 8.0f,
        .default_value = 4.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "attack_ms",
        .display_name = "Attack",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.1f,
        .max_value = 50.0f,
        .default_value = 3.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "release_ms",
        .display_name = "Release",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 5.0f,
        .max_value = 500.0f,
        .default_value = 120.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "hold_ms",
        .display_name = "Hold",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.0f,
        .max_value = 200.0f,
        .default_value = 20.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    }
};

#define GATE_PARAM_SPEC_COUNT (sizeof(kGateParamSpecs) / sizeof(kGateParamSpecs[0]))

// DeEsserParamSpecs defines UI metadata for the DeEsser effect parameters.
static const EffectParamSpec kDeEsserParamSpecs[] = {
    {
        .id = "center_hz",
        .display_name = "Center",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 3000.0f,
        .max_value = 12000.0f,
        .default_value = 6000.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "q",
        .display_name = "Q",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.5f,
        .max_value = 6.0f,
        .default_value = 2.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "threshold_db",
        .display_name = "Threshold",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -60.0f,
        .max_value = 0.0f,
        .default_value = -25.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "ratio",
        .display_name = "Ratio",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_RATIO,
        .min_value = 1.0f,
        .max_value = 10.0f,
        .default_value = 4.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "attack_ms",
        .display_name = "Attack",
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
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 5.0f,
        .max_value = 200.0f,
        .default_value = 80.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
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
        .smoothing_ms = 20.0f
    },
    {
        .id = "band_only",
        .display_name = "Band Only",
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

#define DEESSER_PARAM_SPEC_COUNT (sizeof(kDeEsserParamSpecs) / sizeof(kDeEsserParamSpecs[0]))

// SidechainCompressorParamSpecs defines UI metadata for the sidechain compressor effect parameters.
static const EffectParamSpec kSidechainCompressorParamSpecs[] = {
    {
        .id = "threshold_db",
        .display_name = "Threshold",
        .group = "Sidechain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -60.0f,
        .max_value = 0.0f,
        .default_value = -18.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "ratio",
        .display_name = "Ratio",
        .group = "Sidechain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_RATIO,
        .min_value = 1.0f,
        .max_value = 20.0f,
        .default_value = 4.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "attack_ms",
        .display_name = "Attack",
        .group = "Sidechain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.1f,
        .max_value = 100.0f,
        .default_value = 5.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "release_ms",
        .display_name = "Release",
        .group = "Sidechain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 5.0f,
        .max_value = 500.0f,
        .default_value = 80.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "makeup_db",
        .display_name = "Makeup",
        .group = "Sidechain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -24.0f,
        .max_value = 24.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "knee_db",
        .display_name = "Knee",
        .group = "Sidechain",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = 0.0f,
        .max_value = 24.0f,
        .default_value = 6.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "detector",
        .display_name = "Detector",
        .group = "Sidechain",
        .type = FX_PARAM_TYPE_ENUM,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_DROPDOWN,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .enum_count = 2,
        .enum_labels = {"Peak", "RMS"}
    }
};

#define SIDECHAIN_COMP_PARAM_SPEC_COUNT (sizeof(kSidechainCompressorParamSpecs) / sizeof(kSidechainCompressorParamSpecs[0]))

// UpwardCompParamSpecs defines UI metadata for the upward compressor effect parameters.
static const EffectParamSpec kUpwardCompParamSpecs[] = {
    {
        .id = "threshold_db",
        .display_name = "Threshold",
        .group = "Upward",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -60.0f,
        .max_value = 0.0f,
        .default_value = -24.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "ratio",
        .display_name = "Ratio",
        .group = "Upward",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_RATIO,
        .min_value = 1.0f,
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
        .group = "Upward",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 2.0f,
        .max_value = 50.0f,
        .default_value = 10.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "release_ms",
        .display_name = "Release",
        .group = "Upward",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 20.0f,
        .max_value = 500.0f,
        .default_value = 120.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Upward",
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
        .id = "makeup_db",
        .display_name = "Makeup",
        .group = "Upward",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = 0.0f,
        .max_value = 24.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define UPWARD_COMP_PARAM_SPEC_COUNT (sizeof(kUpwardCompParamSpecs) / sizeof(kUpwardCompParamSpecs[0]))

// ExpanderParamSpecs defines UI metadata for the expander effect parameters.
static const EffectParamSpec kExpanderParamSpecs[] = {
    {
        .id = "threshold_db",
        .display_name = "Threshold",
        .group = "Expander",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -80.0f,
        .max_value = 0.0f,
        .default_value = -45.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "ratio",
        .display_name = "Ratio",
        .group = "Expander",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_RATIO,
        .min_value = 1.0f,
        .max_value = 8.0f,
        .default_value = 2.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "hysteresis_db",
        .display_name = "Hysteresis",
        .group = "Expander",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = 0.0f,
        .max_value = 12.0f,
        .default_value = 3.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "attack_ms",
        .display_name = "Attack",
        .group = "Expander",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 2.0f,
        .max_value = 50.0f,
        .default_value = 5.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "release_ms",
        .display_name = "Release",
        .group = "Expander",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 20.0f,
        .max_value = 500.0f,
        .default_value = 100.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    }
};

#define EXPANDER_PARAM_SPEC_COUNT (sizeof(kExpanderParamSpecs) / sizeof(kExpanderParamSpecs[0]))

// TransientShaperParamSpecs defines UI metadata for the transient shaper effect parameters.
static const EffectParamSpec kTransientShaperParamSpecs[] = {
    {
        .id = "attack_amt",
        .display_name = "Attack",
        .group = "Transient",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = -1.0f,
        .max_value = 1.0f,
        .default_value = 0.5f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "sustain_amt",
        .display_name = "Sustain",
        .group = "Transient",
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
        .id = "fast_ms",
        .display_name = "Fast",
        .group = "Transient",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 1.0f,
        .max_value = 20.0f,
        .default_value = 5.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "slow_ms",
        .display_name = "Slow",
        .group = "Transient",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 20.0f,
        .max_value = 400.0f,
        .default_value = 150.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Transient",
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

#define TRANSIENT_SHAPER_PARAM_SPEC_COUNT (sizeof(kTransientShaperParamSpecs) / sizeof(kTransientShaperParamSpecs[0]))

// CompressorParamSpecs defines UI metadata for the Compressor effect parameters.
static const EffectParamSpec kCompressorParamSpecs[] = {
    {
        .id = "threshold_db",
        .display_name = "Threshold",
        .group = "Compressor",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -60.0f,
        .max_value = 0.0f,
        .default_value = -18.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "ratio",
        .display_name = "Ratio",
        .group = "Compressor",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_RATIO,
        .min_value = 1.0f,
        .max_value = 20.0f,
        .default_value = 4.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "attack_ms",
        .display_name = "Attack",
        .group = "Compressor",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.1f,
        .max_value = 100.0f,
        .default_value = 5.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "release_ms",
        .display_name = "Release",
        .group = "Compressor",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 5.0f,
        .max_value = 500.0f,
        .default_value = 80.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "makeup_db",
        .display_name = "Makeup",
        .group = "Compressor",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = -24.0f,
        .max_value = 24.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "knee_db",
        .display_name = "Knee",
        .group = "Compressor",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_DB,
        .min_value = 0.0f,
        .max_value = 24.0f,
        .default_value = 6.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "detector",
        .display_name = "Detector",
        .group = "Compressor",
        .type = FX_PARAM_TYPE_ENUM,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_DROPDOWN,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .enum_count = 2,
        .enum_labels = {"Peak", "RMS"}
    }
};

#define COMPRESSOR_PARAM_SPEC_COUNT (sizeof(kCompressorParamSpecs) / sizeof(kCompressorParamSpecs[0]))
