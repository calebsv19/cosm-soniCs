// fx_reverb.c — simple Schroeder/Moorer reverb (interleaved, in-place)
// Params: size, decay_rt60, damping, predelay_ms, mix
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Design choices ---------------------------------------------------------
// - 4 combs in parallel -> 2 allpasses in series (per channel).
// - L/R stereo width via slight delay offsets.
// - Damping via one-pole lowpass in comb feedback path.
// - All processing is in-place and RT-safe (no allocs in process()).

// Tunings inspired by Freeverb but trimmed/simplified:
#define NUM_COMBS     4
#define NUM_ALLPASS   2

// Base delay times (in samples @48k) — scaled by SR and size.
static const int COMB_BASE_48K[NUM_COMBS]     = { 1557, 1617, 1491, 1422 };
static const int ALLPASS_BASE_48K[NUM_ALLPASS]= { 225,  556 };

// Small offset for R channel (in samples @48k) to widen stereo
static const int STEREO_SPREAD_48K = 23;

// Parameter ranges/defaults
static const float REVERB_SIZE_MIN = 0.5f;
static const float REVERB_SIZE_MAX = 1.5f;
static const float REVERB_SIZE_DEF = 1.0f;
static const float REVERB_DECAY_MIN = 0.1f;
static const float REVERB_DECAY_MAX = 10.0f;
static const float REVERB_DECAY_DEF = 2.0f;
static const float REVERB_DAMP_MIN = 0.0f;
static const float REVERB_DAMP_MAX = 1.0f;
static const float REVERB_DAMP_DEF = 0.3f;
static const float REVERB_PRE_MIN = 0.0f;
static const float REVERB_PRE_MAX = 100.0f;
static const float REVERB_PRE_DEF = 20.0f; // ms
static const float REVERB_MIX_MIN = 0.0f;
static const float REVERB_MIX_MAX = 1.0f;
static const float REVERB_MIX_DEF = 0.2f;

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
// --- Delay line helpers -----------------------------------------------------

typedef struct DelayLine {
    float* buf;          // contiguous [len] per channel (we keep separate lines per stage)
    int    len;
    int    idx;          // write index
} DelayLine;

static inline float dl_read(const DelayLine* dl, int tap) {
    int r = dl->idx - tap;
    while (r < 0) r += dl->len;
    return dl->buf[r];
}
static inline void dl_write(DelayLine* dl, float x) {
    dl->buf[dl->idx] = x;
    dl->idx++;
    if (dl->idx >= dl->len) dl->idx = 0;
}
static inline void dl_clear(DelayLine* dl) {
    if (!dl || !dl->buf) return;
    memset(dl->buf, 0, (size_t)dl->len * sizeof(float));
    dl->idx = 0;
}

// --- Reverb state -----------------------------------------------------------

typedef struct Comb {
    DelayLine dl;                // delay line
    float     feedback_g;        // derived from RT60 and delay time
    float     damp;              // 0..1
    float     lp;                // one-pole lowpass state (feedback path)
} Comb;

typedef struct Allpass {
    DelayLine dl;
    float     g;                 // ~0.5–0.7
} Allpass;

typedef struct FxReverb {
    // Config
    uint32_t sr;
    uint32_t max_channels;

    // Params (public)
    float size;          // scales delays
    float decay_rt60;    // seconds
    float damping;       // 0..1
    float predelay_ms;   // ms
    float mix;           // 0..1

    // Derived
    int   predelay_samp; // samples
    int   comb_taps_L[NUM_COMBS];
    int   comb_taps_R[NUM_COMBS];
    int   allpass_taps_L[NUM_ALLPASS];
    int   allpass_taps_R[NUM_ALLPASS];

    // Per-channel arrays of stages
    Comb    combs_L[NUM_COMBS];
    Comb    combs_R[NUM_COMBS];
    Allpass ap_L[NUM_ALLPASS];
    Allpass ap_R[NUM_ALLPASS];

    // Predelay per channel
    DelayLine predelay_L;
    DelayLine predelay_R;

} FxReverb;

// --- Coefficient derivation -------------------------------------------------

