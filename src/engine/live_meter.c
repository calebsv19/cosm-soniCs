#include "engine/live_meter.h"
#include "core_time.h"
#include <string.h>
#include <math.h>
// Resets only partial sums after publishing an immutable measurement window.
static void clear_window(EngineLiveMeterState* s) {
    s->count = 0;
    s->invalid = false;
    memset(s->power, 0, sizeof(s->power));
    memset(s->peak, 0, sizeof(s->peak));
    s->cross = s->mid = s->side = 0;
    memset(s->full, 0, sizeof(s->full));
    memset(s->over, 0, sizeof(s->over));
}
// Streams all-channel energy without stereo cancellation or block-size-dependent decay.
void engine_live_meter_feed(EngineLiveMeterState* s, const float* pcm, int frames, int channels,
                            int rate, uint64_t start, uint64_t latency) {
    if (!s)
        return;
    if (!pcm || frames < 1 || rate < 10 || channels < 1 || channels > 2) {
        memset(s, 0, sizeof(*s));
        return;
    }
    if (s->rate != rate || s->channels != channels || s->next != start || s->latency != latency) {
        memset(s, 0, sizeof(*s));
        s->rate = rate;
        s->channels = channels;
        s->latency = latency;
    }
    uint64_t length = (uint64_t)rate * 2 / 5;
    for (int f = 0; f < frames; ++f) {
        if (!s->count)
            s->start = start + f;
        double l = pcm[f * channels], r = channels == 2 ? pcm[f * channels + 1] : l;
        if (!isfinite(l) || !isfinite(r)) {
            s->invalid = true;
            l = r = 0;
        }
        double pair[2] = {l, r};
        for (int ch = 0; ch < channels; ++ch) {
            double a = fabs(pair[ch]);
            s->power[ch] += pair[ch] * pair[ch];
            if (a > s->peak[ch])
                s->peak[ch] = a;
            if (a >= 1)
                ++s->full[ch];
            if (a > 1)
                ++s->over[ch];
        }
        s->cross += l * r;
        double mid = (l + r) * .5, side = (l - r) * .5;
        s->mid += mid * mid;
        s->side += side * side;
        ++s->count;
        if (s->count == length) {
            EngineLiveMeter v = {.available = true,
                                 .valid = !s->invalid,
                                 .channels = channels,
                                 .rate = rate,
                                 .sequence = ++s->sequence,
                                 .start = s->start,
                                 .end = start + f + 1,
                                 .latency = latency,
                                 .published_ns = core_time_now_ns()};
            double n = s->count;
            for (int ch = 0; ch < channels; ++ch) {
                v.peak[ch] = s->peak[ch];
                v.rms[ch] = sqrt(s->power[ch] / n);
                v.full_scale[ch] = s->full[ch];
                v.overload[ch] = s->over[ch];
            }
            v.mid_rms = sqrt(s->mid / n);
            v.side_rms = sqrt(s->side / n);
            double energy = channels == 2 ? (s->power[0] + s->power[1]) * .5 : s->power[0];
            v.mono_available = v.valid && energy > 1e-20;
            v.mono_ratio = v.mono_available ? sqrt(s->mid / energy) : 0;
            v.balance =
                channels == 2 && energy > 1e-20 ? (s->power[1] - s->power[0]) / (2 * energy) : 0;
            double denom = channels == 2 ? sqrt(s->power[0] * s->power[1]) : 0;
            v.correlation_available = v.valid && channels == 2 && denom > 1e-20;
            v.correlation = v.correlation_available ? fmax(-1, fmin(1, s->cross / denom)) : 0;
            s->result = v;
            clear_window(s);
        }
    }
    s->next = start + frames;
}
