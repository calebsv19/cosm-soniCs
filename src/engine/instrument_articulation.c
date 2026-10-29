#include "engine/instrument_articulation.h"
#include "engine/instrument_percussion.h"
#include <math.h>
#include <stdlib.h>

// Preserves the old storage on allocation failure.
bool engine_articulation_reserve(EngineArticulationTimeline* timeline, int capacity) {
    if (!timeline || capacity < 0 || capacity > ENGINE_MIDI_NOTE_CAP) return false;
    if (capacity <= timeline->capacity) return true;
    EngineArticulationEvent* events = realloc(timeline->events, (size_t)capacity * sizeof(*events));
    if (!events) return false;
    timeline->events = events;
    timeline->capacity = capacity;
    return true;
}

// Retires only this source's prepared transitions.
void engine_articulation_free(EngineArticulationTimeline* timeline) {
    free(timeline->events);
    *timeline = (EngineArticulationTimeline){0};
}

// Orders simultaneous notes by original index so the last authored note wins deterministically.
static int compare_events(const void* a, const void* b) {
    const EngineArticulationEvent* x = a;
    const EngineArticulationEvent* y = b;
    if (x->note.start_frame != y->note.start_frame) return x->note.start_frame < y->note.start_frame ? -1 : 1;
    return (x->note_index > y->note_index) - (x->note_index < y->note_index);
}

// Restores a max heap using only the timeline's reserved storage.
static void sift_events(EngineArticulationEvent* events, int root, int count) {
    while (root < count / 2) {
        int child = root * 2 + 1;
        if (child + 1 < count && compare_events(&events[child], &events[child + 1]) < 0) ++child;
        if (compare_events(&events[root], &events[child]) >= 0) break;
        EngineArticulationEvent value = events[root];
        events[root] = events[child];
        events[child] = value;
        root = child;
    }
}

// Sorts notes in O(n log n) with no allocator calls, including prepared live audition updates.
static void sort_events(EngineArticulationTimeline* timeline) {
    for (int i = timeline->count / 2; i > 0; --i) sift_events(timeline->events, i - 1, timeline->count);
    for (int end = timeline->count - 1; end > 0; --end) {
        EngineArticulationEvent value = timeline->events[0];
        timeline->events[0] = timeline->events[end];
        timeline->events[end] = value;
        sift_events(timeline->events, 0, end);
    }
}

// Integrates an exponential stably, including equal-pitch and zero-glide limits.
static double exponential_integral(double exponent, double seconds) {
    return fabs(exponent) < 1e-10 ? seconds : expm1(exponent * seconds) / exponent;
}

// Integrates the logarithmic glide multiplied by the independently decaying pitch-drop envelope.
static double segment_cycles(const EngineArticulationEvent* e, double age, double seconds) {
    double moving = fmin(seconds, e->glide_seconds);
    double beta = e->glide_seconds > 0 ? log(e->target_hz / e->start_hz) / e->glide_seconds : 0;
    double amount = e->pitch_amount * exp(-age / e->pitch_tau);
    double cycles = 0;
    if (moving > 0) cycles = e->start_hz * (exponential_integral(beta, moving) +
        amount * exponential_integral(beta - 1 / e->pitch_tau, moving));
    if (seconds > moving) cycles += e->target_hz * ((seconds - moving) +
        amount * exp(-moving / e->pitch_tau) * exponential_integral(-1 / e->pitch_tau, seconds - moving));
    return cycles;
}

// Evaluates the prepared glide's pitch and integrated phase at an arbitrary seek position.
void engine_articulation_pitch(const EngineArticulationEvent* e, int rate, uint64_t frame,
    double* frequency, double* cycles) {
    double t = (double)(frame - e->note.start_frame) / rate;
    double age = (double)(e->note.start_frame - e->chain_start) / rate;
    double fraction = e->glide_seconds > 0 ? fmin(1, t / e->glide_seconds) : 1;
    double base = e->start_hz * exp(log(e->target_hz / e->start_hz) * fraction);
    *frequency = base * (1 + e->pitch_amount * exp(-(age + t) / e->pitch_tau));
    *cycles = e->cycles + segment_cycles(e, age, t);
}

