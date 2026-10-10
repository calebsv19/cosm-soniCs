#include "engine/instrument_filter.h"
#include <math.h>
#include <string.h>

// Selects filtered voices while leaving every existing preset on its original renderer.
bool engine_instrument_filter_preset(EngineInstrumentPresetId preset) {
    return preset == ENGINE_INSTRUMENT_PRESET_FILTERED_KEYS || preset == ENGINE_INSTRUMENT_PRESET_FILTERED_PAD ||
        preset == ENGINE_INSTRUMENT_PRESET_SWEEP_BASS || preset == ENGINE_INSTRUMENT_PRESET_FILTER_PLUCK || preset == ENGINE_INSTRUMENT_PRESET_MOVING_PAD;
}

// Clears state on source preparation, format reset, note onset, or transport discontinuity.
void engine_instrument_filter_reset(EngineInstrumentFilter* filter) {
    if (filter) memset(filter, 0, sizeof(*filter));
}

// Evaluates a topology-preserving state-variable low-pass with five-millisecond parameter smoothing.
float engine_instrument_filter_process(EngineInstrumentFilter* f, float sample,
    EngineInstrumentParams p, int rate, uint64_t frame) {
    if (!f || rate <= 0 || !isfinite(sample)) return 0;
    if (!f->active || f->next_frame != frame) engine_instrument_filter_reset(f);
    // Note-relative phase retriggers every voice; zero depths preserve the static filter exactly.
    if (p.filter_env_octaves != 0 || p.filter_lfo_octaves != 0) {
        double seconds = (double)frame / rate;
        double envelope = p.filter_env_octaves == 0 ? 0 : p.filter_env_octaves * exp(-seconds / (p.filter_decay_ms * .001));
        double lfo = p.filter_lfo_octaves == 0 ? 0 : p.filter_lfo_octaves * sin(6.28318530717958647692 * p.filter_lfo_hz * seconds);
        p.cutoff_hz = (float)fmin(fmax(p.cutoff_hz * exp2(envelope + lfo), 80), rate * .45);
    }
    if (!f->active || f->cutoff != p.cutoff_hz || f->resonance != p.resonance_q) {
        double cutoff = fmin(fmax(p.cutoff_hz, 80), rate * .45);
        double q = fmin(fmax(p.resonance_q, .5), 2);
        f->target_g = tan(3.14159265358979323846 * cutoff / rate);
        f->target_k = 1 / q;
        f->cutoff = p.cutoff_hz; f->resonance = p.resonance_q;
    }
    if (!f->active) {
        f->g = f->target_g; f->k = f->target_k;
        f->smoothing = -expm1(-1.0 / (.005 * rate));
        f->active = true;
    } else {
        f->g += (f->target_g - f->g) * f->smoothing;
        f->k += (f->target_k - f->k) * f->smoothing;
    }
    double a1 = 1 / (1 + f->g * (f->g + f->k));
    double a2 = f->g * a1, a3 = f->g * a2;
    double v3 = sample - f->integrator2;
    double v1 = a1 * f->integrator1 + a2 * v3;
    double v2 = f->integrator2 + a2 * f->integrator1 + a3 * v3;
    f->integrator1 = 2 * v1 - f->integrator1;
    f->integrator2 = 2 * v2 - f->integrator2;
    f->next_frame = frame + 1;
    if (!isfinite(v2)) { engine_instrument_filter_reset(f); return 0; }
    return (float)v2;
}
