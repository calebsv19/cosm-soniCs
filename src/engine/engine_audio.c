#include "engine/engine_internal.h"

#include "engine/graph.h"
#include "engine/instrument.h"
#include "core_time.h"

#include <math.h>
#include <string.h>

static inline float sanitize_sample(float v) {
    if (!isfinite(v) || fabsf(v) > 64.0f) {
        return 0.0f;
    }
    if (fabsf(v) < 1e-12f) {
        return 0.0f;
    }
    return v;
}

static void compute_peak_rms(const float* buffer, int frames, int channels, float* out_peak, float* out_rms) {
    if (!buffer || frames <= 0 || channels <= 0 || !out_peak || !out_rms) {
        return;
    }
    double sum = 0.0;
    float peak = 0.0f;
    int count = frames * channels;
    for (int i = 0; i < count; ++i) {
        float v = buffer[i];
        float a = fabsf(v);
        if (a > peak) {
            peak = a;
        }
        sum += (double)v * (double)v;
    }
    *out_peak = peak;
    *out_rms = count > 0 ? (float)sqrt(sum / (double)count) : 0.0f;
}

static void update_meter_state(EngineMeterState* state, float peak, float rms, int hold_blocks) {
    if (!state) {
        return;
    }
    const float decay = 0.90f;
    if (peak > state->peak) {
        state->peak = peak;
    } else {
        state->peak *= decay;
    }
    if (rms > state->rms) {
        state->rms = rms;
    } else {
        state->rms *= decay;
    }
    if (peak > 1.0f) {
        state->clip_hold = hold_blocks;
    } else if (state->clip_hold > 0) {
        state->clip_hold -= 1;
    }
}

// Applies the existing balance law with a worker-owned sample ramp for live control changes.
static void apply_track_pan(FxSampleRamp* pan_ramp, float* buffer, int frames, int channels) {
    if (!pan_ramp || !buffer || frames <= 0 || channels < 2) return;
    for (int frame = 0; frame < frames; ++frame) {
        float pan = fminf(1, fmaxf(-1, fx_sample_ramp_next(pan_ramp)));
        buffer[frame * channels] *= pan > 0 ? 1 - pan : 1;
        buffer[frame * channels + 1] *= pan < 0 ? 1 + pan : 1;
    }
}

void engine_sanitize_block(float* buf, size_t samples) {
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < samples; ++i) {
        buf[i] = sanitize_sample(buf[i]);
    }
}

