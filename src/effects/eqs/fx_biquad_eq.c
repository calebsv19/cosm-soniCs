// fx_biquad_eq.c — single-band parametric EQ (interleaved, in-place)
// Types: 0=LowShelf, 1=Peaking, 2=HighShelf
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct FxBiquad {
    // coeffs
    float b0, b1, b2, a1, a2;
    // per-channel state (Direct Form II Transposed)
    float *z1;      // size = channels at create
    float *z2;      // size = channels at create
    // params
    float sr;
    float freq;     // Hz
    float q;        // Q
    float gain_db;  // dB (for LS/PK/HS)
    int   type;     // 0 LS, 1 PK, 2 HS
    // capacity
    unsigned max_channels;
} FxBiquad;

static inline float dB_to_A(float db) { return powf(10.0f, db * 0.05f); } // 10^(db/20)
static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }

static void biquad_recalc(FxBiquad* f) {
    float fs = f->sr;
    float w0 = 2.0f * (float)M_PI * clampf(f->freq, 10.0f, fs * 0.45f) / fs;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * clampf(f->q, 0.1f, 24.0f));
    float A = dB_to_A(f->gain_db);

    float b0, b1, b2, a0, a1, a2;

    switch (f->type) {
        case 0: { // Low Shelf (RBJ)
            float S = 1.0f; // slope (we keep it simple)
            float beta = sqrtf(A)/clampf(S,0.1f,10.0f);
            float ap1 = A + 1.0f;
            float am1 = A - 1.0f;

            b0 =    A*( ap1 - am1*cosw0 + 2.0f*beta*sinw0 );
            b1 =  2.0f*A*( am1 - ap1*cosw0 );
            b2 =    A*( ap1 - am1*cosw0 - 2.0f*beta*sinw0 );
            a0 =        ( ap1 + am1*cosw0 + 2.0f*beta*sinw0 );
            a1 =   -2.0f*( am1 + ap1*cosw0 );
            a2 =        ( ap1 + am1*cosw0 - 2.0f*beta*sinw0 );
        } break;

        case 2: { // High Shelf (RBJ)
            float S = 1.0f;
            float beta = sqrtf(A)/clampf(S,0.1f,10.0f);
            float ap1 = A + 1.0f;
            float am1 = A - 1.0f;

            b0 =    A*( ap1 + am1*cosw0 + 2.0f*beta*sinw0 );
            b1 = -2.0f*A*( am1 + ap1*cosw0 );
            b2 =    A*( ap1 + am1*cosw0 - 2.0f*beta*sinw0 );
            a0 =        ( ap1 - am1*cosw0 + 2.0f*beta*sinw0 );
            a1 =    2.0f*( am1 - ap1*cosw0 );
            a2 =        ( ap1 - am1*cosw0 - 2.0f*beta*sinw0 );
        } break;

        default: { // 1: Peaking EQ (RBJ)
            b0 = 1.0f + alpha*A;
            b1 = -2.0f*cosw0;
            b2 = 1.0f - alpha*A;
            a0 = 1.0f + alpha/A;
            a1 = -2.0f*cosw0;
            a2 = 1.0f - alpha/A;
        } break;
    }

    // normalize
    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}

static void biquad_process(FxHandle* h, const float* in, float* out, int frames, int channels) {
    (void)in; // in-place
    FxBiquad* f = (FxBiquad*)h;

    // DF2T per-channel
    for (int n = 0; n < frames; ++n) {
        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float x = out[base + ch];
            float v = x - f->a1 * f->z1[ch] - f->a2 * f->z2[ch];
            float y = f->b0 * v + f->b1 * f->z1[ch] + f->b2 * f->z2[ch];
            f->z2[ch] = f->z1[ch];
            f->z1[ch] = v;
            // tiny DC/denormal guard not needed with DF2T + typical audio content,
            // but harmless to add:
            if (fabsf(y) < 1e-30f) y = 0.0f;
            out[base + ch] = y;
        }
    }
}

static void biquad_set_param(FxHandle* h, uint32_t idx, float value) {
    FxBiquad* f = (FxBiquad*)h;
    switch (idx) {
        case 0: f->type = (int)clampf(value, 0.0f, 2.0f); break; // 0/1/2
        case 1: f->freq = clampf(value, 10.0f, f->sr*0.45f); break;
        case 2: f->q    = clampf(value, 0.1f, 24.0f); break;
        case 3: f->gain_db = clampf(value, -24.0f, 24.0f); break;
        default: break;
    }
    biquad_recalc(f); // non-RT ok
}

static void biquad_reset(FxHandle* h) {
    FxBiquad* f = (FxBiquad*)h;
    for (unsigned c = 0; c < f->max_channels; ++c) {
        f->z1[c] = 0.0f;
        f->z2[c] = 0.0f;
    }
}

static void biquad_destroy(FxHandle* h) {
    FxBiquad* f = (FxBiquad*)h;
    free(f->z1);
    free(f->z2);
    free(f);
}

int biquad_get_desc(FxDesc *out) {
    if (!out) return 0;
    out->name = "BiquadEQ";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 4;
    out->param_names[0] = "type";     // 0=LowShelf,1=Peak,2=HighShelf
    out->param_names[1] = "freq_hz";
    out->param_names[2] = "Q";
    out->param_names[3] = "gain_dB";
    out->param_defaults[0] = 1.0f;   // Peak
    out->param_defaults[1] = 1000.0f;
    out->param_defaults[2] = 0.707f;
    out->param_defaults[3] = 0.0f;
    out->latency_samples = 0;
    return 1;
}

int biquad_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                  uint32_t sample_rate, uint32_t max_block, uint32_t max_channels) {
    (void)desc; (void)max_block;
    FxBiquad* f = (FxBiquad*)calloc(1, sizeof(FxBiquad));
    if (!f) return 0;

    f->sr = (float)sample_rate;
    f->freq = 1000.0f;
    f->q = 0.707f;
    f->gain_db = 0.0f;
    f->type = 1;
    f->max_channels = max_channels > 0 ? max_channels : 2;

    f->z1 = (float*)calloc(f->max_channels, sizeof(float));
    f->z2 = (float*)calloc(f->max_channels, sizeof(float));
    if (!f->z1 || !f->z2) {
        biquad_destroy((FxHandle*)f);
        return 0;
    }

    biquad_recalc(f);
    biquad_reset((FxHandle*)f);

    out_vt->process = biquad_process;
    out_vt->set_param = biquad_set_param;
    out_vt->reset = biquad_reset;
    out_vt->destroy = biquad_destroy;

    *out_handle = (FxHandle*)f;
    return 1;
}
