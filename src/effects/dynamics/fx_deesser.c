
// fx_deesser.c — De-Esser with optional sidechain key (interleaved, in-place)
// Detection: band-pass filtered sibilant region (default 6 kHz center, Q 2.0).
// Action: broadband gain reduction keyed by sibilant level, or band-only reduction.
// If sidechain is connected and process_sc() is used, detection uses sidechain.
// Params:
//   0: center_hz     (3000..12000)
//   1: Q             (0.5..6)
//   2: threshold_dB  (-60..0)   — detector level to trigger reduction
//   3: ratio         (1..10)    — 1=no reduction, higher = stronger
//   4: attack_ms     (0.1..20)
//   5: release_ms    (5..200)
//   6: makeup_dB     (-12..12)
//   7: band_only     (0..1)     — 0=broadband attenuation, 1=attenuate band component only
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }
static inline float dB_to_lin(float dB){ return powf(10.0f, dB * 0.05f); }
static inline float lin_to_dB(float lin){ return 20.0f * log10f(fmaxf(lin, 1e-12f)); }

typedef struct Biquad {
    float b0, b1, b2, a1, a2;
    // DF2T state per channel — we'll keep one set per channel per filter
    float *z1, *z2;
} Biquad;

typedef struct FxDeEsser {
    float sr;
    unsigned max_channels;

    // params
    float center_hz;
    float q;
    float thresh_dB;
    float ratio;
    float attack_ms;
    float release_ms;
    float makeup_dB;
    float band_only;

    // detector state
    float *env;    // per-channel envelope (RMS-ish simple follower)

    // bandpass filter for detection and for band-only subtraction
    Biquad bp;

} FxDeEsser;

// RBJ band-pass (constant skirt gain, peak gain = Q) design
static void biquad_bp_design(Biquad* bq, float sr, float f0, float Q)
{
    f0 = clampf(f0, 300.0f, sr * 0.45f);
    Q  = clampf(Q, 0.2f, 12.0f);
    float w0 = 2.0f * (float)M_PI * f0 / sr;
    float alpha = sinf(w0) / (2.0f * Q);
    float cosw0 = cosf(w0);

    float b0 =   alpha;
    float b1 =   0.0f;
    float b2 =  -alpha;
    float a0 =   1.0f + alpha;
    float a1 =  -2.0f * cosw0;
    float a2 =   1.0f - alpha;

    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

static inline float biquad_tick(Biquad* bq, int ch, float x)
{
    float v = x - bq->a1 * bq->z1[ch] - bq->a2 * bq->z2[ch];
    float y = bq->b0 * v + bq->b1 * bq->z1[ch] + bq->b2 * bq->z2[ch];
    bq->z2[ch] = bq->z1[ch];
    bq->z1[ch] = v;
    return y;
}

static void deess_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxDeEsser* d = (FxDeEsser*)h;
    switch (idx) {
        case 0: d->center_hz = clampf(value, 3000.f, 12000.f); break;
        case 1: d->q         = clampf(value, 0.5f, 6.0f); break;
        case 2: d->thresh_dB = clampf(value, -60.f, 0.f); break;
        case 3: d->ratio     = clampf(value, 1.f, 10.f); break;
        case 4: d->attack_ms = clampf(value, 0.1f, 20.f); break;
        case 5: d->release_ms= clampf(value, 5.f, 200.f); break;
        case 6: d->makeup_dB = clampf(value, -12.f, 12.f); break;
        case 7: d->band_only = clampf(value, 0.f, 1.f); break;
        default: break;
    }
    biquad_bp_design(&d->bp, d->sr, d->center_hz, d->q); // non-RT ok
}

static void deess_reset(FxHandle* h)
{
    FxDeEsser* d = (FxDeEsser*)h;
    for (unsigned ch = 0; ch < d->max_channels; ++ch) {
        d->env[ch] = 0.0f;
        d->bp.z1[ch] = 0.0f;
        d->bp.z2[ch] = 0.0f;
    }
}

static void deess_destroy(FxHandle* h)
{
    FxDeEsser* d = (FxDeEsser*)h;
    free(d->env);
    free(d->bp.z1);
    free(d->bp.z2);
    free(d);
}

// Gain computer (like compressor): returns linear gain (<=1)
static inline float gain_from_detector(float in_dB, float thr_dB, float ratio)
{
    if (in_dB <= thr_dB) return 1.0f;
    float target = thr_dB + (in_dB - thr_dB) / ratio;
    float g_db = target - in_dB; // negative
    return dB_to_lin(g_db);
}

