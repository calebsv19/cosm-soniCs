
// fx_svf_filter.c — State-Variable Filter (LP/HP/BP/Notch) per channel
// Modeled as simple SVF (zero-delay approximation not used here; keep Q moderate)
// Params:
//   0: mode        (0=LP,1=HP,2=BP,3=Notch)
//   1: cutoff_hz   (20..20000)
//   2: q           (0.3..12)
//   3: gain        (-24..+24) dB — output makeup (post filter)
// Notes:
// - Interleaved, in-place
// - Coefficients are updated in set_param() and cached per instance
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }
static inline float dB_to_lin(float dB){ return powf(10.0f, dB*0.05f); }

typedef struct FxSVF {
    float sr;
    unsigned max_channels;

    // params
    int   mode;        // 0..3
    float cutoff_hz;
    float q;
    float gain_db;

    // state per channel
    float *ic1eq;
    float *ic2eq;

    // derived coeffs
    float g;  // tan(pi*f/fs)
    float k;  // 1/Q
    float out_gain;
} FxSVF;

// compute TPT-SVF coeffs (Zavalishin) for good stability
static void svf_update_coeffs(FxSVF* f)
{
    float fc = clampf(f->cutoff_hz, 20.0f, f->sr * 0.45f);
    float Q  = clampf(f->q, 0.3f, 12.0f);
    float g  = tanf((float)M_PI * fc / f->sr);
    f->g = g;
    f->k = 1.0f / Q;
    f->out_gain = dB_to_lin(clampf(f->gain_db, -24.0f, 24.0f));
}

static void svf_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxSVF* f = (FxSVF*)h;
    switch (idx) {
        case 0: f->mode = (int)clampf(value, 0.0f, 3.0f); break;
        case 1: f->cutoff_hz = value; break;
        case 2: f->q = value; break;
        case 3: f->gain_db = value; break;
        default: break;
    }
    svf_update_coeffs(f); // non-RT safe
}

static void svf_reset(FxHandle* h)
{
    FxSVF* f = (FxSVF*)h;
    for (unsigned ch = 0; ch < f->max_channels; ++ch) {
        f->ic1eq[ch] = 0.0f;
        f->ic2eq[ch] = 0.0f;
    }
}

static void svf_destroy(FxHandle* h)
{
    FxSVF* f = (FxSVF*)h;
    free(f->ic1eq);
    free(f->ic2eq);
    free(f);
}

static void svf_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxSVF* f = (FxSVF*)h;
    if (channels > (int)f->max_channels) channels = (int)f->max_channels;

    const float g = f->g;
    const float k = f->k;
    const float og = f->out_gain;

    for (int n = 0; n < frames; ++n) {
        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float v0 = out[base + ch];
            float v1 = f->ic1eq[ch];
            float v2 = f->ic2eq[ch];

            // TPT SVF core
            float t1 = v1 + g*(v0 - v2);
            float t2 = v2 + g*(t1 - k*v2);
            float lp = t2;
            float hp = v0 - k*t1 - t2;
            float bp = t1;
            float notch = hp + lp;

            f->ic1eq[ch] = 2.0f*bp - v1;
            f->ic2eq[ch] = 2.0f*lp - v2;

            float y = 0.0f;
            switch (f->mode) {
                case 0: y = lp; break;
                case 1: y = hp; break;
                case 2: y = bp; break;
                case 3: y = notch; break;
                default: y = lp; break;
            }
            out[base + ch] = y * og;
        }
    }
}

int svf_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "SVF";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 4;
    out->param_names[0] = "mode";
    out->param_names[1] = "cutoff_hz";
    out->param_names[2] = "q";
    out->param_names[3] = "gain_dB";
    out->param_defaults[0] = 0.0f;
    out->param_defaults[1] = 1200.0f;
    out->param_defaults[2] = 0.707f;
    out->param_defaults[3] = 0.0f;
    out->latency_samples = 0;
    return 1;
}

int svf_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
               uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxSVF* f = (FxSVF*)calloc(1, sizeof(FxSVF));
    if (!f) return 0;
    f->sr = (float)sample_rate;
    f->max_channels = max_channels ? max_channels : 2;

    f->ic1eq = (float*)calloc(f->max_channels, sizeof(float));
    f->ic2eq = (float*)calloc(f->max_channels, sizeof(float));
    if (!f->ic1eq || !f->ic2eq) { svf_destroy((FxHandle*)f); return 0; }

    // defaults
    f->mode = 0;
    f->cutoff_hz = 1200.0f;
    f->q = 0.707f;
    f->gain_db = 0.0f;
    svf_update_coeffs(f);
    svf_reset((FxHandle*)f);

    out_vt->process = svf_process;
    out_vt->set_param = svf_set_param;
    out_vt->reset = svf_reset;
    out_vt->destroy = svf_destroy;
    *out_handle = (FxHandle*)f;
    return 1;
}
