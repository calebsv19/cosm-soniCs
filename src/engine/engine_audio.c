#include "engine/engine_internal.h"

#include "engine/graph.h"
#include "engine/instrument.h"

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

static void apply_track_pan(const EngineTrack* track, float* buffer, int frames, int channels) {
    if (!track || !buffer || frames <= 0 || channels < 2) {
        return;
    }
    float pan = track->pan;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    if (pan == 0.0f) {
        return;
    }

    float left = 1.0f;
    float right = 1.0f;
    if (pan < 0.0f) {
        right = 1.0f + pan;
    } else {
        left = 1.0f - pan;
    }
    for (int i = 0; i < frames; ++i) {
        int base = i * channels;
        buffer[base] *= left;
        buffer[base + 1] *= right;
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
    size_t grabbed = audio_queue_read(&engine->output_queue, output, (size_t)frames);
    if (grabbed < (size_t)frames) {
        size_t missing = (size_t)frames - grabbed;
        memset(output + grabbed * channels, 0, missing * (size_t)channels * sizeof(float));
    }
}

void engine_mix_tracks(Engine* engine,
                       uint64_t start_frame,
                       int frames,
                       float* out,
                       float* track_buffer,
                       int channels) {
    if (!engine || !out || !track_buffer || frames <= 0 || channels <= 0) {
        return;
    }
    memset(out, 0, (size_t)frames * (size_t)channels * sizeof(float));

    int tcount = engine->track_count;
    int hold_blocks = (int)lroundf((0.45f * (float)engine->config.sample_rate) / (float)frames);
    if (hold_blocks < 1) {
        hold_blocks = 1;
    }
    int write_index = 1 - atomic_load_explicit(&engine->meter_snapshot_index, memory_order_acquire);
    EngineMeterSnapshot* track_snaps = NULL;
    if (engine->track_meter_snapshots && engine->track_meter_capacity > 0) {
        track_snaps = engine->track_meter_snapshots +
                      (size_t)write_index * (size_t)engine->track_meter_capacity;
    }
    for (int t = 0; t < tcount; ++t) {
        memset(track_buffer, 0, (size_t)frames * (size_t)channels * sizeof(float));
        engine_graph_render_track(engine->graph,
                                  track_buffer,
                                  frames,
                                  start_frame,
                                  t);
        if (engine->fxm) {
            fxm_render_track(engine->fxm, t, track_buffer, frames, channels);
        }
        engine_eq_process(&engine->tracks[t].track_eq, track_buffer, frames, channels);
        engine_spectrum_update_track(engine, t, track_buffer, frames, channels);
        apply_track_pan(&engine->tracks[t], track_buffer, frames, channels);
        if (engine->track_meters && t < engine->track_meter_capacity) {
            float peak = 0.0f;
            float rms = 0.0f;
            compute_peak_rms(track_buffer, frames, channels, &peak, &rms);
            update_meter_state(&engine->track_meters[t], peak, rms, hold_blocks);
            if (track_snaps) {
                track_snaps[t].peak = engine->track_meters[t].peak;
                track_snaps[t].rms = engine->track_meters[t].rms;
                track_snaps[t].clipped = engine->track_meters[t].clip_hold > 0;
            }
        }
        for (int s = 0; s < frames * channels; ++s) {
            out[s] += track_buffer[s];
        }
    }

    engine_eq_process(&engine->master_eq, out, frames, channels);
    if (engine->fxm) {
        fxm_render_master(engine->fxm, out, frames, channels);
    }

    engine_sanitize_block(out, (size_t)frames * (size_t)channels);
    if (engine->track_meters) {
        float peak = 0.0f;
        float rms = 0.0f;
        compute_peak_rms(out, frames, channels, &peak, &rms);
        update_meter_state(&engine->master_meter, peak, rms, hold_blocks);
        engine->master_meter_snapshots[write_index].peak = engine->master_meter.peak;
        engine->master_meter_snapshots[write_index].rms = engine->master_meter.rms;
        engine->master_meter_snapshots[write_index].clipped = engine->master_meter.clip_hold > 0;
    }
    atomic_store_explicit(&engine->meter_snapshot_index, write_index, memory_order_release);
}

static bool engine_track_allowed_for_audition(const Engine* engine, int track_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    const EngineTrack* track = &engine->tracks[track_index];
    if (!track->active || track->muted) {
        return false;
    }
    bool any_solo = false;
    for (int i = 0; i < engine->track_count; ++i) {
        const EngineTrack* candidate = &engine->tracks[i];
        if (candidate->active && !candidate->muted && candidate->solo) {
            any_solo = true;
            break;
        }
    }
    return !any_solo || track->solo;
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

    int track_index = engine->midi_audition_track_index;
    if (track_index < 0 || track_index >= engine->track_count) {
        track_index = 0;
    }
    if (!engine->midi_audition_source ||
        engine->midi_audition_notes.note_count <= 0 ||
        !engine_track_allowed_for_audition(engine, track_index)) {
        return;
    }

    int hold_blocks = (int)lroundf((0.45f * (float)engine->config.sample_rate) / (float)frames);
    if (hold_blocks < 1) {
        hold_blocks = 1;
    }
    int write_index = 1 - atomic_load_explicit(&engine->meter_snapshot_index, memory_order_acquire);
    EngineMeterSnapshot* track_snaps = NULL;
    if (engine->track_meter_snapshots && engine->track_meter_capacity > 0) {
        track_snaps = engine->track_meter_snapshots +
                      (size_t)write_index * (size_t)engine->track_meter_capacity;
    }

    memset(track_buffer, 0, (size_t)frames * (size_t)channels * sizeof(float));
    engine_instrument_source_render(engine->midi_audition_source, track_buffer, frames, start_frame);

    float track_gain = engine->tracks[track_index].gain != 0.0f ? engine->tracks[track_index].gain : 1.0f;
    for (int s = 0; s < frames * channels; ++s) {
        track_buffer[s] *= track_gain;
    }
    if (engine->fxm) {
        fxm_render_track(engine->fxm, track_index, track_buffer, frames, channels);
    }
    engine_eq_process(&engine->tracks[track_index].track_eq, track_buffer, frames, channels);
    engine_spectrum_update_track(engine, track_index, track_buffer, frames, channels);
    apply_track_pan(&engine->tracks[track_index], track_buffer, frames, channels);
    if (engine->track_meters && track_index < engine->track_meter_capacity) {
        float peak = 0.0f;
        float rms = 0.0f;
        compute_peak_rms(track_buffer, frames, channels, &peak, &rms);
        update_meter_state(&engine->track_meters[track_index], peak, rms, hold_blocks);
        if (track_snaps) {
            track_snaps[track_index].peak = engine->track_meters[track_index].peak;
            track_snaps[track_index].rms = engine->track_meters[track_index].rms;
            track_snaps[track_index].clipped = engine->track_meters[track_index].clip_hold > 0;
        }
    }
    memcpy(out, track_buffer, (size_t)frames * (size_t)channels * sizeof(float));

    engine_eq_process(&engine->master_eq, out, frames, channels);
    if (engine->fxm) {
        fxm_render_master(engine->fxm, out, frames, channels);
    }

    engine_sanitize_block(out, (size_t)frames * (size_t)channels);
    if (engine->track_meters) {
        float peak = 0.0f;
        float rms = 0.0f;
        compute_peak_rms(out, frames, channels, &peak, &rms);
        update_meter_state(&engine->master_meter, peak, rms, hold_blocks);
        engine->master_meter_snapshots[write_index].peak = engine->master_meter.peak;
        engine->master_meter_snapshots[write_index].rms = engine->master_meter.rms;
        engine->master_meter_snapshots[write_index].clipped = engine->master_meter.clip_hold > 0;
    }
    atomic_store_explicit(&engine->meter_snapshot_index, write_index, memory_order_release);
}