static inline void deesser_core(FxDeEsser* d,
                                const float* key, int key_ch,
                                float* io, int frames, int channels)
{
    const float atk_a = expf(-1.0f / ((d->attack_ms  * 0.001f) * d->sr));
    const float rel_a = expf(-1.0f / ((d->release_ms * 0.001f) * d->sr));
    const float makeup = dB_to_lin(d->makeup_dB);
    const int   key_stride = key_ch; // interleaved

    for (int n = 0; n < frames; ++n) {
        // build a "mono" key sample by averaging key channels after bandpass
        float key_level = 0.0f;
        for (int kc = 0; kc < key_ch; ++kc) {
            float xk = key[n * key_stride + kc];
            // use channel 0's filter state for detector? No: use per-channel states
            // We'll re-use detector per output channel states (bp.z1/z2 indexed by ch),
            // but for a mono combined detector, use channel 0's states safely.
            // Simpler: process bandpass separately per output channel below.
            (void)xk;
        }

        // We compute detector per output channel and then take max for shared gain
        float shared_gain = 1.0f;
        for (int ch = 0; ch < channels; ++ch) {
            // pick key source sample for this "channel" if available; else use io sample
            float xk = 0.0f;
            if (key) {
                int kc = (ch < key_ch) ? ch : 0;
                xk = key[n * key_stride + kc];
            } else {
                xk = io[n * channels + ch];
            }
            float band = biquad_tick(&d->bp, ch % d->max_channels, xk);
            float level = fabsf(band);

            // peak follower with separate attack/release
            float prev = d->env[ch % d->max_channels];
            float coef = (level > prev) ? atk_a : rel_a;
            float env = coef * prev + (1.0f - coef) * level;
            d->env[ch % d->max_channels] = env;

            float in_dB = lin_to_dB(env + 1e-12f);
            float g = gain_from_detector(in_dB, d->thresh_dB, d->ratio);
            if (g < shared_gain) shared_gain = g;
        }

        // apply gain
        for (int ch = 0; ch < channels; ++ch) {
            float x = io[n * channels + ch];
            if (d->band_only >= 0.5f) {
                // subtract only sibilant band
                float band = biquad_tick(&d->bp, ch % d->max_channels, x);
                float atten = 1.0f - shared_gain; // amount to reduce
                float y = x - atten * band;
                io[n * channels + ch] = y * makeup;
            } else {
                io[n * channels + ch] = x * shared_gain * makeup;
            }
        }
    }
}

static void deess_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxDeEsser* d = (FxDeEsser*)h;
    if (channels > (int)d->max_channels) channels = (int)d->max_channels;
    deesser_core(d, NULL, channels, out, frames, channels);
}

static void deess_process_sc(FxHandle* h,
                             const float* in,
                             const float* sidechain,
                             float* out,
                             int frames,
                             int channels,
                             int sc_channels)
{
    (void)in;
    FxDeEsser* d = (FxDeEsser*)h;
    if (channels > (int)d->max_channels) channels = (int)d->max_channels;
    if (sc_channels < 1) { deesser_core(d, NULL, channels, out, frames, channels); return; }
    deesser_core(d, sidechain, sc_channels, out, frames, channels);
}

int deesser_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "DeEsser";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 2;  // accepts sidechain
    out->num_outputs = 1;
    out->num_params = 8;
    out->param_names[0] = "center_hz";
    out->param_names[1] = "Q";
    out->param_names[2] = "threshold_dB";
    out->param_names[3] = "ratio";
    out->param_names[4] = "attack_ms";
    out->param_names[5] = "release_ms";
    out->param_names[6] = "makeup_dB";
    out->param_names[7] = "band_only";
    out->param_defaults[0] = 6000.0f;
    out->param_defaults[1] = 2.0f;
    out->param_defaults[2] = -25.0f;
    out->param_defaults[3] = 4.0f;
    out->param_defaults[4] = 2.0f;
    out->param_defaults[5] = 80.0f;
    out->param_defaults[6] = 0.0f;
    out->param_defaults[7] = 0.0f;
    out->latency_samples = 0;
    return 1;
}

int deesser_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxDeEsser* d = (FxDeEsser*)calloc(1, sizeof(FxDeEsser));
    if (!d) return 0;

    d->sr = (float)sample_rate;
    d->max_channels = max_channels ? max_channels : 2;

    d->env = (float*)calloc(d->max_channels, sizeof(float));
    d->bp.z1 = (float*)calloc(d->max_channels, sizeof(float));
    d->bp.z2 = (float*)calloc(d->max_channels, sizeof(float));
    if (!d->env || !d->bp.z1 || !d->bp.z2) { deess_destroy((FxHandle*)d); return 0; }

    // defaults (match get_desc)
    d->center_hz = 6000.0f;
    d->q = 2.0f;
    d->thresh_dB = -25.0f;
    d->ratio = 4.0f;
    d->attack_ms = 2.0f;
    d->release_ms = 80.0f;
    d->makeup_dB = 0.0f;
    d->band_only = 0.0f;
    biquad_bp_design(&d->bp, d->sr, d->center_hz, d->q);
    deess_reset((FxHandle*)d);

    out_vt->process    = deess_process;
    out_vt->process_sc = deess_process_sc;
    out_vt->set_param  = deess_set_param;
    out_vt->reset      = deess_reset;
    out_vt->destroy    = deess_destroy;

    *out_handle = (FxHandle*)d;
    return 1;
}
