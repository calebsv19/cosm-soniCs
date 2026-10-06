// Linked sample-peak limiter with integer-sample lookahead and prepared
// storage.
#include "effects/effects_api.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Holds the prepared delay ring, sliding peak maximum, and shared release
// envelope.
typedef struct FxLimiter {
    float sr, ceiling_db, release_ms, gain, reduction_db;
    unsigned channels, delay, capacity, write_pos, peak_head, peak_count;
    uint64_t frame;
    float* audio;
    float* peaks;
    uint64_t* peak_frames;
} FxLimiter;

// Bounds a finite control value to its supported interval.
static float clampf(float x, float low, float high) { return fminf(high, fmaxf(low, x)); }

// Clears captured audio and detector history without allocating.
static void limiter_reset(FxHandle* handle) {
    FxLimiter* l = (FxLimiter*)handle;
    memset(l->audio, 0, (size_t)l->capacity * l->channels * sizeof(float));
    l->write_pos = l->peak_head = l->peak_count = 0;
    l->frame = 0;
    l->gain = 1;
    l->reduction_db = 0;
}

// Applies finite controls; a changed integer delay starts a new silent history
// segment.
static void limiter_set_param(FxHandle* handle, uint32_t index, float value) {
    FxLimiter* l = (FxLimiter*)handle;
    if (!isfinite(value))
        return;
    if (index == 0)
        l->ceiling_db = clampf(value, -24, 0);
    if (index == 2)
        l->release_ms = clampf(value, 5, 200);
    if (index == 1) {
        unsigned delay = (unsigned)llround((double)clampf(value, 0, 3) * .001 * l->sr);
        if (delay >= l->capacity)
            delay = l->capacity - 1;
        if (delay != l->delay) {
            l->delay = delay;
            limiter_reset(handle);
        }
    }
}

// Releases all storage prepared by the factory.
static void limiter_destroy(FxHandle* handle) {
    FxLimiter* l = (FxLimiter*)handle;
    free(l->audio);
    free(l->peaks);
    free(l->peak_frames);
    free(l);
}

// Reports the active signal delay in project-rate samples.
static uint32_t limiter_latency(FxHandle* handle) { return ((FxLimiter*)handle)->delay; }

// Reports the maximum signal delay needed for host buffer preparation.
static uint32_t limiter_max_latency(FxHandle* handle) { return ((FxLimiter*)handle)->capacity - 1; }

// Reports the most negative applied gain in the last process call, excluding
// any makeup gain.
static float limiter_reduction(FxHandle* handle) { return ((FxLimiter*)handle)->reduction_db; }

// Delays audio by D samples and limits against the linked peak across the
// D+1-sample window.
static void limiter_process(FxHandle* handle, const float* in, float* out, int frames, int channels) {
    FxLimiter* l = (FxLimiter*)handle;
    if (!in || !out || frames <= 0 || channels <= 0 || (unsigned)channels > l->channels)
        return;
    float ceiling = powf(10, l->ceiling_db * .05f);
    float release = expf(-1 / (l->release_ms * .001f * l->sr));
    float min_gain = 1;
    for (int n = 0; n < frames; ++n) {
        float peak = 0;
        for (int ch = 0; ch < channels; ++ch) {
            float x = in[(size_t)n * channels + ch];
            if (!isfinite(x))
                x = 0;
            l->audio[(size_t)l->write_pos * l->channels + ch] = x;
            peak = fmaxf(peak, fabsf(x));
        }
        while (l->peak_count && l->frame - l->peak_frames[l->peak_head] > l->delay) {
            l->peak_head = (l->peak_head + 1) % l->capacity;
            --l->peak_count;
        }
        while (l->peak_count) {
            unsigned back = (l->peak_head + l->peak_count - 1) % l->capacity;
            if (l->peaks[back] > peak)
                break;
            --l->peak_count;
        }
        unsigned tail = (l->peak_head + l->peak_count) % l->capacity;
        l->peaks[tail] = peak;
        l->peak_frames[tail] = l->frame;
        ++l->peak_count;
        float maximum = l->peaks[l->peak_head];
        float needed = maximum > ceiling ? ceiling / maximum : 1;
        l->gain = needed < l->gain ? needed : release * l->gain + (1 - release) * needed;
        min_gain = fminf(min_gain, l->gain);
        unsigned read = (l->write_pos + l->capacity - l->delay) % l->capacity;
        for (int ch = 0; ch < channels; ++ch)
            out[(size_t)n * channels + ch] = l->audio[(size_t)read * l->channels + ch] * l->gain;
        l->write_pos = (l->write_pos + 1) % l->capacity;
        ++l->frame;
    }
    l->reduction_db = 20 * log10f(fmaxf(min_gain, 1e-20f));
}

// Describes the existing three controls and the optional dynamic-latency
// contract.
int limiter_get_desc(FxDesc* out) {
    if (!out)
        return 0;
    *out = (FxDesc){.name = "Limiter",
                    .api_version = FX_API_VERSION,
                    .flags = FX_FLAG_INPLACE_OK | FX_FLAG_DYNAMIC_LATENCY,
                    .num_inputs = 1,
                    .num_outputs = 1,
                    .num_params = 3,
                    .param_names = {"ceiling_dB", "lookahead_ms", "release_ms"},
                    .param_defaults = {-.3f, 1, 50}};
    return 1;
}

// Prepares maximum supported delay and detector storage before the instance can
// render.
int limiter_create(const FxDesc* desc, FxHandle** out, FxVTable* vt, uint32_t rate, uint32_t block,
                   uint32_t channels) {
    (void)desc;
    (void)block;
    if (!out || !vt || !rate)
        return 0;
    FxLimiter* l = calloc(1, sizeof(*l));
    if (!l)
        return 0;
    l->sr = rate;
    l->channels = channels ? channels : 2;
    l->capacity = (unsigned)llround((double)rate * .003) + 1;
    if ((size_t)l->capacity > SIZE_MAX / sizeof(float) / l->channels) {
        free(l);
        return 0;
    }
    l->audio = calloc((size_t)l->capacity * l->channels, sizeof(float));
    l->peaks = calloc(l->capacity, sizeof(float));
    l->peak_frames = calloc(l->capacity, sizeof(uint64_t));
    if (!l->audio || !l->peaks || !l->peak_frames) {
        limiter_destroy((FxHandle*)l);
        return 0;
    }
    l->ceiling_db = -.3f;
    l->release_ms = 50;
    l->delay = (unsigned)llround((double)rate * .001);
    limiter_reset((FxHandle*)l);
    *vt = (FxVTable){.process = limiter_process,
                     .set_param = limiter_set_param,
                     .reset = limiter_reset,
                     .destroy = limiter_destroy,
                     .latency = limiter_latency,
                     .max_latency = limiter_max_latency,
                     .gain_reduction_db = limiter_reduction};
    *out = (FxHandle*)l;
    return 1;
}
