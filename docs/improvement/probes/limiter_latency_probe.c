#include "effects/effects_api.h"
#include <stdio.h>
#include <math.h>

int limiter_get_desc(FxDesc* out);
int limiter_create(const FxDesc*, FxHandle**, FxVTable*, uint32_t, uint32_t, uint32_t);

// Measures the existing limiter's sample delay without asserting a proposed replacement contract.
int main(void) {
    const float settings[] = {0, 1, 3};
    for (unsigned i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        FxDesc descriptor = {0};
        FxVTable methods = {0};
        FxHandle* effect = NULL;
        if (!limiter_get_desc(&descriptor) || !limiter_create(&descriptor, &effect, &methods, 48000, 512, 2)) return 1;
        methods.set_param(effect, 1, settings[i]);
        methods.reset(effect);
        float samples[1024] = {0};
        samples[0] = samples[1] = 0.25f;
        methods.process(effect, samples, samples, 512, 2);
        int first = -1;
        for (int frame = 0; frame < 512; ++frame) {
            if (fabsf(samples[frame * 2]) > 0.001f) { first = frame; break; }
        }
        printf("lookahead_ms=%.0f requested_frames=%.0f observed_delay=%d descriptor_latency=%u dynamic_latency=%u\n",
               settings[i], settings[i] * 48, first, descriptor.latency_samples, methods.latency ? methods.latency(effect) : descriptor.latency_samples);
        methods.destroy(effect);
    }
    return 0;
}