// Given delay time t (seconds) and RT60, feedback gain g satisfies g^(t*fs) = 10^-3
// Classic approximation: g = 10^(-3 * delay_seconds / RT60)
static inline float rt60_to_feedback(float delay_seconds, float rt60_sec) {
    if (rt60_sec <= 1e-5f) rt60_sec = 1e-5f;
    float g = powf(10.0f, (-3.0f * delay_seconds) / rt60_sec);
    // Keep inside safe bounds to avoid blow-ups
    if (g < 0.0f) g = 0.0f;
    if (g > 0.9999f) g = 0.9999f;
    return g;
}

static int scale_delay(int base_48k, float sr, float size, int stereo_spread_48k, int is_right) {
    float scale = sr / 48000.0f;
    int base = (int)( (float)base_48k * scale * size + 0.5f );
    int spread = (int)( (float)stereo_spread_48k * scale + 0.5f );
    if (is_right) base += spread;
    if (base < 1) base = 1;
    return base;
}

static void reverb_recompute(FxReverb* rv) {
    const float sr = (float)rv->sr;
    const float size = clampf(rv->size, REVERB_SIZE_MIN, REVERB_SIZE_MAX);
    const float damp = clampf(rv->damping, REVERB_DAMP_MIN, REVERB_DAMP_MAX);
    float rt60 = clampf(rv->decay_rt60, REVERB_DECAY_MIN, REVERB_DECAY_MAX);

    // Predelay
    int pre = (int)( (rv->predelay_ms * 0.001f) * sr + 0.5f );
    if (pre < 0) pre = 0;
    rv->predelay_samp = pre;

    // Comb taps and feedback gains per side
    for (int i = 0; i < NUM_COMBS; ++i) {
        int tapL = scale_delay(COMB_BASE_48K[i], sr, size, STEREO_SPREAD_48K, 0);
        int tapR = scale_delay(COMB_BASE_48K[i], sr, size, STEREO_SPREAD_48K, 1);
        rv->comb_taps_L[i] = tapL;
        rv->comb_taps_R[i] = tapR;

        rv->combs_L[i].feedback_g = rt60_to_feedback((float)tapL / sr, rt60);
        rv->combs_R[i].feedback_g = rt60_to_feedback((float)tapR / sr, rt60);
        rv->combs_L[i].damp = damp;
        rv->combs_R[i].damp = damp;
    }

    // Allpass taps (fixed gains ~0.5–0.7 for diffusion)
    for (int i = 0; i < NUM_ALLPASS; ++i) {
        int tapL = scale_delay(ALLPASS_BASE_48K[i], sr, size, STEREO_SPREAD_48K, 0);
        int tapR = scale_delay(ALLPASS_BASE_48K[i], sr, size, STEREO_SPREAD_48K, 1);
        rv->allpass_taps_L[i] = tapL;
        rv->allpass_taps_R[i] = tapR;
        rv->ap_L[i].g = 0.5f + 0.1f * (float)i; // 0.5, 0.6
        rv->ap_R[i].g = 0.5f + 0.1f * (float)i; // 0.5, 0.6
    }
}

// --- Allocation helpers -----------------------------------------------------

static int alloc_dl(DelayLine* dl, int len) {
    dl->buf = (float*)calloc((size_t)len, sizeof(float));
    if (!dl->buf) return 0;
    dl->len = len;
    dl->idx = 0;
    return 1;
}

static void free_dl(DelayLine* dl) {
    free(dl->buf);
    dl->buf = NULL;
    dl->len = 0;
    dl->idx = 0;
}

// Ensure delay line length fits the requested tap length; if not, reallocate.
// Non-RT only (called in create or set_param via recompute path if ever extended).
static int ensure_dl_len(DelayLine* dl, int len) {
    if (dl->len >= len) return 1;
    float* nb = (float*)realloc(dl->buf, (size_t)len * sizeof(float));
    if (!nb) return 0;
    // zero the new region
    if (len > dl->len) {
        memset(nb + dl->len, 0, (size_t)(len - dl->len) * sizeof(float));
    }
    dl->buf = nb;
    dl->len = len;
    if (dl->idx >= dl->len) dl->idx = 0;
    return 1;
}

// --- Processing -------------------------------------------------------------

