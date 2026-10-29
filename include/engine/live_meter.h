#pragma once
#include <stdbool.h>
#include <stdint.h>
// Describes one exact 400 ms channel/stereo window before listening-only mono monitoring.
typedef struct EngineLiveMeter {
    bool available, valid, correlation_available, mono_available;
    int channels, rate;
    uint64_t sequence, start, end, latency, published_ns, full_scale[2], overload[2];
    double peak[2], rms[2], correlation, mid_rms, side_rms, mono_ratio, balance;
} EngineLiveMeter;
// Owns partition-invariant worker-only window accumulation without allocation or control pointers.
typedef struct EngineLiveMeterState {
    EngineLiveMeter result;
    uint64_t count, next, start, sequence, latency;
    int rate, channels;
    bool invalid;
    double power[2], peak[2], cross, mid, side;
    uint64_t full[2], over[2];
} EngineLiveMeterState;
// Accumulates exact disjoint sample windows and invalidates state on seek/rate/channel
// discontinuity.
void engine_live_meter_feed(EngineLiveMeterState*, const float*, int frames, int channels, int rate,
                            uint64_t start, uint64_t latency);
