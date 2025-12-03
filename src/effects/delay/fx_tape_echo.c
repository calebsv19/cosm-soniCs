// fx_tape_echo.c — Tape-style echo with wobble, high-cut, and soft saturation in feedback
//
// Params:
//   0: time_ms        (20..1500,  default 450)
//   1: feedback       (0..0.97,   default 0.55)
//   2: mix            (0..1,      default 0.35)
//   3: wobble_hz      (0..5,      default 0.4)
//   4: wobble_depth_ms(0..12,     default 3.0)   — peak-to-peak/2 depth (approx)
//   5: highcut_Hz     (1000..12000, default 6000)
//   6: sat            (0..1,      default 0.3)   — soft saturation amount in the loop
//
// Implementation notes:
// - Fractional delay with linear interpolation.
// - Wobble modulates read position only (classic tape feel).
// - One-pole lowpass filters the feedback path to darken repeats.
// - Soft saturation via tanh(sat*x) inside the loop; sat=0 bypasses.
//
// Realtime safe: pre-alloc per-channel delay buffers on create; in-place process.
// Flags: not marking INPLACE_OK because we rely on an internal buffer; actual processing is in-place.

#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}

typedef struct FxTapeEcho {
    float sr; unsigned max_ch; unsigned max_block;
    // params
    float time_ms, feedback, mix, wobble_hz, wobble_depth_ms, highcut_Hz, sat;
    // state
    float **buf;       // per-channel circular buffers
    unsigned *size;    // per-channel sizes (same across channels)
    unsigned *wpos;    // write head positions
    float *lp;         // lowpass state per channel
    float lpf_a;       // lpf coefficient (computed from highcut)
    float phase;       // LFO phase 0..1
} FxTapeEcho;

static void te_free(FxTapeEcho* d){
    if(!d) return;
    if (d->buf){
        for(unsigned c=0;c<d->max_ch;++c) free(d->buf[c]);
        free(d->buf);
    }
    free(d->size); free(d->wpos); free(d->lp);
}

static void te_alloc(FxTapeEcho* d){
    unsigned max_delay_samps = (unsigned)(d->sr * 1.6f) + d->max_block + 8; // up to 1500ms + margin
    d->buf  = (float**)calloc(d->max_ch, sizeof(float*));
    d->size = (unsigned*)calloc(d->max_ch, sizeof(unsigned));
    d->wpos = (unsigned*)calloc(d->max_ch, sizeof(unsigned));
    d->lp   = (float*)calloc(d->max_ch, sizeof(float));
    for(unsigned c=0;c<d->max_ch;++c){
        d->buf[c]  = (float*)calloc(max_delay_samps, sizeof(float));
        d->size[c] = max_delay_samps;
        d->wpos[c] = 0u;
        d->lp[c]   = 0.f;
    }
}

static inline float read_frac(float* buf, unsigned size, unsigned wpos, float delay){
    float rpos = (float)wpos - delay;
    while (rpos < 0) rpos += size;
    unsigned i0 = ((unsigned)rpos) % size;
    unsigned i1 = (i0 + 1) % size;
    float frac = rpos - floorf(rpos);
    return buf[i0]*(1.f-frac) + buf[i1]*frac;
}

static inline float softsat(float x, float amt){
    if (amt <= 0.0001f) return x;
    // simple tanh curve scaled by amt
    float y = tanhf(amt * x);
    return y / (amt <= 1e-6f ? 1.f : amt);
}

static void te_update_lowpass(FxTapeEcho* d){
    // one-pole lowpass coefficient from fc
    float fc = d->highcut_Hz;
    if (fc < 1000.f) fc = 1000.f;
    if (fc > 12000.f) fc = 12000.f;
    float x = expf(-2.f * (float)M_PI * fc / d->sr);
    d->lpf_a = x; // y = (1-a)*x + a*y_prev
}

static void te_set_param(FxHandle* h, uint32_t idx, float v){
    FxTapeEcho* d=(FxTapeEcho*)h;
    switch(idx){
        case 0: d->time_ms        = clampf(v, 20.f, 1500.f); break;
        case 1: d->feedback       = clampf(v, 0.f, 0.97f);   break;
        case 2: d->mix            = clampf(v, 0.f, 1.f);     break;
        case 3: d->wobble_hz      = clampf(v, 0.f, 5.f);     break;
        case 4: d->wobble_depth_ms= clampf(v, 0.f, 12.f);    break;
        case 5: d->highcut_Hz     = clampf(v, 1000.f, 12000.f); te_update_lowpass(d); break;
        case 6: d->sat            = clampf(v, 0.f, 1.f);     break;
        default: break;
    }
}