// Comb filter with damping in feedback path (per channel)
static inline float comb_tick(Comb* c, float x) {
    // read delayed
    float y = dl_read(&c->dl, c->dl.len - 1); // fastest: one-sample write/read drift
    // one-pole lowpass on feedback (classic Freeverb damping)
    c->lp = (1.0f - c->damp) * y + c->damp * c->lp;
    float fb = c->feedback_g * c->lp;
    // write
    dl_write(&c->dl, x + fb);
    return y;
}

// Allpass: y[n] = -g*x[n] + in_delayed + g*y[n-1] (via delay line trick)
// Here we use canonical implementation with delay buffer.
static inline float allpass_tick(Allpass* ap, float x) {
    float bufout = dl_read(&ap->dl, ap->dl.len - 1);
    float y = -ap->g * x + bufout;
    float w = x + ap->g * y;
    dl_write(&ap->dl, w);
    return y;
}

static void reverb_process(FxHandle* h, const float* in, float* out, int frames, int channels) {
    (void)in; // in-place
    FxReverb* rv = (FxReverb*)h;
    if (channels > (int)rv->max_channels) channels = (int)rv->max_channels;

    const float mix = clampf(rv->mix, REVERB_MIX_MIN, REVERB_MIX_MAX);
    const float dry = 1.0f - mix;

    // Pre-delay tap positions (advance by one write per sample)
    for (int n = 0; n < frames; ++n) {
        int base = n * channels;

        // Left and Right channel paths (if mono, R path won't run)
        for (int ch = 0; ch < channels; ++ch) {
            float x = out[base + ch];

            // Predelay
            DelayLine* pd = (ch == 0) ? &rv->predelay_L : &rv->predelay_R;
            float pd_out = rv->predelay_samp > 0 ? dl_read(pd, rv->predelay_samp) : x;
            dl_write(pd, x);

            // Parallel comb sum
            float csum = 0.0f;
            Comb* combs = (ch == 0) ? rv->combs_L : rv->combs_R;
            for (int i = 0; i < NUM_COMBS; ++i) {
                csum += comb_tick(&combs[i], pd_out);
            }
            csum *= (1.0f / (float)NUM_COMBS);

            // Series allpasses
            Allpass* aps = (ch == 0) ? rv->ap_L : rv->ap_R;
            float y = csum;
            for (int i = 0; i < NUM_ALLPASS; ++i) {
                y = allpass_tick(&aps[i], y);
            }

            // Dry/wet mix
            out[base + ch] = dry * x + mix * y;
        }
    }
}

// --- Param interface --------------------------------------------------------

static void reverb_set_param(FxHandle* h, uint32_t idx, float value) {
    FxReverb* rv = (FxReverb*)h;
    switch (idx) {
        case 0: rv->size = clampf(value, REVERB_SIZE_MIN, REVERB_SIZE_MAX); break;
        case 1: rv->decay_rt60 = clampf(value, REVERB_DECAY_MIN, REVERB_DECAY_MAX); break;
        case 2: rv->damping = clampf(value, REVERB_DAMP_MIN, REVERB_DAMP_MAX); break;
        case 3: rv->predelay_ms = clampf(value, REVERB_PRE_MIN, REVERB_PRE_MAX); break;
        case 4: rv->mix = clampf(value, REVERB_MIX_MIN, REVERB_MIX_MAX); break;
        default: break;
    }
    // Recompute derived taps/coeffs (non-RT OK)
    reverb_recompute(rv);

    // Ensure delay buffers are large enough for new taps
    int need_pre = rv->predelay_samp;
    if (!ensure_dl_len(&rv->predelay_L, need_pre + 1)) return;
    if (!ensure_dl_len(&rv->predelay_R, need_pre + 1)) return;

    for (int i = 0; i < NUM_COMBS; ++i) {
        ensure_dl_len(&rv->combs_L[i].dl, rv->comb_taps_L[i]);
        ensure_dl_len(&rv->combs_R[i].dl, rv->comb_taps_R[i]);
    }
    for (int i = 0; i < NUM_ALLPASS; ++i) {
        ensure_dl_len(&rv->ap_L[i].dl, rv->allpass_taps_L[i]);
        ensure_dl_len(&rv->ap_R[i].dl, rv->allpass_taps_R[i]);
    }
}

