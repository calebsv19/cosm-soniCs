// fx_formant_filter.c — Dual-formant band-pass filter with vowel presets and LFO sweep
// Two RBJ band-pass filters (constant skirt gain). Choose vowel preset and modulate with LFO.
//
// Params:
//   0: vowel_idx  (0..4 default 0)  — {A,E,I,O,U}
//   1: mod_rate_hz (0..5 default 0.6)
//   2: mod_depth   (0..1 default 0.5) — sweeps ±depth*20% around each center
//   3: Q1          (1..20 default 5)
//   4: Q2          (1..20 default 10)
//   5: mix         (0..1  default 1)
//
#include <stdlib.h>
#include <math.h>
#include "effects/effects_api.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}

typedef struct Biquad { float b0,b1,b2,a1,a2,z1,z2; } Biquad;

static void biq_reset(Biquad* b){ b->z1 = b->z2 = 0.f; }
static float biq_run(Biquad* b, float x){
    float y = b->b0*x + b->z1;
    b->z1 = b->b1*x - b->a1*y + b->z2;
    b->z2 = b->b2*x - b->a2*y;
    return y;
}

// RBJ band-pass (constant skirt gain, peak gain = Q)
static void make_bandpass(Biquad* b, float sr, float freq, float Q){
    float w0 = 2.f*(float)M_PI * freq / sr;
    float cosw0 = cosf(w0), sinw0 = sinf(w0);
    float alpha = sinw0/(2.f*Q);

    float b0 =   sinw0/2.f;
    float b1 =   0.f;
    float b2 =  -sinw0/2.f;
    float a0 =   1.f + alpha;
    float a1 =  -2.f*cosw0;
    float a2 =   1.f - alpha;

    b->b0 = b0/a0; b->b1 = b1/a0; b->b2 = b2/a0;
    b->a1 = a1/a0; b->a2 = a2/a0;
}

typedef struct FxFormant {
    float sr; unsigned max_ch;
    int vowel_idx;
    float mod_rate_hz, mod_depth;
    float Q1, Q2;
    float mix;
    float phase; // LFO
    Biquad *f1, *f2; // per-channel
} FxFormant;

// simple vowel table (male-ish): A,E,I,O,U
static const float VOWEL_F1[5] = { 800.f, 400.f, 350.f, 450.f, 325.f };
static const float VOWEL_F2[5] = {1150.f,2000.f,2200.f, 800.f, 700.f };

static void fm_recalc(FxFormant* f){
    float lfo = 0.f; // recalc at neutral; per-sample mod applies in process
    float d = 1.f + f->mod_depth * 0.2f * (2.f*lfo - 1.f);
    float c1 = VOWEL_F1[f->vowel_idx] * d;
    float c2 = VOWEL_F2[f->vowel_idx] * d;
    for(unsigned c=0;c<f->max_ch;++c){
        make_bandpass(&f->f1[c], f->sr, c1, f->Q1);
        make_bandpass(&f->f2[c], f->sr, c2, f->Q2);
        biq_reset(&f->f1[c]); biq_reset(&f->f2[c]);
    }
}

static void fm_set_param(FxHandle* h, uint32_t idx, float v){
    FxFormant* f=(FxFormant*)h;
    switch(idx){
        case 0: { int vi=(int)roundf(v); if(vi<0)vi=0; if(vi>4)vi=4; f->vowel_idx=vi; } break;
        case 1: f->mod_rate_hz = clampf(v, 0.f, 5.f); break;
        case 2: f->mod_depth   = clampf(v, 0.f, 1.f); break;
        case 3: f->Q1          = clampf(v, 1.f, 20.f); break;
        case 4: f->Q2          = clampf(v, 1.f, 20.f); break;
        case 5: f->mix         = clampf(v, 0.f, 1.f); break;
        default: break;
    }
    fm_recalc(f);
}
static void fm_reset(FxHandle* h){
    FxFormant* f=(FxFormant*)h;
    f->phase = 0.f;
    for(unsigned c=0;c<f->max_ch;++c){ biq_reset(&f->f1[c]); biq_reset(&f->f2[c]); }
}
static void fm_destroy(FxHandle* h){ FxFormant* f=(FxFormant*)h; free(f->f1); free(f->f2); free(f); }

