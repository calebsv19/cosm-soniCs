#include "time/tempo.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TEMPO_MIN_BPM 20.0
#define TEMPO_MAX_BPM 300.0
#define TIME_SIGNATURE_MIN_VALUE 1
#define TIME_SIGNATURE_MAX_VALUE 64

// Returns the default tempo event at beat zero.
static TempoEvent tempo_default_event(void) {
    TempoEvent evt = {.beat = 0.0, .bpm = 120.0};
    return evt;
}

// Returns the default time signature event at beat zero.
static TimeSignatureEvent time_signature_default_event(void) {
    TimeSignatureEvent evt = {.beat = 0.0, .ts_num = 4, .ts_den = 4};
    return evt;
}

// Clamps a time signature to supported values.
static void clamp_time_signature(int* ts_num, int* ts_den) {
    int num = ts_num ? *ts_num : 4;
    int den = ts_den ? *ts_den : 4;
    if (num < TIME_SIGNATURE_MIN_VALUE) num = TIME_SIGNATURE_MIN_VALUE;
    if (num > TIME_SIGNATURE_MAX_VALUE) num = TIME_SIGNATURE_MAX_VALUE;
    if (den < TIME_SIGNATURE_MIN_VALUE) den = TIME_SIGNATURE_MIN_VALUE;
    if (den > TIME_SIGNATURE_MAX_VALUE) den = TIME_SIGNATURE_MAX_VALUE;
    if (ts_num) {
        *ts_num = num;
    }
    if (ts_den) {
        *ts_den = den;
    }
}

// Clamps BPM to a supported range.
static double clamp_bpm(double bpm) {
    if (bpm < TEMPO_MIN_BPM) {
        return TEMPO_MIN_BPM;
    }
    if (bpm > TEMPO_MAX_BPM) {
        return TEMPO_MAX_BPM;
    }
    return bpm;
}

// Sorts tempo events by beat position.
static int compare_tempo_event(const void* a, const void* b) {
    const TempoEvent* lhs = (const TempoEvent*)a;
    const TempoEvent* rhs = (const TempoEvent*)b;
    if (lhs->beat < rhs->beat) return -1;
    if (lhs->beat > rhs->beat) return 1;
    return 0;
}

// Sorts time signature events by beat position.
static int compare_time_signature_event(const void* a, const void* b) {
    const TimeSignatureEvent* lhs = (const TimeSignatureEvent*)a;
    const TimeSignatureEvent* rhs = (const TimeSignatureEvent*)b;
    if (lhs->beat < rhs->beat) return -1;
    if (lhs->beat > rhs->beat) return 1;
    return 0;
}

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
    clamp_time_signature(&tempo->ts_num, &tempo->ts_den);
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

void tempo_map_init(TempoMap* map, double sample_rate) {
    if (!map) {
        return;
    }
    memset(map, 0, sizeof(*map));
    map->sample_rate = sample_rate > 0.0 ? sample_rate : 44100.0;
    map->events = (TempoEvent*)calloc(1, sizeof(TempoEvent));
    if (map->events) {
        map->event_capacity = 1;
        map->event_count = 1;
        map->events[0] = tempo_default_event();
        map->events[0].bpm = clamp_bpm(map->events[0].bpm);
    }
}

void tempo_map_free(TempoMap* map) {
    if (!map) {
        return;
    }
    free(map->events);
    map->events = NULL;
    map->event_count = 0;
    map->event_capacity = 0;
    map->sample_rate = 0.0;
}

// Ensures the tempo map storage can hold the requested number of events.
static bool tempo_map_ensure_capacity(TempoMap* map, int needed) {
    if (!map) {
        return false;
    }
    if (needed <= map->event_capacity) {
        return true;
    }
    int next = map->event_capacity > 0 ? map->event_capacity : 1;
    while (next < needed) {
        next *= 2;
    }
    TempoEvent* resized = (TempoEvent*)realloc(map->events, (size_t)next * sizeof(TempoEvent));
    if (!resized) {
        return false;
    }
    map->events = resized;
    map->event_capacity = next;
    return true;
}

