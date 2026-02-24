#pragma once

#include "effects/param_spec.h"

// TremoloPanParamSpecs defines UI metadata for the tremolo/auto-pan effect parameters.
static const EffectParamSpec kTremoloPanParamSpecs[] = {
    {
        .id = "rate_hz",
        .display_name = "Rate",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 0.1f,
        .max_value = 20.0f,
        .default_value = 3.0f,
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
        .default_value = 0.7f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "shape",
        .display_name = "Shape",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_PERCENT,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "autopan",
        .display_name = "Auto Pan",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_BOOL,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 1.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_TOGGLE,
        .flags = FX_PARAM_FLAG_AUTOMATABLE
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

#define TREMPAN_PARAM_SPEC_COUNT (sizeof(kTremoloPanParamSpecs) / sizeof(kTremoloPanParamSpecs[0]))

// ChorusParamSpecs defines UI metadata for the chorus effect parameters.
static const EffectParamSpec kChorusParamSpecs[] = {
    {
        .id = "rate_hz",
        .display_name = "Rate",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 0.05f,
        .max_value = 5.0f,
        .default_value = 0.8f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "depth_ms",
        .display_name = "Depth",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.0f,
        .max_value = 15.0f,
        .default_value = 6.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "base_ms",
        .display_name = "Base",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 5.0f,
        .max_value = 25.0f,
        .default_value = 15.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Modulation",
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

#define CHORUS_PARAM_SPEC_COUNT (sizeof(kChorusParamSpecs) / sizeof(kChorusParamSpecs[0]))

// FlangerParamSpecs defines UI metadata for the flanger effect parameters.
static const EffectParamSpec kFlangerParamSpecs[] = {
    {
        .id = "rate_hz",
        .display_name = "Rate",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 0.05f,
        .max_value = 5.0f,
        .default_value = 0.25f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "depth_ms",
        .display_name = "Depth",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.1f,
        .max_value = 5.0f,
        .default_value = 2.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "base_ms",
        .display_name = "Base",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.1f,
        .max_value = 5.0f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "feedback",
        .display_name = "Feedback",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = -0.95f,
        .max_value = 0.95f,
        .default_value = 0.25f,
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
        .default_value = 0.5f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define FLANGER_PARAM_SPEC_COUNT (sizeof(kFlangerParamSpecs) / sizeof(kFlangerParamSpecs[0]))

// VibratoParamSpecs defines UI metadata for the vibrato effect parameters.
static const EffectParamSpec kVibratoParamSpecs[] = {
    {
        .id = "rate_hz",
        .display_name = "Rate",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 0.1f,
        .max_value = 10.0f,
        .default_value = 5.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "depth_ms",
        .display_name = "Depth",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 0.1f,
        .max_value = 8.0f,
        .default_value = 2.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "base_ms",
        .display_name = "Base",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_MS,
        .min_value = 2.0f,
        .max_value = 12.0f,
        .default_value = 6.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
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

#define VIBRATO_PARAM_SPEC_COUNT (sizeof(kVibratoParamSpecs) / sizeof(kVibratoParamSpecs[0]))

// RingModParamSpecs defines UI metadata for the ring mod effect parameters.
static const EffectParamSpec kRingModParamSpecs[] = {
    {
        .id = "freq_hz",
        .display_name = "Freq",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 1.0f,
        .max_value = 5000.0f,
        .default_value = 30.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE | FX_PARAM_FLAG_TEMPO_SYNC_CAPABLE,
        .smoothing_ms = 10.0f
    },
    {
        .id = "mix",
        .display_name = "Mix",
        .group = "Modulation",
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

#define RINGMOD_PARAM_SPEC_COUNT (sizeof(kRingModParamSpecs) / sizeof(kRingModParamSpecs[0]))

// AutoPanParamSpecs defines UI metadata for the auto-pan effect parameters.
static const EffectParamSpec kAutoPanParamSpecs[] = {
    {
        .id = "rate_hz",
        .display_name = "Rate",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 0.01f,
        .max_value = 10.0f,
        .default_value = 1.0f,
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
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "phase_deg",
        .display_name = "Phase",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = 0.0f,
        .max_value = 360.0f,
        .default_value = 0.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 10.0f
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

#define AUTOPAN_PARAM_SPEC_COUNT (sizeof(kAutoPanParamSpecs) / sizeof(kAutoPanParamSpecs[0]))

// BarberpolePhaserParamSpecs defines UI metadata for the barberpole phaser effect parameters.
static const EffectParamSpec kBarberpolePhaserParamSpecs[] = {
    {
        .id = "rate_hz",
        .display_name = "Rate",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 0.05f,
        .max_value = 3.0f,
        .default_value = 0.5f,
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
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_KNOB,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "min_hz",
        .display_name = "Min Hz",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 100.0f,
        .max_value = 600.0f,
        .default_value = 200.0f,
        .curve = FX_PARAM_CURVE_LOG,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    },
    {
        .id = "max_hz",
        .display_name = "Max Hz",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_HZ,
        .min_value = 800.0f,
        .max_value = 4000.0f,
        .default_value = 2000.0f,
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
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .step = 1.0f
    },
    {
        .id = "feedback",
        .display_name = "Feedback",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = -0.9f,
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
    },
    {
        .id = "direction",
        .display_name = "Direction",
        .group = "Modulation",
        .type = FX_PARAM_TYPE_FLOAT,
        .unit = FX_PARAM_UNIT_GENERIC,
        .min_value = -1.0f,
        .max_value = 1.0f,
        .default_value = 1.0f,
        .curve = FX_PARAM_CURVE_LINEAR,
        .ui_hint = FX_PARAM_UI_SLIDER,
        .flags = FX_PARAM_FLAG_AUTOMATABLE,
        .smoothing_ms = 20.0f
    }
};

#define BARBERPOLE_PHASER_PARAM_SPEC_COUNT (sizeof(kBarberpolePhaserParamSpecs) / sizeof(kBarberpolePhaserParamSpecs[0]))