static void fm_process(FxHandle* h, const float* in, float* out, int frames, int channels){
    (void)in;
    FxFormant* f=(FxFormant*)h;
    int ch = channels; if (ch < 1) return;
    if ((unsigned)ch > f->max_ch) ch = (int)f->max_ch;

    float phase = f->phase;
    const float inc = f->mod_rate_hz / f->sr;
    const float wet = f->mix, dry = 1.f - wet;

    for(int n=0;n<frames;++n){
        float lfo = 0.5f + 0.5f*sinf(2.f*(float)M_PI * phase);
        float d = 1.f + f->mod_depth * 0.2f * (2.f*lfo - 1.f);
        float c1 = VOWEL_F1[f->vowel_idx] * d;
        float c2 = VOWEL_F2[f->vowel_idx] * d;

        // update coeffs slowly? We update per-sample—cheap enough for 2 biquads.
        for(int c=0;c<ch;++c){
            // Recompute coeffs on the fly (tiny cost)
            make_bandpass(&f->f1[c], f->sr, c1, f->Q1);
            make_bandpass(&f->f2[c], f->sr, c2, f->Q2);
        }

        for(int c=0;c<ch;++c){
            int i = n*channels + c;
            float x = out[i];
            float y = biq_run(&f->f1[c], x);
            y = biq_run(&f->f2[c], y);
            out[i] = dry*x + wet*y;
        }

        phase += inc;
        if (phase >= 1.f) phase -= 1.f;
        if (phase < 0.f)  phase += 1.f;
    }
    f->phase = phase;
}

int formant_get_desc(FxDesc *out){
    if(!out) return 0;
    out->name = "FormantFilter";
    out->api_version = FX_API_VERSION;
    out->flags = FX_FLAG_INPLACE_OK;
    out->num_inputs  = 1;
    out->num_outputs = 1;
    out->num_params  = 6;
    out->param_names[0]    = "vowel_idx";
    out->param_names[1]    = "mod_rate_hz";
    out->param_names[2]    = "mod_depth";
    out->param_names[3]    = "Q1";
    out->param_names[4]    = "Q2";
    out->param_names[5]    = "mix";
    out->param_defaults[0] = 0.0f; // A
    out->param_defaults[1] = 0.6f;
    out->param_defaults[2] = 0.5f;
    out->param_defaults[3] = 5.0f;
    out->param_defaults[4] = 10.0f;
    out->param_defaults[5] = 1.0f;
    out->latency_samples = 0;
    return 1;
}

int formant_create(const FxDesc* desc, FxHandle **out_handle, FxVTable *out_vt,
                   uint32_t sample_rate, uint32_t max_block, uint32_t max_channels){
    (void)desc; (void)max_block;
    FxFormant* f=(FxFormant*)calloc(1,sizeof(FxFormant));
    if(!f) return 0;
    f->sr=(float)sample_rate; f->max_ch = max_channels?max_channels:2;
    f->vowel_idx=0; f->mod_rate_hz=0.6f; f->mod_depth=0.5f; f->Q1=5.f; f->Q2=10.f; f->mix=1.f;
    f->f1 = (Biquad*)calloc(f->max_ch, sizeof(Biquad));
    f->f2 = (Biquad*)calloc(f->max_ch, sizeof(Biquad));
    if(!f->f1||!f->f2){ free(f->f1); free(f->f2); free(f); return 0; }
    fm_reset((FxHandle*)f);
    out_vt->process = fm_process;
    out_vt->set_param = fm_set_param;
    out_vt->reset = fm_reset;
    out_vt->destroy = fm_destroy;
    *out_handle = (FxHandle*)f;
    return 1;
}