// Sorts tempo events and ensures there is a beat-zero entry.
static void tempo_map_sort_and_normalize(TempoMap* map) {
    if (!map || map->event_count <= 0 || !map->events) {
        return;
    }
    qsort(map->events, (size_t)map->event_count, sizeof(TempoEvent), compare_tempo_event);
    for (int i = 0; i < map->event_count; ++i) {
        map->events[i].bpm = clamp_bpm(map->events[i].bpm);
        if (map->events[i].beat < 0.0) {
            map->events[i].beat = 0.0;
        }
    }
    if (map->events[0].beat > 0.0) {
        if (tempo_map_ensure_capacity(map, map->event_count + 1)) {
            memmove(&map->events[1], &map->events[0], (size_t)map->event_count * sizeof(TempoEvent));
            map->events[0].beat = 0.0;
            map->events[0].bpm = map->events[1].bpm;
            map->event_count += 1;
        }
    }
}

bool tempo_map_set_events(TempoMap* map, const TempoEvent* events, int count) {
    if (!map) {
        return false;
    }
    map->event_count = 0;
    if (!events || count <= 0) {
        tempo_map_sort_and_normalize(map);
        if (map->event_count == 0 && tempo_map_ensure_capacity(map, 1)) {
            map->events[0] = tempo_default_event();
            map->event_count = 1;
        }
        return true;
    }
    if (!tempo_map_ensure_capacity(map, count)) {
        return false;
    }
    memcpy(map->events, events, (size_t)count * sizeof(TempoEvent));
    map->event_count = count;
    tempo_map_sort_and_normalize(map);
    return true;
}

bool tempo_map_upsert_event(TempoMap* map, double beat, double bpm) {
    if (!map) {
        return false;
    }
    if (beat < 0.0) {
        beat = 0.0;
    }
    bpm = clamp_bpm(bpm);
    const double epsilon = 1e-6;
    for (int i = 0; i < map->event_count; ++i) {
        if (fabs(map->events[i].beat - beat) <= epsilon) {
            map->events[i].bpm = bpm;
            tempo_map_sort_and_normalize(map);
            return true;
        }
    }
    if (!tempo_map_ensure_capacity(map, map->event_count + 1)) {
        return false;
    }
    map->events[map->event_count] = (TempoEvent){.beat = beat, .bpm = bpm};
    map->event_count += 1;
    tempo_map_sort_and_normalize(map);
    return true;
}

// Update a tempo event in-place and keep the map sorted.
bool tempo_map_update_event(TempoMap* map, int index, double beat, double bpm) {
    if (!map || !map->events || index < 0 || index >= map->event_count) {
        return false;
    }
    if (beat < 0.0) {
        beat = 0.0;
    }
    map->events[index].beat = beat;
    map->events[index].bpm = clamp_bpm(bpm);
    tempo_map_sort_and_normalize(map);
    return true;
}

const TempoEvent* tempo_map_event_at_beat(const TempoMap* map, double beat) {
    if (!map || !map->events || map->event_count <= 0) {
        return NULL;
    }
    const TempoEvent* active = &map->events[0];
    for (int i = 0; i < map->event_count; ++i) {
        if (map->events[i].beat <= beat) {
            active = &map->events[i];
        } else {
            break;
        }
    }
    return active;
}

double tempo_map_beats_to_seconds(const TempoMap* map, double beats) {
    if (!map || map->event_count <= 0 || !map->events) {
        TempoState tempo = tempo_state_default(44100.0);
        return tempo_beats_to_seconds(beats, &tempo);
    }
    if (beats <= 0.0) {
        return 0.0;
    }
    double seconds = 0.0;
    for (int i = 0; i < map->event_count; ++i) {
        const TempoEvent* evt = &map->events[i];
        double seg_start = evt->beat;
        double seg_end = (i + 1 < map->event_count) ? map->events[i + 1].beat : beats;
        if (beats <= seg_start) {
            break;
        }
        if (seg_end > beats) {
            seg_end = beats;
        }
        if (seg_end <= seg_start) {
            continue;
        }
        double sec_per_beat = 60.0 / clamp_bpm(evt->bpm);
        seconds += (seg_end - seg_start) * sec_per_beat;
        if (seg_end >= beats) {
            break;
        }
    }
    return seconds;
}

double tempo_map_seconds_to_beats(const TempoMap* map, double seconds) {
    if (!map || map->event_count <= 0 || !map->events) {
        TempoState tempo = tempo_state_default(44100.0);
        return tempo_seconds_to_beats(seconds, &tempo);
    }
    if (seconds <= 0.0) {
        return 0.0;
    }
    double beats = 0.0;
    double remaining = seconds;
    for (int i = 0; i < map->event_count; ++i) {
        const TempoEvent* evt = &map->events[i];
        double seg_start = evt->beat;
        double seg_end = (i + 1 < map->event_count) ? map->events[i + 1].beat : 0.0;
        double sec_per_beat = 60.0 / clamp_bpm(evt->bpm);
        if (i + 1 < map->event_count) {
            double seg_beats = seg_end - seg_start;
            double seg_seconds = seg_beats * sec_per_beat;
            if (remaining <= seg_seconds) {
                beats = seg_start + (remaining / sec_per_beat);
                return beats;
            }
            remaining -= seg_seconds;
            beats = seg_end;
        } else {
            beats = seg_start + (remaining / sec_per_beat);
            return beats;
        }
    }
    return beats;
}

