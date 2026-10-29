#include "audio/audio_device.h"
#include <math.h>
// Crossfades sample-by-sample to dual-mono without gain boosts, allocation, locks or authored
// edits.
void audio_device_monitor_render(AudioDevice* d, float* pcm, int frames, int channels) {
    if (!d || !pcm || frames < 1 || channels != 2 || d->spec.sample_rate < 1)
        return;
    double target = atomic_load_explicit(&d->monitor_mono, memory_order_acquire) ? 1 : 0;
    if (!target && d->monitor_blend == 0)
        return;
    double step = 1.0 / fmax(1, round(d->spec.sample_rate * .005));
    for (int f = 0; f < frames; ++f) {
        if (d->monitor_blend < target)
            d->monitor_blend = fmin(target, d->monitor_blend + step);
        else if (d->monitor_blend > target)
            d->monitor_blend = fmax(target, d->monitor_blend - step);
        double l = pcm[f * 2], r = pcm[f * 2 + 1], mid = (l + r) * .5, b = d->monitor_blend;
        pcm[f * 2] = (float)(l + (mid - l) * b);
        pcm[f * 2 + 1] = (float)(r + (mid - r) * b);
    }
}
