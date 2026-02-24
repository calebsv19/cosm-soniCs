// effects_builtin.c - single source of truth for built-in FX registry
#include "effects/effects_api.h"
#include "effects/effects_manager.h"
#include "effects/effects_builtin.h"
#include "effects/param_specs/basics_param_specs.h"
#include "effects/param_specs/delay_param_specs.h"
#include "effects/param_specs/dynamics_param_specs.h"
#include "effects/param_specs/distortion_param_specs.h"
#include "effects/param_specs/eq_param_specs.h"
#include "effects/param_specs/filter_tone_param_specs.h"
#include "effects/param_specs/metering_param_specs.h"
#include "effects/param_specs/modulation_param_specs.h"
#include "effects/param_specs/reverb_param_specs.h"

// Forward declarations for every compiled effect

// Basics
int gain_get_desc(FxDesc *out);
int gain_create(const FxDesc*, FxHandle **out_handle, FxVTable *out_vt,
                uint32_t sample_rate, uint32_t max_block, uint32_t max_channels);
int dcblock_get_desc(FxDesc*);
int dcblock_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);
int pan_get_desc(FxDesc*);
int pan_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);
int mute_get_desc(FxDesc*);
int mute_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);
int monomaker_get_desc(FxDesc*);
int monomaker_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);
int stereo_blend_get_desc(FxDesc*);
int stereo_blend_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);
int auto_trim_get_desc(FxDesc*);
int auto_trim_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);

// Dynamics
int compressor_get_desc(FxDesc *out);
int compressor_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int softclip_get_desc(FxDesc *out);
int softclip_create(const FxDesc*, FxHandle **out_handle, FxVTable *out_vt,
                    uint32_t sample_rate, uint32_t max_block, uint32_t max_channels);
int limiter_get_desc(FxDesc *out);
int limiter_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int gate_get_desc(FxDesc *out);
int gate_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int deesser_get_desc(FxDesc*);
int deesser_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);
int sccomp_get_desc(FxDesc*);
int sccomp_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);
int upward_comp_get_desc(FxDesc *out);
int upward_comp_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int expander_get_desc(FxDesc *out);
int expander_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int transient_shaper_get_desc(FxDesc *out);
int transient_shaper_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);

// EQ
int biquad_get_desc(FxDesc *out);
int biquad_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int eq_fixed3_get_desc(FxDesc*);
int eq_fixed3_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int eq_notch_get_desc(FxDesc *out);
int eq_notch_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int eq_tilt_get_desc(FxDesc *out);
int eq_tilt_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);

// Filter & Tone
int svf_get_desc(FxDesc *out);
int svf_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int autowah_get_desc(FxDesc *out);
int autowah_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int width_get_desc(FxDesc *out);
int width_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int tilt_get_desc(FxDesc *out);
int tilt_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int phaser_get_desc(FxDesc *out);
int phaser_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int formant_get_desc(FxDesc *out);
int formant_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int comb_get_desc(FxDesc *out);
int comb_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);

// Delay
int delay_get_desc(FxDesc *out);
int delay_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int pingpong_get_desc(FxDesc *out);
int pingpong_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int multitap_get_desc(FxDesc *out);
int multitap_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int tape_echo_get_desc(FxDesc *out);
int tape_echo_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int diffusion_delay_get_desc(FxDesc *out);
int diffusion_delay_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);

// Distortion / Lo-Fi
int hardclip_get_desc(FxDesc *out);
int hardclip_create(const FxDesc*, FxHandle **, FxVTable*, uint32_t, uint32_t, uint32_t);
int softsat_get_desc(FxDesc *out);
int softsat_create(const FxDesc*, FxHandle **, FxVTable*, uint32_t, uint32_t, uint32_t);
int bitcrusher_get_desc(FxDesc*);
int bitcrusher_create(const FxDesc*, FxHandle **, FxVTable*, uint32_t, uint32_t, uint32_t);
int overdrive_get_desc(FxDesc *);
int overdrive_create(const FxDesc*, FxHandle **, FxVTable*, uint32_t, uint32_t, uint32_t);
int waveshaper_get_desc(FxDesc *out);
int waveshaper_create(const FxDesc*, FxHandle **, FxVTable*, uint32_t, uint32_t, uint32_t);
int decimator_get_desc(FxDesc *out);
int decimator_create(const FxDesc*, FxHandle **, FxVTable*, uint32_t, uint32_t, uint32_t);

// Modulation
int trempan_get_desc(FxDesc *out);
int trempan_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int chorus_get_desc(FxDesc *out);
int chorus_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int flanger_get_desc(FxDesc *out);
int flanger_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int vibrato_get_desc(FxDesc *out);
int vibrato_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int ringmod_get_desc(FxDesc *out);
int ringmod_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int autopan_get_desc(FxDesc *out);
int autopan_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int barberpole_phaser_get_desc(FxDesc *out);
int barberpole_phaser_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);