void engine_audio_callback(float* output, int frames, int channels, void* userdata) {
    Engine* engine = (Engine*)userdata;
    if (!output || frames <= 0 || !engine) {
        return;
    }
    atomic_fetch_add_explicit(&engine->diag_callback_count, 1, memory_order_relaxed);
    atomic_fetch_add(&engine->clock_callback_seq, 1);
    atomic_store(&engine->clock_callback_ns, core_time_now_ns());
    atomic_store(&engine->clock_callback_start, atomic_load(&engine->clock_consumed));
    size_t grabbed = audio_queue_read(&engine->output_queue, output, (size_t)frames);
    atomic_store(&engine->clock_callback_frames, grabbed);
    uint64_t consumed = atomic_fetch_add(&engine->clock_consumed, grabbed) + grabbed;
    atomic_store(&engine->clock_fallback_frame, engine_clock_advance(atomic_load(&engine->clock_origin),
        atomic_load(&engine->clock_advancing) ? consumed : 0,
        atomic_load(&engine->clock_loop_start), atomic_load(&engine->clock_loop_end)));
    atomic_fetch_add(&engine->clock_callback_seq, 1);
    if (grabbed < (size_t)frames) {
        size_t missing = (size_t)frames - grabbed;
        if (atomic_load_explicit(&engine->clock_advancing, memory_order_relaxed)) {
            atomic_fetch_add_explicit(&engine->diag_underrun_callbacks, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(&engine->diag_underrun_frames, missing, memory_order_relaxed);
        }
        memset(output + grabbed * channels, 0, missing * (size_t)channels * sizeof(float));
    }
}

// Shares the serial source/FX/EQ/pan/compensation path between live and independently owned export plans.
bool engine_mix_prepared(Engine* engine, EngineGraph* graph, EngineMixState* mix,
                         uint64_t start_frame, int frames, float* out, float* track_buffer, int channels) {
    if (!graph || !mix || !out || !track_buffer || frames <= 0 || channels <= 0) return false;
    memset(out, 0, (size_t)frames * (size_t)channels * sizeof(float));

    if (mix->fxm) fxm_begin_render_block(mix->fxm, frames);
    if (engine) atomic_store(&engine->diag_processing_latency, fxm_processing_latency(mix->fxm));
    int tcount = mix->track_count;
    int hold_blocks = (int)lroundf((0.45f * (float)engine_graph_get_sample_rate(graph)) / (float)frames);
    if (hold_blocks < 1) {
        hold_blocks = 1;
    }
    for (int t = 0; t < tcount; ++t) {
        memset(track_buffer, 0, (size_t)frames * (size_t)channels * sizeof(float));
        engine_graph_render_track(graph,
                                  track_buffer,
                                  frames,
                                  start_frame,
                                  t);
        if (mix->fxm) {
            fxm_render_track(mix->fxm, t, track_buffer, frames, channels);
        }
        engine_eq_process(&mix->tracks[t].track_eq, track_buffer, frames, channels);
        if (engine) engine_spectrum_update_track(engine, t, track_buffer, frames, channels);
        apply_track_pan(&mix->track_pan_ramps[t], track_buffer, frames, channels);
        if (mix->fxm) fxm_align_track(mix->fxm, t, track_buffer, frames, channels);
        if (engine && mix->track_meters && t < mix->track_count) {
            float peak = 0.0f;
            float rms = 0.0f;
            compute_peak_rms(track_buffer, frames, channels, &peak, &rms);
            update_meter_state(&mix->track_meters[t], peak, rms, hold_blocks);

        }
        for (int s = 0; s < frames * channels; ++s) {
            out[s] += track_buffer[s];
        }
    }

    engine_eq_process(&mix->master_eq, out, frames, channels);
    if (mix->fxm) {
        fxm_render_master(mix->fxm, out, frames, channels);
    }

    if (!engine) for (size_t i = 0; i < (size_t)frames * channels; ++i)
        if (!isfinite(out[i]) || fabsf(out[i]) > 64) return false;
    engine_sanitize_block(out, (size_t)frames * (size_t)channels);
    if (engine && mix->track_meters) {
        float peak = 0.0f;
        float rms = 0.0f;
        compute_peak_rms(out, frames, channels, &peak, &rms);
        update_meter_state(&mix->master_meter, peak, rms, hold_blocks);
    }
    if (engine) engine_publish_mix_meters(mix);
    return true;
}

// Renders the active live revision with its normal analysis and meter publication.
void engine_mix_tracks(Engine* engine, uint64_t start_frame, int frames, float* out,
                       float* track_buffer, int channels) {
    if (!engine) return;
    if (!engine_mix_prepared(engine, engine_render_source_graph(engine), engine_render_mix_state(engine),
                             start_frame, frames, out, track_buffer, channels) && out && frames > 0 && channels > 0)
        memset(out, 0, (size_t)frames * channels * sizeof(float));
}

void engine_mix_midi_audition_only(Engine* engine,
                                   uint64_t start_frame,
                                   int frames,
                                   float* out,
                                   float* track_buffer,
                                   int channels) {
    if (!engine || !out || !track_buffer || frames <= 0 || channels <= 0) {
        return;
    }
    memset(out, 0, (size_t)frames * (size_t)channels * sizeof(float));

    EngineMixState* mix = engine_render_mix_state(engine);
    if (!mix) return;
    int track_index = engine->midi_audition_track_index;
    if (track_index < 0 || track_index >= mix->track_count) {
        track_index = 0;
    }
    if (!engine->midi_audition_source ||
        (engine->midi_audition_notes.note_count <= 0 && !engine->midi_audition_tail_until) ||
        track_index < 0 || track_index >= mix->track_count) {
        return;
    }

    int hold_blocks = (int)lroundf((0.45f * (float)engine->config.sample_rate) / (float)frames);
    if (hold_blocks < 1) {
        hold_blocks = 1;
    }

    memset(track_buffer, 0, (size_t)frames * (size_t)channels * sizeof(float));
    engine_instrument_source_render(engine->midi_audition_source, track_buffer, frames, start_frame);

    for (int frame = 0; frame < frames; ++frame) {
        float gain = fx_sample_ramp_next(&mix->audition_gain_ramps[track_index]);
        for (int ch = 0; ch < channels; ++ch) track_buffer[frame * channels + ch] *= gain;
    }
    if (mix->fxm) {
        fxm_begin_render_block(mix->fxm, frames);
        atomic_store(&engine->diag_processing_latency, fxm_processing_latency(mix->fxm));
        fxm_render_track(mix->fxm, track_index, track_buffer, frames, channels);
    }
    engine_eq_process(&mix->tracks[track_index].track_eq, track_buffer, frames, channels);
    engine_spectrum_update_track(engine, track_index, track_buffer, frames, channels);
    apply_track_pan(&mix->track_pan_ramps[track_index], track_buffer, frames, channels);
    if (mix->fxm) fxm_align_track(mix->fxm, track_index, track_buffer, frames, channels);
    if (mix->track_meters && track_index < mix->track_count) {
        float peak = 0.0f;
        float rms = 0.0f;
        compute_peak_rms(track_buffer, frames, channels, &peak, &rms);
        update_meter_state(&mix->track_meters[track_index], peak, rms, hold_blocks);

    }
    memcpy(out, track_buffer, (size_t)frames * (size_t)channels * sizeof(float));

    engine_eq_process(&mix->master_eq, out, frames, channels);
    if (mix->fxm) {
        fxm_render_master(mix->fxm, out, frames, channels);
    }

    if (engine->midi_audition_tail_until) {
        uint64_t fade = (uint64_t)engine->config.sample_rate / 200;
        for (int frame = 0; frame < frames; ++frame) {
            uint64_t now = start_frame + (uint64_t)frame;
            uint64_t left = now < engine->midi_audition_tail_until ? engine->midi_audition_tail_until - now : 0;
            float gain = left >= fade ? 1 : (float)left / (float)fade;
            for (int ch = 0; ch < channels; ++ch) out[frame * channels + ch] *= gain;
        }
    }

    engine_sanitize_block(out, (size_t)frames * (size_t)channels);
    if (mix->track_meters) {
        float peak = 0.0f;
        float rms = 0.0f;
        compute_peak_rms(out, frames, channels, &peak, &rms);
        update_meter_state(&mix->master_meter, peak, rms, hold_blocks);
    }
    engine_publish_mix_meters(mix);
}