double tempo_map_beats_to_samples(const TempoMap* map, double beats) {
    double seconds = tempo_map_beats_to_seconds(map, beats);
    double sample_rate = map && map->sample_rate > 0.0 ? map->sample_rate : 44100.0;
    return seconds * sample_rate;
}

double tempo_map_samples_to_beats(const TempoMap* map, int64_t samples) {
    if (!map) {
        TempoState tempo = tempo_state_default(44100.0);
        double seconds = tempo_samples_to_seconds(samples, &tempo);
        return tempo_seconds_to_beats(seconds, &tempo);
    }
    double sample_rate = map->sample_rate > 0.0 ? map->sample_rate : 44100.0;
    double seconds = sample_rate > 0.0 ? (double)samples / sample_rate : 0.0;
    return tempo_map_seconds_to_beats(map, seconds);
}

void time_signature_map_init(TimeSignatureMap* map) {
    if (!map) {
        return;
    }
    memset(map, 0, sizeof(*map));
    map->events = (TimeSignatureEvent*)calloc(1, sizeof(TimeSignatureEvent));
    if (map->events) {
        map->event_capacity = 1;
        map->event_count = 1;
        map->events[0] = time_signature_default_event();
        clamp_time_signature(&map->events[0].ts_num, &map->events[0].ts_den);
    }
}

void time_signature_map_free(TimeSignatureMap* map) {
    if (!map) {
        return;
    }
    free(map->events);
    map->events = NULL;
    map->event_count = 0;
    map->event_capacity = 0;
}

// Ensures the time signature map storage can hold the requested number of events.
static bool time_signature_map_ensure_capacity(TimeSignatureMap* map, int needed) {
    if (!map) {
        return false;
    }
    if (needed <= map->event_capacity) {
        return true;
    }
    int next = map->event_capacity > 0 ? map->event_capacity : 1;
    while (next < needed) {
        next *= 2;
    }
    TimeSignatureEvent* resized = (TimeSignatureEvent*)realloc(map->events,
                                                               (size_t)next * sizeof(TimeSignatureEvent));
    if (!resized) {
        return false;
    }
    map->events = resized;
    map->event_capacity = next;
    return true;
}

// Sorts time signature events and ensures there is a beat-zero entry.
static void time_signature_map_sort_and_normalize(TimeSignatureMap* map) {
    if (!map || map->event_count <= 0 || !map->events) {
        return;
    }
    qsort(map->events, (size_t)map->event_count, sizeof(TimeSignatureEvent), compare_time_signature_event);
    for (int i = 0; i < map->event_count; ++i) {
        if (map->events[i].beat < 0.0) {
            map->events[i].beat = 0.0;
        }
        clamp_time_signature(&map->events[i].ts_num, &map->events[i].ts_den);
    }
    if (map->events[0].beat > 0.0) {
        if (time_signature_map_ensure_capacity(map, map->event_count + 1)) {
            memmove(&map->events[1], &map->events[0], (size_t)map->event_count * sizeof(TimeSignatureEvent));
            map->events[0].beat = 0.0;
            map->events[0].ts_num = map->events[1].ts_num;
            map->events[0].ts_den = map->events[1].ts_den;
            map->event_count += 1;
        }
    }
}

bool time_signature_map_set_events(TimeSignatureMap* map, const TimeSignatureEvent* events, int count) {
    if (!map) {
        return false;
    }
    map->event_count = 0;
    if (!events || count <= 0) {
        time_signature_map_sort_and_normalize(map);
        if (map->event_count == 0 && time_signature_map_ensure_capacity(map, 1)) {
            map->events[0] = time_signature_default_event();
            map->event_count = 1;
        }
        return true;
    }
    if (!time_signature_map_ensure_capacity(map, count)) {
        return false;
    }
    memcpy(map->events, events, (size_t)count * sizeof(TimeSignatureEvent));
    map->event_count = count;
    time_signature_map_sort_and_normalize(map);
    return true;
}

