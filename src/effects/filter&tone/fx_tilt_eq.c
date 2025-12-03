
// fx_tilt_eq.c — Simple first-order tilt EQ around a pivot frequency
// Implemented as a pair of low/high shelf filters with opposite gains.
// Params:
//   0: pivot_hz (100..5000)
//   1: tilt_dB  (-12..+12)  — positive = brighter, negative = darker
//   2: mix      (0..1)
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }

typedef struct Shelf {
    float b0,b1,b2,a1,a2;
    float *z1,*z2;
} Shelf;

typedef struct FxTilt {
    float sr;
    unsigned max_channels;

    float pivot_hz;
    float tilt_dB;
    float mix;

    Shelf low, high;
} FxTilt;

// RBJ low shelf
static void lowshelf_design(Shelf* s, float sr, float f0, float dB)
{
    float A = powf(10.0f, dB/40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / sr;
    float alpha = sinf(w0)/2.0f * sqrtf((A + 1/A) * (1.0f/0.5f - 1.0f) + 2.0f); // S=0.5
    float cosw0 = cosf(w0);

    float b0 =    A*( (A+1) - (A-1)*cosw0 + 2*sqrtf(A)*alpha );
    float b1 =  2*A*( (A-1) - (A+1)*cosw0 );
    float b2 =    A*( (A+1) - (A-1)*cosw0 - 2*sqrtf(A)*alpha );
    float a0 =        (A+1) + (A-1)*cosw0 + 2*sqrtf(A)*alpha;
    float a1 =   -2*( (A-1) + (A+1)*cosw0 );
    float a2 =        (A+1) + (A-1)*cosw0 - 2*sqrtf(A)*alpha;

    s->b0=b0/a0; s->b1=b1/a0; s->b2=b2/a0;
    s->a1=a1/a0; s->a2=a2/a0;
}

// RBJ high shelf
static void highshelf_design(Shelf* s, float sr, float f0, float dB)
{
    float A = powf(10.0f, dB/40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / sr;
    float alpha = sinf(w0)/2.0f * sqrtf((A + 1/A) * (1.0f/0.5f - 1.0f) + 2.0f); // S=0.5
    float cosw0 = cosf(w0);

    float b0 =    A*( (A+1) + (A-1)*cosw0 + 2*sqrtf(A)*alpha );
    float b1 = -2*A*( (A-1) + (A+1)*cosw0 );
    float b2 =    A*( (A+1) + (A-1)*cosw0 - 2*sqrtf(A)*alpha );
    float a0 =        (A+1) - (A-1)*cosw0 + 2*sqrtf(A)*alpha;
    float a1 =    2*( (A-1) - (A+1)*cosw0 );
    float a2 =        (A+1) - (A-1)*cosw0 - 2*sqrtf(A)*alpha;

    s->b0=b0/a0; s->b1=b1/a0; s->b2=b2/a0;
    s->a1=a1/a0; s->a2=a2/a0;
}

static inline float shelf_tick(Shelf* s, int ch, float x)
{
    float v = x - s->a1*s->z1[ch] - s->a2*s->z2[ch];
    float y = s->b0*v + s->b1*s->z1[ch] + s->b2*s->z2[ch];
    s->z2[ch] = s->z1[ch];
    s->z1[ch] = v;
    return y;
}

static void tilt_update(FxTilt* t)
{
    float f0 = clampf(t->pivot_hz, 100.0f, t->sr*0.45f);
    float tilt = clampf(t->tilt_dB, -12.0f, 12.0f);
    // split tilt: low gets -tilt/2, high gets +tilt/2 to pivot at f0
    lowshelf_design(&t->low,  t->sr, f0, -tilt*0.5f);
    highshelf_design(&t->high, t->sr, f0, +tilt*0.5f);
}

static void tilt_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxTilt* t = (FxTilt*)h;
    switch (idx) {
        case 0: t->pivot_hz = value; break;
        case 1: t->tilt_dB  = value; break;
        case 2: t->mix      = clampf(value, 0.0f, 1.0f); break;
        default: break;
    }
    tilt_update(t);
}

static void tilt_reset(FxHandle* h)
{
    FxTilt* t = (FxTilt*)h;
    for (unsigned ch = 0; ch < t->max_channels; ++ch) {
        t->low.z1[ch] = t->low.z2[ch] = 0.0f;
        t->high.z1[ch]= t->high.z2[ch]= 0.0f;
    }
}

static void tilt_destroy(FxHandle* h)
{
    FxTilt* t = (FxTilt*)h;
    free(t->low.z1);  free(t->low.z2);
    free(t->high.z1); free(t->high.z2);
    free(t);
}

static void tilt_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxTilt* t = (FxTilt*)h;
    if (channels > (int)t->max_channels) channels = (int)t->max_channels;

    const float mix = t->mix;
    const float dry = 1.0f - mix;

    for (int n = 0; n < frames; ++n) {
        int base = n * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float x = out[base + ch];
            float y = shelf_tick(&t->low, ch, x);
            y = shelf_tick(&t->high, ch, y);
            out[base + ch] = dry * x + mix * y;
        }
    }
}

int tilt_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "TiltEQ";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 3;
    out->param_names[0] = "pivot_hz";
    out->param_names[1] = "tilt_dB";
    out->param_names[2] = "mix";
    out->param_defaults[0] = 1000.0f;
    out->param_defaults[1] = 0.0f;
    out->param_defaults[2] = 1.0f;
    out->latency_samples = 0;
    return 1;
}

int tilt_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxTilt* t = (FxTilt*)calloc(1, sizeof(FxTilt));
    if (!t) return 0;
    t->sr = (float)sample_rate;
    t->max_channels = max_channels ? max_channels : 2;

    t->low.z1  = (float*)calloc(t->max_channels, sizeof(float));
    t->low.z2  = (float*)calloc(t->max_channels, sizeof(float));
    t->high.z1 = (float*)calloc(t->max_channels, sizeof(float));
    t->high.z2 = (float*)calloc(t->max_channels, sizeof(float));
    if (!t->low.z1 || !t->low.z2 || !t->high.z1 || !t->high.z2) { tilt_destroy((FxHandle*)t); return 0; }

    // defaults
    t->pivot_hz = 1000.0f;
    t->tilt_dB = 0.0f;
    t->mix = 1.0f;
    tilt_update(t);
    tilt_reset((FxHandle*)t);

    out_vt->process = tilt_process;
    out_vt->set_param = tilt_set_param;
    out_vt->reset = tilt_reset;
    out_vt->destroy = tilt_destroy;
    *out_handle = (FxHandle*)t;
    return 1;
}
