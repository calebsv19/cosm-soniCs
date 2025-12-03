
// fx_stereo_width.c — Mid/Side width control (interleaved, in-place)
// Params:
//   0: width   (0..2)    — 0 = mono (side mute), 1 = original, 2 = super-wide (be careful)
//   1: balance (-1..1)   — pan mid between L/R (simple stereo balance)
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }

typedef struct FxWidth {
    float sr;
    unsigned max_channels;
    float width;
    float balance;
} FxWidth;

static void width_set_param(FxHandle* h, uint32_t idx, float value)
{
    FxWidth* w = (FxWidth*)h;
    switch (idx) {
        case 0: w->width = clampf(value, 0.0f, 2.0f); break;
        case 1: w->balance = clampf(value, -1.0f, 1.0f); break;
        default: break;
    }
}

static void width_reset(FxHandle* h){ (void)h; }
static void width_destroy(FxHandle* h){ free(h); }

static void width_process(FxHandle* h, const float* in, float* out, int frames, int channels)
{
    (void)in;
    FxWidth* w = (FxWidth*)h;
    if (channels < 2) return; // only meaningful for stereo
    const float wid = w->width;
    const float bal = w->balance;

    for (int n = 0; n < frames; ++n) {
        int base = n * channels;
        float L = out[base + 0];
        float R = out[base + 1];
        float M = 0.5f * (L + R);
        float S = 0.5f * (L - R);

        S *= wid;            // widen or narrow
        // balance: pan mid between L/R
        float Ml = M * (1.0f - clampf(bal, 0.0f, 1.0f));
        float Mr = M * (1.0f + clampf(bal, -1.0f, 0.0f));

        float L2 = Ml + S;
        float R2 = Mr - S;

        out[base + 0] = L2;
        out[base + 1] = R2;
        // pass-through extra channels untouched
        for (int ch = 2; ch < channels; ++ch) {
            (void)ch;
        }
    }
}

int width_get_desc(FxDesc *out)
{
    if (!out) return 0;
    out->name = "StereoWidth";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs = 1;
    out->num_outputs = 1;
    out->num_params = 2;
    out->param_names[0] = "width";
    out->param_names[1] = "balance";
    out->param_defaults[0] = 1.2f;
    out->param_defaults[1] = 0.0f;
    out->latency_samples = 0;
    return 1;
}

int width_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                 uint32_t sample_rate, uint32_t max_block, uint32_t max_channels)
{
    (void)desc; (void)max_block;
    FxWidth* w = (FxWidth*)calloc(1, sizeof(FxWidth));
    if (!w) return 0;
    w->sr = (float)sample_rate;
    w->max_channels = max_channels ? max_channels : 2;
    w->width = 1.2f;
    w->balance = 0.0f;

    out_vt->process = width_process;
    out_vt->set_param = width_set_param;
    out_vt->reset = width_reset;
    out_vt->destroy = width_destroy;
    *out_handle = (FxHandle*)w;
    return 1;
}
