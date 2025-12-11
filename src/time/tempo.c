#include "time/tempo.h"

#include <math.h>

#define TEMPO_MIN_BPM 20.0
#define TEMPO_MAX_BPM 300.0

TempoState tempo_state_default(double sample_rate) {
    TempoState t = {
        .bpm = 120.0,
        .ts_num = 4,
        .ts_den = 4,
        .sample_rate = sample_rate > 0.0 ? sample_rate : 44100.0
    };
    return t;
}

void tempo_state_clamp(TempoState* tempo) {
    if (!tempo) {
        return;
    }
    if (tempo->bpm < TEMPO_MIN_BPM) {
        tempo->bpm = TEMPO_MIN_BPM;
    } else if (tempo->bpm > TEMPO_MAX_BPM) {
        tempo->bpm = TEMPO_MAX_BPM;
    }
    if (tempo->ts_num <= 0) {
        tempo->ts_num = 4;
    }
    if (tempo->ts_den <= 0) {
        tempo->ts_den = 4;
    }
    // Only allow common simple denominators (power-of-two).
    int den = tempo->ts_den;
    int tmp = den;
    bool power_of_two = (tmp > 0) && ((tmp & (tmp - 1)) == 0);
    if (!power_of_two) {
        tempo->ts_den = 4;
    }
    if (tempo->sample_rate <= 0.0) {
        tempo->sample_rate = 44100.0;
    }
}

double tempo_seconds_to_beats(double seconds, const TempoState* tempo) {
    if (!tempo || tempo->bpm <= 0.0) {
        return 0.0;
    }
    double sec_per_beat = 60.0 / tempo->bpm;
    return seconds / sec_per_beat;
}

double tempo_beats_to_seconds(double beats, const TempoState* tempo) {
    if (!tempo || tempo->bpm <= 0.0) {
        return 0.0;
    }
    double sec_per_beat = 60.0 / tempo->bpm;
    return beats * sec_per_beat;
}

double tempo_seconds_to_samples(double seconds, const TempoState* tempo) {
    if (!tempo || tempo->sample_rate <= 0.0) {
        return 0.0;
    }
    return seconds * tempo->sample_rate;
}

double tempo_samples_to_seconds(int64_t samples, const TempoState* tempo) {
    if (!tempo || tempo->sample_rate <= 0.0) {
        return 0.0;
    }
    return (double)samples / tempo->sample_rate;
}

void time_context_populate(TimeContext* ctx, int64_t sample_pos, const TempoState* tempo) {
    if (!ctx || !tempo) {
        return;
    }
    TempoState clamped = *tempo;
    tempo_state_clamp(&clamped);
    ctx->tempo = clamped;
    ctx->sample_rate = clamped.sample_rate;
    ctx->sample_pos = sample_pos;
    ctx->time_seconds = clamped.sample_rate > 0.0 ? (double)sample_pos / clamped.sample_rate : 0.0;
    ctx->time_beats = tempo_seconds_to_beats(ctx->time_seconds, &clamped);
}