static void te_reset(FxHandle* h){
    FxTapeEcho* d=(FxTapeEcho*)h;
    for(unsigned c=0;c<d->max_ch;++c){
        for(unsigned i=0;i<d->size[c];++i) d->buf[c][i]=0.f;
        d->wpos[c]=0u;
        d->lp[c]=0.f;
    }
    d->phase = 0.f;
}

static void te_destroy(FxHandle* h){
    FxTapeEcho* d=(FxTapeEcho*)h;
    te_free(d);
    free(d);
}

static void te_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxTapeEcho* d=(FxTapeEcho*)h;
    int ch = channels; if (ch < 1) return;
    if ((unsigned)ch > d->max_ch) ch = (int)d->max_ch;

    const float wet = d->mix, dry = 1.f - wet;
    const float base_delay = (d->time_ms * 0.001f) * d->sr;
    float phase = d->phase;
    const float inc = d->wobble_hz / d->sr;

    for(int n=0;n<frames;++n){
        float wob = (d->wobble_depth_ms * 0.001f) * d->sr;
        float lfo = sinf(2.f*(float)M_PI * phase);
        float dly = base_delay + wob * lfo; // ± wobble
        if (dly < 1.f) dly = 1.f;
        // cap to buffer
        if (dly > (float)(d->size[0] - d->max_block - 4)) dly = (float)(d->size[0] - d->max_block - 4);

        for(int c=0;c<ch;++c){
            unsigned size = d->size[c];
            unsigned w = d->wpos[c];
            int idx = n*channels + c;
            float x = out[idx];

            // read delayed sample (modulated)
            float delayed = read_frac(d->buf[c], size, w, dly);

            // feedback path: LPF then saturation
            float yfb = (1.f - d->lpf_a)*delayed + d->lpf_a * d->lp[c];
            d->lp[c] = yfb;
            yfb = softsat(yfb, d->sat);

            // mix output
            float y = dry*x + wet*delayed;

            // write input + feedback to buffer
            d->buf[c][w] = x + d->feedback * yfb;

            // advance write head
            w = (w + 1) % size;
            d->wpos[c] = w;

            out[idx] = y;
        }

        // fold channels if any extra
        for(int c=d->max_ch; c<channels; ++c){
            int idx = n*channels + c;
            out[idx] = out[n*channels + (c % ch)];
        }

        phase += inc;
        if (phase >= 1.f) phase -= 1.f;
        if (phase < 0.f)  phase += 1.f;
    }
    d->phase = phase;
}

int tape_echo_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "TapeEcho";
    out->api_version = FX_API_VERSION;
    out->flags = 0;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 7;
    out->param_names[0]    = "time_ms";
    out->param_names[1]    = "feedback";
    out->param_names[2]    = "mix";
    out->param_names[3]    = "wobble_hz";
    out->param_names[4]    = "wobble_depth_ms";
    out->param_names[5]    = "highcut_Hz";
    out->param_names[6]    = "sat";
    out->param_defaults[0] = 450.0f;
    out->param_defaults[1] = 0.55f;
    out->param_defaults[2] = 0.35f;
    out->param_defaults[3] = 0.40f;
    out->param_defaults[4] = 3.0f;
    out->param_defaults[5] = 6000.0f;
    out->param_defaults[6] = 0.30f;
    out->latency_samples = 0;
    return 1;
}

int tape_echo_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                     uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc;
    FxTapeEcho* d=(FxTapeEcho*)calloc(1,sizeof(FxTapeEcho));
    if(!d) return 0;
    d->sr=(float)sample_rate; d->max_block=max_block; d->max_ch = max_channels?max_channels:2;
    d->time_ms=450.f; d->feedback=0.55f; d->mix=0.35f; d->wobble_hz=0.4f;
    d->wobble_depth_ms=3.0f; d->highcut_Hz=6000.f; d->sat=0.3f;
    te_alloc(d); te_update_lowpass(d); te_reset((FxHandle*)d);
    out_vt->process = te_process;
    out_vt->set_param = te_set_param;
    out_vt->reset = te_reset;
    out_vt->destroy = te_destroy;
    *out_handle = (FxHandle*)d;
    return 1;
}