bool time_signature_map_upsert_event(TimeSignatureMap* map, double beat, int ts_num, int ts_den) {
    if (!map) {
        return false;
    }
    if (beat < 0.0) {
        beat = 0.0;
    }
    clamp_time_signature(&ts_num, &ts_den);
    const double epsilon = 1e-6;
    for (int i = 0; i < map->event_count; ++i) {
        if (fabs(map->events[i].beat - beat) <= epsilon) {
            map->events[i].ts_num = ts_num;
            map->events[i].ts_den = ts_den;
            time_signature_map_sort_and_normalize(map);
            return true;
        }
    }
    if (!time_signature_map_ensure_capacity(map, map->event_count + 1)) {
        return false;
    }
    map->events[map->event_count] = (TimeSignatureEvent){.beat = beat, .ts_num = ts_num, .ts_den = ts_den};
    map->event_count += 1;
    time_signature_map_sort_and_normalize(map);
    return true;
}

const TimeSignatureEvent* time_signature_map_event_at_beat(const TimeSignatureMap* map, double beat) {
    if (!map || !map->events || map->event_count <= 0) {
        return NULL;
    }
    const TimeSignatureEvent* active = &map->events[0];
    for (int i = 0; i < map->event_count; ++i) {
        if (map->events[i].beat <= beat) {
            active = &map->events[i];
        } else {
            break;
        }
    }
    return active;
}

// Returns the beat unit length (in beat space) for a time signature.
double time_signature_beat_unit(const TimeSignatureEvent* sig) {
    int den = sig ? sig->ts_den : 4;
    if (den <= 0) {
        den = 4;
    }
    return 4.0 / (double)den;
}

// Returns the beats-per-bar length (in beat space) for a time signature.
double time_signature_beats_per_bar(const TimeSignatureEvent* sig) {
    int num = sig ? sig->ts_num : 4;
    if (num <= 0) {
        num = 4;
    }
    return (double)num * time_signature_beat_unit(sig);
}

void time_signature_map_beat_to_bar_beat(const TimeSignatureMap* map,
                                         double beat,
                                         int* out_bar,
                                         int* out_beat,
                                         double* out_sub_beat,
                                         int* out_ts_num,
                                         int* out_ts_den) {
    if (out_bar) *out_bar = 1;
    if (out_beat) *out_beat = 1;
    if (out_sub_beat) *out_sub_beat = 0.0;
    if (out_ts_num) *out_ts_num = 4;
    if (out_ts_den) *out_ts_den = 4;
    if (!map || !map->events || map->event_count <= 0) {
        return;
    }
    if (beat < 0.0) {
        beat = 0.0;
    }

    int total_bars = 0;
    double segment_start = 0.0;
    const TimeSignatureEvent* current = &map->events[0];
    for (int i = 0; i < map->event_count; ++i) {
        if (map->events[i].beat <= beat) {
            current = &map->events[i];
        } else {
            break;
        }
    }

    for (int i = 0; i < map->event_count; ++i) {
        const TimeSignatureEvent* evt = &map->events[i];
        double seg_end = (i + 1 < map->event_count) ? map->events[i + 1].beat : beat;
        if (seg_end <= segment_start) {
            continue;
        }
        double beats_per_bar = time_signature_beats_per_bar(evt);
        if (beats_per_bar <= 0.0) {
            beats_per_bar = 4.0;
        }
        if (beat < seg_end || i + 1 == map->event_count) {
            double seg_offset = beat - segment_start;
            int bar_offset = (beats_per_bar > 0.0) ? (int)floor(seg_offset / beats_per_bar) : 0;
            double beat_in_bar = seg_offset - (double)bar_offset * beats_per_bar;
            if (out_bar) *out_bar = total_bars + bar_offset + 1;
            double beat_unit = time_signature_beat_unit(evt);
            if (beat_unit <= 0.0) {
                beat_unit = 1.0;
            }
            double beat_pos = beat_in_bar / beat_unit;
            int beat_index = (int)floor(beat_pos);
            if (out_beat) *out_beat = beat_index + 1;
            if (out_sub_beat) *out_sub_beat = beat_pos - floor(beat_pos);
            if (out_ts_num) *out_ts_num = current->ts_num;
            if (out_ts_den) *out_ts_den = current->ts_den;
            return;
        }
        int bars_in_segment = (beats_per_bar > 0.0) ? (int)floor((seg_end - segment_start) / beats_per_bar) : 0;
        total_bars += bars_in_segment;
        segment_start = seg_end;
    }
}