// Reverb / Spatial
int reverb_get_desc(FxDesc *out);
int reverb_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int early_reflections_get_desc(FxDesc *out);
int early_reflections_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int plate_lite_get_desc(FxDesc *out);
int plate_lite_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int gated_reverb_get_desc(FxDesc *out);
int gated_reverb_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);

// Metering
int correlation_meter_get_desc(FxDesc *out);
int correlation_meter_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int mid_side_meter_get_desc(FxDesc *out);
int mid_side_meter_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int vectorscope_meter_get_desc(FxDesc *out);
int vectorscope_meter_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int peak_rms_meter_get_desc(FxDesc *out);
int peak_rms_meter_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int lufs_meter_get_desc(FxDesc *out);
int lufs_meter_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);
int spectrogram_meter_get_desc(FxDesc *out);
int spectrogram_meter_create(const FxDesc*, FxHandle **, FxVTable *, uint32_t, uint32_t, uint32_t);

#define FX_ENTRY(id, name, get_desc, create) {id, name, get_desc, create, NULL, 0}
#define FX_ENTRY_SPEC(id, name, get_desc, create, specs, count) {id, name, get_desc, create, specs, count}

static FxRegistryEntry kBuiltinFxTable[] = {
    // Basics / Utility (01–19)
    FX_ENTRY_SPEC( 1u, "Gain",             gain_get_desc,            gain_create, kGainParamSpecs, GAIN_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC( 2u, "DCBlock",          dcblock_get_desc,         dcblock_create, kDcBlockParamSpecs, DCBLOCK_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC( 3u, "Pan",              pan_get_desc,             pan_create, kPanParamSpecs, PAN_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC( 4u, "Mute",             mute_get_desc,            mute_create, kMuteParamSpecs, MUTE_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC( 5u, "MonoMakerLow",     monomaker_get_desc,       monomaker_create, kMonoMakerParamSpecs, MONOMAKER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC( 6u, "StereoBlend",      stereo_blend_get_desc,    stereo_blend_create, kStereoBlendParamSpecs, STEREOBLEND_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC( 7u, "AutoTrim",         auto_trim_get_desc,       auto_trim_create, kAutoTrimParamSpecs, AUTOTRIM_PARAM_SPEC_COUNT),

    // Dynamics (20–29)
    FX_ENTRY_SPEC(20u, "Compressor",  compressor_get_desc,      compressor_create, kCompressorParamSpecs, COMPRESSOR_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(21u, "Limiter",     limiter_get_desc,         limiter_create, kLimiterParamSpecs, LIMITER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(22u, "Gate",        gate_get_desc,            gate_create, kGateParamSpecs, GATE_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(23u, "DeEsser",     deesser_get_desc,         deesser_create, kDeEsserParamSpecs, DEESSER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(24u, "SCCompressor",    sccomp_get_desc,          sccomp_create, kSidechainCompressorParamSpecs, SIDECHAIN_COMP_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(25u, "UpwardComp",      upward_comp_get_desc,     upward_comp_create, kUpwardCompParamSpecs, UPWARD_COMP_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(26u, "Expander",        expander_get_desc,        expander_create, kExpanderParamSpecs, EXPANDER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(27u, "TransientShaper", transient_shaper_get_desc, transient_shaper_create, kTransientShaperParamSpecs, TRANSIENT_SHAPER_PARAM_SPEC_COUNT),

    // EQ (30–39)
    FX_ENTRY_SPEC(30u, "BiquadEQ",         biquad_get_desc,          biquad_create, kBiquadEqParamSpecs, BIQUAD_EQ_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(31u, "EQ_Fixed3",        eq_fixed3_get_desc,       eq_fixed3_create, kEqFixed3ParamSpecs, EQ_FIXED3_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(32u, "EQ_Notch",         eq_notch_get_desc,        eq_notch_create, kEqNotchParamSpecs, EQ_NOTCH_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(33u, "EQ_Tilt",          eq_tilt_get_desc,         eq_tilt_create, kEqTiltParamSpecs, EQ_TILT_PARAM_SPEC_COUNT),

    // Filter & Tone (40–49)
    FX_ENTRY_SPEC(40u, "SVF",              svf_get_desc,             svf_create, kSvfParamSpecs, SVF_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(41u, "AutoWah",          autowah_get_desc,         autowah_create, kAutoWahParamSpecs, AUTOWAH_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(42u, "StereoWidth",      width_get_desc,           width_create, kStereoWidthParamSpecs, STEREO_WIDTH_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(43u, "TiltEQ",           tilt_get_desc,            tilt_create, kTiltEqParamSpecs, TILT_EQ_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(44u, "Phaser",           phaser_get_desc,          phaser_create, kPhaserParamSpecs, PHASER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(45u, "FormantFilter",    formant_get_desc,         formant_create, kFormantFilterParamSpecs, FORMANT_FILTER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(46u, "CombFF",           comb_get_desc,            comb_create, kCombFFParamSpecs, COMB_FF_PARAM_SPEC_COUNT),

    // Delay (50–59)
    FX_ENTRY_SPEC(50u, "Delay",            delay_get_desc,           delay_create, kDelayParamSpecs, DELAY_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(51u, "PingPongDelay",    pingpong_get_desc,        pingpong_create, kPingPongParamSpecs, PINGPONG_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(52u, "MultiTapDelay",    multitap_get_desc,        multitap_create, kMultiTapParamSpecs, MULTITAP_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(53u, "TapeEcho",         tape_echo_get_desc,       tape_echo_create, kTapeEchoParamSpecs, TAPE_ECHO_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(54u, "DiffusionDelay",   diffusion_delay_get_desc, diffusion_delay_create, kDiffusionDelayParamSpecs, DIFFUSION_DELAY_PARAM_SPEC_COUNT),

    // Distortion / Lo-Fi (60–69)
    FX_ENTRY_SPEC(60u, "HardClip",        hardclip_get_desc,        hardclip_create, kHardClipParamSpecs, HARDCLIP_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(61u, "SoftSaturation",  softsat_get_desc,         softsat_create, kSoftSatParamSpecs, SOFTSAT_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(62u, "BitCrusher",      bitcrusher_get_desc,      bitcrusher_create, kBitCrusherParamSpecs, BITCRUSHER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(63u, "Overdrive",       overdrive_get_desc,       overdrive_create, kOverdriveParamSpecs, OVERDRIVE_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(64u, "Waveshaper",      waveshaper_get_desc,      waveshaper_create, kWaveshaperParamSpecs, WAVESHAPER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(65u, "Decimator",       decimator_get_desc,       decimator_create, kDecimatorParamSpecs, DECIMATOR_PARAM_SPEC_COUNT),

    // Modulation (70–79)
    FX_ENTRY_SPEC(70u, "TremoloPan",       trempan_get_desc,         trempan_create, kTremoloPanParamSpecs, TREMPAN_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(71u, "Chorus",           chorus_get_desc,          chorus_create, kChorusParamSpecs, CHORUS_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(72u, "Flanger",          flanger_get_desc,         flanger_create, kFlangerParamSpecs, FLANGER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(73u, "Vibrato",          vibrato_get_desc,         vibrato_create, kVibratoParamSpecs, VIBRATO_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(74u, "RingMod",          ringmod_get_desc,         ringmod_create, kRingModParamSpecs, RINGMOD_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(75u, "AutoPan",          autopan_get_desc,         autopan_create, kAutoPanParamSpecs, AUTOPAN_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(76u, "BarberpolePhaser", barberpole_phaser_get_desc, barberpole_phaser_create, kBarberpolePhaserParamSpecs, BARBERPOLE_PHASER_PARAM_SPEC_COUNT),

    // Reverb / Spatial (90–99)
    FX_ENTRY_SPEC(90u, "Reverb",           reverb_get_desc,          reverb_create, kReverbParamSpecs, REVERB_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(91u, "EarlyReflections", early_reflections_get_desc, early_reflections_create,
                  kEarlyReflectionsParamSpecs, EARLY_REFLECTIONS_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(92u, "PlateLite",        plate_lite_get_desc,      plate_lite_create,
                  kPlateLiteParamSpecs, PLATE_LITE_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(93u, "GatedReverb",      gated_reverb_get_desc,    gated_reverb_create,
                  kGatedReverbParamSpecs, GATED_REVERB_PARAM_SPEC_COUNT),

    // Metering (100–109)
    FX_ENTRY_SPEC(100u, "CorrelationMeter", correlation_meter_get_desc, correlation_meter_create,
                  kCorrelationMeterParamSpecs, CORRELATION_METER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(101u, "MidSideMeter",     mid_side_meter_get_desc,     mid_side_meter_create,
                  kMidSideMeterParamSpecs, MID_SIDE_METER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(102u, "VectorScope",      vectorscope_meter_get_desc,  vectorscope_meter_create,
                  kVectorScopeParamSpecs, VECTOR_SCOPE_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(103u, "PeakRmsMeter",     peak_rms_meter_get_desc,     peak_rms_meter_create,
                  kPeakRmsMeterParamSpecs, PEAK_RMS_METER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(104u, "LufsMeter",        lufs_meter_get_desc,         lufs_meter_create,
                  kLufsMeterParamSpecs, LUFS_METER_PARAM_SPEC_COUNT),
    FX_ENTRY_SPEC(105u, "SpectrogramMeter", spectrogram_meter_get_desc,  spectrogram_meter_create,
                  kSpectrogramMeterParamSpecs, SPECTROGRAM_METER_PARAM_SPEC_COUNT),
};

#undef FX_ENTRY
#undef FX_ENTRY_SPEC

static const int kBuiltinFxCount = (int)(sizeof(kBuiltinFxTable) / sizeof(kBuiltinFxTable[0]));

void fx_register_builtins_all(struct EffectsManager* fm) {
    if (!fm) return;
    fxm_register_builtin(fm, kBuiltinFxTable, kBuiltinFxCount);
}

const FxRegistryEntry* fx_get_builtin_registry(int* out_count) {
    if (out_count) {
        *out_count = kBuiltinFxCount;
    }
    return kBuiltinFxTable;
}