// Reuses reserved storage while preparing phase continuity and onset-latched physical controls.
void engine_articulation_retime(EngineArticulationTimeline* timeline, int rate,
    EngineArticulationParamsAt params_at, void* context) {
    for (int i = 0; i < timeline->count; ++i) {
        EngineArticulationEvent* e = &timeline->events[i];
        EngineInstrumentParams params = params_at(context, e->note.start_frame);
        e->chain_start = e->note.start_frame;
        e->target_hz = 440 * pow(2, ((double)e->note.note - 69) / 12);
        e->start_hz = e->target_hz;
        e->cycles = 0;
        e->glide_seconds = 0;
        e->pitch_semitones = params.pitch_env_semitones;
        e->pitch_decay_ms = params.pitch_decay_ms;
        e->pitch_amount = exp2(params.pitch_env_semitones / 12.0) - 1;
        e->pitch_tau = fmax(.001, params.pitch_decay_ms * .001);
        e->previous_velocity = e->note.velocity;
        e->legato = false;
        if (i == 0) continue;
        const EngineArticulationEvent* previous = &timeline->events[i - 1];
        uint64_t elapsed = e->note.start_frame - previous->note.start_frame;
        if (elapsed == 0 || elapsed >= previous->note.duration_frames) continue;
        e->legato = true;
        e->chain_start = previous->chain_start;
        double frequency;
        engine_articulation_pitch(previous, rate, e->note.start_frame, &frequency, &e->cycles);
        double age = (double)(e->note.start_frame - e->chain_start) / rate;
        e->pitch_semitones = previous->pitch_semitones;
        e->pitch_decay_ms = previous->pitch_decay_ms;
        e->pitch_amount = previous->pitch_amount;
        e->pitch_tau = previous->pitch_tau;
        e->start_hz = frequency / (1 + e->pitch_amount * exp(-age / e->pitch_tau));
        e->glide_seconds = params.glide_ms * .001;
        double velocity_fraction = previous->legato ? fmin(1, (double)elapsed / fmax(1, rate * .005)) : 1;
        e->previous_velocity = previous->previous_velocity + (float)velocity_fraction * (previous->note.velocity - previous->previous_velocity);
    }
}

// Copies only valid triggers and sorts them on the preparation thread.
void engine_articulation_prepare(EngineArticulationTimeline* timeline, const EngineMidiNote* notes,
    int count, int rate, EngineArticulationParamsAt params_at, void* context) {
    timeline->count = 0;
    for (int i = 0; i < count; ++i) {
        if (!notes[i].duration_frames) continue;
        timeline->events[timeline->count++] = (EngineArticulationEvent){.note = notes[i], .note_index = i};
    }
    if (timeline->count > 1) sort_events(timeline);
    engine_articulation_retime(timeline, rate, params_at, context);
}

// Finds a transition in logarithmic time without scanning historical notes in the sample loop.
const EngineArticulationEvent* engine_articulation_event_at(const EngineArticulationTimeline* timeline, uint64_t frame) {
    int lo = 0, hi = timeline->count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (timeline->events[mid].note.start_frame <= frame) lo = mid + 1;
        else hi = mid;
    }
    return lo ? &timeline->events[lo - 1] : NULL;
}

// Keeps oscillator phase and the body envelope through legato transitions, with a short velocity ramp.
float engine_articulation_808_sample(const EngineArticulationTimeline* timeline,
    EngineInstrumentPresetId preset, EngineInstrumentParams params, int rate, uint64_t frame) {
    const EngineArticulationEvent* e = engine_articulation_event_at(timeline, frame);
    if (!e) return 0;
    uint64_t age = frame - e->chain_start;
    uint64_t lead = e->note.start_frame - e->chain_start;
    uint64_t gate = e->note.duration_frames > UINT64_MAX - lead ? UINT64_MAX : lead + e->note.duration_frames;
    double fade = 1;
    int next = (int)(e - timeline->events) + 1;
    if (next < timeline->count && !timeline->events[next].legato)
        fade = fmin(1, (double)(timeline->events[next].note.start_frame - frame) / fmax(1, rate * .002));
    params.pitch_env_semitones = e->pitch_semitones;
    params.pitch_decay_ms = e->pitch_decay_ms;
    if (!e->legato) return (float)(fade * engine_instrument_percussion_sample(preset, params, e->target_hz, rate, age, gate) * params.level * e->note.velocity);
    double frequency, cycles;
    engine_articulation_pitch(e, rate, frame, &frequency, &cycles);
    float ramp = fminf(1, (float)(frame - e->note.start_frame) / fmaxf(1, rate * .005f));
    float velocity = e->previous_velocity + ramp * (e->note.velocity - e->previous_velocity);
    return engine_instrument_percussion_sample_phase(preset, params, frequency,
        6.28318530717958647692 * cycles, rate, age, gate) * params.level * velocity * (float)fade;
}