static void reverb_reset(FxHandle* h) {
    FxReverb* rv = (FxReverb*)h;

    dl_clear(&rv->predelay_L);
    dl_clear(&rv->predelay_R);

    for (int i = 0; i < NUM_COMBS; ++i) {
        dl_clear(&rv->combs_L[i].dl);
        dl_clear(&rv->combs_R[i].dl);
        rv->combs_L[i].lp = 0.0f;
        rv->combs_R[i].lp = 0.0f;
    }
    for (int i = 0; i < NUM_ALLPASS; ++i) {
        dl_clear(&rv->ap_L[i].dl);
        dl_clear(&rv->ap_R[i].dl);
    }
}

static void reverb_destroy(FxHandle* h) {
    FxReverb* rv = (FxReverb*)h;

    free_dl(&rv->predelay_L);
    free_dl(&rv->predelay_R);

    for (int i = 0; i < NUM_COMBS; ++i) {
        free_dl(&rv->combs_L[i].dl);
        free_dl(&rv->combs_R[i].dl);
    }
    for (int i = 0; i < NUM_ALLPASS; ++i) {
        free_dl(&rv->ap_L[i].dl);
        free_dl(&rv->ap_R[i].dl);
    }
    free(rv);
}

// --- Descriptor / Factory ---------------------------------------------------

int reverb_get_desc(FxDesc *out) {
    if (!out) return 0;
    out->name = "Reverb";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 5;
    out->param_names[0] = "size";
    out->param_names[1] = "decay_rt60";
    out->param_names[2] = "damping";
    out->param_names[3] = "predelay_ms";
    out->param_names[4] = "mix";
    out->param_defaults[0] = REVERB_SIZE_DEF;
    out->param_defaults[1] = REVERB_DECAY_DEF;
    out->param_defaults[2] = REVERB_DAMP_DEF;
    out->param_defaults[3] = REVERB_PRE_DEF;
    out->param_defaults[4] = REVERB_MIX_DEF;
    out->latency_samples = 0; // creative predelay is included in effect sound, not throughput latency
    return 1;
}

int reverb_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                  uint32_t sample_rate, uint32_t max_block, uint32_t max_channels) {
    (void)desc; (void)max_block;

    FxReverb* rv = (FxReverb*)calloc(1, sizeof(FxReverb));
    if (!rv) return 0;

    rv->sr = sample_rate;
    rv->max_channels = (max_channels > 0) ? max_channels : 2;

    // Defaults
    rv->size = REVERB_SIZE_DEF;
    rv->decay_rt60 = REVERB_DECAY_DEF;
    rv->damping = REVERB_DAMP_DEF;
    rv->predelay_ms = REVERB_PRE_DEF;
    rv->mix = REVERB_MIX_DEF;

    // Compute taps/coeffs
    reverb_recompute(rv);

    // Allocate lines with initial lengths
    int pre_len = rv->predelay_samp + 1;
    if (pre_len < 1) pre_len = 1;
    if (!alloc_dl(&rv->predelay_L, pre_len) || !alloc_dl(&rv->predelay_R, pre_len)) {
        reverb_destroy((FxHandle*)rv);
        return 0;
    }

    for (int i = 0; i < NUM_COMBS; ++i) {
        if (!alloc_dl(&rv->combs_L[i].dl, rv->comb_taps_L[i]) ||
            !alloc_dl(&rv->combs_R[i].dl, rv->comb_taps_R[i])) {
            reverb_destroy((FxHandle*)rv);
            return 0;
        }
        rv->combs_L[i].lp = 0.0f;
        rv->combs_R[i].lp = 0.0f;
    }
    for (int i = 0; i < NUM_ALLPASS; ++i) {
        if (!alloc_dl(&rv->ap_L[i].dl, rv->allpass_taps_L[i]) ||
            !alloc_dl(&rv->ap_R[i].dl, rv->allpass_taps_R[i])) {
            reverb_destroy((FxHandle*)rv);
            return 0;
        }
    }

    reverb_reset((FxHandle*)rv);

    out_vt->process = reverb_process;
    out_vt->set_param = reverb_set_param;
    out_vt->reset     = reverb_reset;
    out_vt->destroy   = reverb_destroy;

    *out_handle = (FxHandle*)rv;
    return 1;
}
