
// fx_autowah.c — Envelope-following band-pass "auto-wah"
// Params:
//   0: min_hz     (200..2000)   — low end of sweep
//   1: max_hz     (800..6000)   — high end of sweep
//   2: q          (0.5..8.0)    — band-pass Q
//   3: attack_ms  (0.1..20)     — detector attack
//   4: release_ms (5..400)      — detector release
//   5: mix        (0..1)        — dry/wet
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }

typedef struct Biquad {
    float b0,b1,b2,a1,a2;
    float *z1,*z2;
} Biquad;

typedef struct FxAutoWah {
    float sr;
    unsigned max_channels;

    float min_hz, max_hz, q;
    float attack_ms, release_ms;
    float mix;

    float env;   // mono detector for simplicity
    Biquad bp;
} FxAutoWah;

static void bp_design(Biquad* bq, float sr, float f0, float Q)
{
    f0 = clampf(f0, 50.0f, sr*0.45f);
    Q  = clampf(Q, 0.3f, 12.0f);
    float w0 = 2.0f * (float)M_PI * f0 / sr;
    float alpha = sinf(w0) / (2.0f * Q);
    float cosw0 = cosf(w0);

    float b0 =   alpha;
    float b1 =   0.0f;
    float b2 =  -alpha;
    float a0 =   1.0f + alpha;
    float a1 =  -2.0f * cosw0;
    float a2 =   1.0f - alpha;

    bq->b0 = b0/a0; bq->b1 = b1/a0; bq->b2 = b2/a0;
    bq->a1 = a1/a0; bq->a2 = a2/a0;
}

static inline float bqtick(Biquad* bq, int ch, float x)
{
    float v = x - bq->a1 * bq->z1[ch] - bq->a2 * bq->z2[ch];
    float y = bq->b0 * v + bq->b1 * bq->z1[ch] + bq->b2 * bq->z2[ch];
    bq->z2[ch] = bq->z1[ch];
    bq->z1[ch] = v;
    return y;
}

static void aw_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxAutoWah* w = (FxAutoWah*)h;
    switch (idx) {
        case 0: w->min_hz    = clampf(value, 200.f, 2000.f); break;
        case 1: w->max_hz    = clampf(value, 800.f, 6000.f); break;
        case 2: w->q         = clampf(value, 0.5f, 8.0f); break;
        case 3: w->attack_ms = clampf(value, 0.1f, 20.f); break;
        case 4: w->release_ms= clampf(value, 5.f, 400.f); break;
        case 5: w->mix       = clampf(value, 0.0f, 1.0f); break;
        default: break;
    }
    // no coeff update here; coeffs change every frame based on env
}

static void aw_reset(FxHandle* h)
{
    FxAutoWah* w = (FxAutoWah*)h;
    w->env = 0.0f;
    for (unsigned ch = 0; ch < w->max_channels; ++ch) {
        w->bp.z1[ch] = 0.0f;
        w->bp.z2[ch] = 0.0f;
    }
}

static void aw_destroy(FxHandle* h)
{
    FxAutoWah* w = (FxAutoWah*)h;
    free(w->bp.z1);
    free(w->bp.z2);
    free(w);
}

static void aw_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxAutoWah* w = (FxAutoWah*)h;
    if (channels > (int)w->max_channels) channels = (int)w->max_channels;

    const float atk_a = expf(-1.0f / ((w->attack_ms  * 0.001f) * w->sr));
    const float rel_a = expf(-1.0f / ((w->release_ms * 0.001f) * w->sr));
    const float minf = w->min_hz;
    const float maxf = fmaxf(w->min_hz + 10.0f, w->max_hz);
    const float mix = w->mix;
    const float dry = 1.0f - mix;

    for (int n = 0; n < frames; ++n) {
        // mono envelope from L channel (simple rectifier follower)
        float xL = out[n * channels + 0];
        float lev = fabsf(xL);
        float coef = (lev > w->env) ? atk_a : rel_a;
        w->env = coef * w->env + (1.0f - coef) * lev;

        // map env (0..~1) to frequency range (log-ish mapping)
        float t = w->env;                 // 0..1-ish
        t = sqrtf(fminf(t, 1.0f));        // gentle curve
        float f0 = minf * powf((maxf / minf), t);
        bp_design(&w->bp, w->sr, f0, w->q);

        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float x = out[base + ch];
            float y = bqtick(&w->bp, ch, x);
            out[base + ch] = dry * x + mix * y;
        }
    }
}

int autowah_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "AutoWah";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 6;
    out->param_names[0] = "min_hz";
    out->param_names[1] = "max_hz";
    out->param_names[2] = "q";
    out->param_names[3] = "attack_ms";
    out->param_names[4] = "release_ms";
    out->param_names[5] = "mix";
    out->param_defaults[0] = 300.0f;
    out->param_defaults[1] = 2500.0f;
    out->param_defaults[2] = 2.0f;
    out->param_defaults[3] = 2.0f;
    out->param_defaults[4] = 120.0f;
    out->param_defaults[5] = 0.8f;
    out->latency_samples = 0;
    return 1;
}

int autowah_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxAutoWah* w = (FxAutoWah*)calloc(1, sizeof(FxAutoWah));
    if (!w) return 0;

    w->sr = (float)sample_rate;
    w->max_channels = max_channels ? max_channels : 2;

    w->bp.z1 = (float*)calloc(w->max_channels, sizeof(float));
    w->bp.z2 = (float*)calloc(w->max_channels, sizeof(float));
    if (!w->bp.z1 || !w->bp.z2) { aw_destroy((FxHandle*)w); return 0; }

    // defaults
    w->min_hz = 300.0f;
    w->max_hz = 2500.0f;
    w->q = 2.0f;
    w->attack_ms = 2.0f;
    w->release_ms = 120.0f;
    w->mix = 0.8f;
    aw_reset((FxHandle*)w);

    out_vt->process = aw_process;
    out_vt->set_param = aw_set_param;
    out_vt->reset = aw_reset;
    out_vt->destroy = aw_destroy;
    *out_handle = (FxHandle*)w;
    return 1;
}
