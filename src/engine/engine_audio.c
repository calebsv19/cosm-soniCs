#include "engine/engine_internal.h"

#include "engine/graph.h"
#include "engine/groups.h"
#include "engine/mix_probe.h"
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

// Clears only prepared return sums and refuses oversized render blocks before writing buffers.
static bool routing_begin(EngineMixState* mix, int frames, int channels) {
    if (!mix->return_count) return true;
    size_t samples = (size_t)frames * channels;
    if (!mix->return_buffers || samples > mix->return_buffer_samples) return false;
    for (int b = 0; b < mix->return_count; ++b)
        memset(mix->return_buffers + b * mix->return_buffer_samples, 0, samples * sizeof(float));
    return true;
}
// Accumulates post-fader/pan/insert signals into independently prepared shared return inputs.
static void routing_send(EngineMixState* mix, int track, const float* source, int frames, int channels,uint64_t start) {
    if (!mix->return_count || mix->bypass_returns) return;
    uint64_t latency=0;for(int t=0;t<mix->track_count;++t)if(!mix->tracks[t].is_return){uint64_t delay=fxm_track_processing_latency(mix->fxm,t,false);if(delay>latency)latency=delay;}
    for (int send = 0; send < ENGINE_SEND_CAP; ++send) {
        int destination = mix->send_destinations[track * ENGINE_SEND_CAP + send];
        if (destination < 0) continue;
        float* out = mix->return_buffers + destination * mix->return_buffer_samples;
        FxSampleRamp* ramp = &mix->send_ramps[track * ENGINE_SEND_CAP + send];
        for (int frame = 0; frame < frames; ++frame) {
            float gain = fx_sample_ramp_next(ramp);
            const EngineMixerAutomation* a=&mix->tracks[track].sends[send].automation;
            if(a->count)gain=start+(uint64_t)frame>=latency?engine_mixer_automation_eval(a,start+(uint64_t)frame-latency,mix->tracks[track].sends[send].gain):0;
            for (int ch = 0; ch < channels; ++ch) out[frame * channels + ch] += source[frame * channels + ch] * gain;
        }
    }
}
// Keeps return solo auditions audible through their source inputs while suppressing the dry sum.
static bool routing_dry_enabled(const EngineMixState* mix) {
    if (mix->isolated_return >= 0) return false;
    for (int b = 0; b < mix->return_count; ++b) {
        const EngineTrack* row = &mix->tracks[mix->return_tracks[b]];
        if (row->solo && !row->muted) return false;
    }
    return true;
}
// Processes each shared return once and aligns the dry and wet paths before the master stage.
static void routing_returns(Engine* engine, EngineMixState* mix, float* out, int frames, int channels, int hold_blocks, uint64_t start) {
    if (mix->bypass_returns || !mix->return_count) return;
    if (mix->fxm) fxm_align_dry(mix->fxm, out, frames, channels);
    for (int b = 0; b < mix->return_count; ++b) {
        int track = mix->return_tracks[b]; float* buffer = mix->return_buffers + b * mix->return_buffer_samples;
        engine_mix_probe_emit(mix,track,ENGINE_MIX_INPUT,start,buffer,frames,channels);
        if (mix->fxm) { fxm_set_automation_clock(mix->fxm,start,fxm_source_processing_latency(mix->fxm));
            fxm_render_track(mix->fxm, track, buffer, frames, channels); }
        engine_eq_process(&mix->tracks[track].track_eq, buffer, frames, channels);
        engine_mix_probe_emit(mix,track,ENGINE_MIX_INSERTS_EQ,start,buffer,frames,channels);
        for (int frame = 0; frame < frames; ++frame) {
            float gain = fx_sample_ramp_next(&mix->audition_gain_ramps[track]);
            for (int ch = 0; ch < channels; ++ch) buffer[frame * channels + ch] *= gain;
        }
        apply_track_pan(&mix->track_pan_ramps[track], buffer, frames, channels);
        engine_mix_probe_emit(mix,track,ENGINE_MIX_OUTPUT,start,buffer,frames,channels);
        engine_live_meter_observe(engine,mix,track,start,buffer,frames,channels);
        if (mix->fxm) fxm_align_track(mix->fxm, track, buffer, frames, channels);
        if (engine) {
            engine_spectrum_update_track(engine, track, buffer, frames, channels);
            if (mix->track_meters) { float peak, rms; compute_peak_rms(buffer, frames, channels, &peak, &rms); update_meter_state(&mix->track_meters[track], peak, rms, hold_blocks); }
        }
        if (mix->isolated_return >= 0 && mix->isolated_return != track) continue;
        for (int sample = 0; sample < frames * channels; ++sample) out[sample] += buffer[sample];
    }
}

// Processes dependency-ordered child, group and return chains once before the master sum.
static bool mix_groups(Engine* engine,EngineGraph* graph,EngineMixState* mix,uint64_t start,int frames,float* out,int channels,int hold,bool audition) {
    mix->automation_frame=start;
    if(!engine_group_mix_begin(mix->group_mix,mix,frames,channels))return false;
    if(engine)atomic_store(&engine->diag_processing_latency,engine_group_mix_latency(mix->group_mix,mix));
    for(int p=0;p<mix->track_count;++p) {
        int t=engine_group_mix_track(mix->group_mix,p);float* buffer=engine_group_mix_buffer(mix->group_mix,t);
        const EngineTrack* row=&mix->tracks[t];
        if(!row->is_group && !row->is_return) {
            if(!audition)engine_graph_render_track(graph,buffer,frames,start,t);
            else if(t==engine->midi_audition_track_index) {
                engine_instrument_source_render(engine->midi_audition_source,buffer,frames,start);
                for(int f=0;f<frames;++f){float gain=fx_sample_ramp_next(&mix->audition_gain_ramps[t]);
                    for(int ch=0;ch<channels;++ch)buffer[f*channels+ch]*=gain;}
            }
        }
        engine_mix_probe_emit(mix,t,ENGINE_MIX_INPUT,start,buffer,frames,channels);
        if(mix->fxm){fxm_set_automation_clock(mix->fxm,start,engine_group_mix_input_latency(mix->group_mix,t));fxm_render_track(mix->fxm,t,buffer,frames,channels);}
        engine_eq_process(&mix->tracks[t].track_eq,buffer,frames,channels);
        engine_mix_probe_emit(mix,t,ENGINE_MIX_INSERTS_EQ,start,buffer,frames,channels);
        bool enabled=row->volume_automation.count && engine_group_enabled(mix->tracks,mix->track_count,t);
        uint64_t latency=engine_group_mix_input_latency(mix->group_mix,t)+fxm_track_processing_latency(mix->fxm,t,false);
        if(row->is_group || row->is_return)for(int f=0;f<frames;++f) {
            float gain=fx_sample_ramp_next(&mix->audition_gain_ramps[t]);
            if(row->is_group && row->volume_automation.count)gain=enabled && start+(uint64_t)f>=latency?engine_mixer_automation_eval(&row->volume_automation,start+(uint64_t)f-latency,row->gain):0;
            for(int ch=0;ch<channels;++ch)buffer[f*channels+ch]*=gain;
        }
        apply_track_pan(&mix->track_pan_ramps[t],buffer,frames,channels);
        engine_mix_probe_emit(mix,t,ENGINE_MIX_OUTPUT,start,buffer,frames,channels);
        engine_live_meter_observe(engine,mix,t,start,buffer,frames,channels);
        if(engine) {
            engine_spectrum_update_track(engine,t,buffer,frames,channels);
            float peak,rms;compute_peak_rms(buffer,frames,channels,&peak,&rms);
            update_meter_state(&mix->track_meters[t],peak,rms,hold);
        }
        engine_group_mix_forward(mix->group_mix,mix,t,out,frames,channels);
    }
    return true;
}

// Shares the serial source/FX/EQ/pan/compensation path between live and independently owned export plans.
bool engine_mix_prepared(Engine* engine, EngineGraph* graph, EngineMixState* mix,
                         uint64_t start_frame, int frames, float* out, float* track_buffer, int channels) {
    if (!graph || !mix || !out || !track_buffer || frames <= 0 || channels <= 0) return false;
    memset(out, 0, (size_t)frames * (size_t)channels * sizeof(float));

    if (!routing_begin(mix, frames, channels)) return false;
    mix->automation_frame=start_frame;
    bool dry_enabled = routing_dry_enabled(mix);
    if(mix->probe)mix->probe->block_start=start_frame;
    if (mix->fxm) fxm_begin_render_block(mix->fxm, frames);
    if (engine) atomic_store(&engine->diag_processing_latency, fxm_processing_latency(mix->fxm));
    int tcount = mix->track_count;
    int hold_blocks = (int)lroundf((0.45f * (float)engine_graph_get_sample_rate(graph)) / (float)frames);
    if (hold_blocks < 1) {
        hold_blocks = 1;
    }
    if(mix->group_mix) {
        if(!mix_groups(engine,graph,mix,start_frame,frames,out,channels,hold_blocks,false))return false;
    } else for (int t = 0; t < tcount; ++t) {
        if (mix->tracks[t].is_return) continue;
        memset(track_buffer, 0, (size_t)frames * (size_t)channels * sizeof(float));
        engine_graph_render_track(graph,
                                  track_buffer,
                                  frames,
                                  start_frame,
                                  t);
        engine_mix_probe_emit(mix,t,ENGINE_MIX_INPUT,start_frame,track_buffer,frames,channels);
        if (mix->fxm) {
            fxm_set_automation_clock(mix->fxm,start_frame,0);
            fxm_render_track(mix->fxm, t, track_buffer, frames, channels);
        }
        engine_eq_process(&mix->tracks[t].track_eq, track_buffer, frames, channels);
        engine_mix_probe_emit(mix,t,ENGINE_MIX_INSERTS_EQ,start_frame,track_buffer,frames,channels);
        if (engine) engine_spectrum_update_track(engine, t, track_buffer, frames, channels);
        apply_track_pan(&mix->track_pan_ramps[t], track_buffer, frames, channels);
        engine_mix_probe_emit(mix,t,ENGINE_MIX_OUTPUT,start_frame,track_buffer,frames,channels);
        engine_live_meter_observe(engine,mix,t,start_frame,track_buffer,frames,channels);
        if (mix->fxm) fxm_align_track(mix->fxm, t, track_buffer, frames, channels);
        if (engine && mix->track_meters && t < mix->track_count) {
            float peak = 0.0f;
            float rms = 0.0f;
            compute_peak_rms(track_buffer, frames, channels, &peak, &rms);
            update_meter_state(&mix->track_meters[t], peak, rms, hold_blocks);

        }
        routing_send(mix, t, track_buffer, frames, channels,start_frame);
        if (dry_enabled) for (int s = 0; s < frames * channels; ++s) {
            out[s] += track_buffer[s];
        }
    }

    if(!mix->group_mix)routing_returns(engine, mix, out, frames, channels, hold_blocks, start_frame);
    engine_mix_probe_emit(mix,-1,ENGINE_MIX_INPUT,start_frame,out,frames,channels);
    engine_eq_process(&mix->master_eq, out, frames, channels);
    engine_mix_probe_emit(mix,-1,ENGINE_MIX_INSERTS_EQ,start_frame,out,frames,channels);
    if (mix->fxm) {
        fxm_set_automation_clock(mix->fxm,start_frame,(mix->group_mix?engine_group_mix_latency(mix->group_mix,mix):fxm_processing_latency(mix->fxm))-fxm_track_processing_latency(mix->fxm,-1,false));
        fxm_render_master(mix->fxm, out, frames, channels);
    }

    engine_mix_probe_emit(mix,-1,ENGINE_MIX_OUTPUT,start_frame,out,frames,channels);
    if (mix->probe && mix->probe->failed) return false;
    if (!engine) for (size_t i = 0; i < (size_t)frames * channels; ++i)
        if (!isfinite(out[i]) || fabsf(out[i]) > 64) return false;
    engine_sanitize_block(out, (size_t)frames * (size_t)channels);
    engine_mix_probe_emit(mix,-1,ENGINE_MIX_SANITIZED,start_frame,out,frames,channels);
    if (mix->probe && mix->probe->failed) return false;
    if (engine && mix->track_meters) {
        float peak = 0.0f;
        float rms = 0.0f;
        compute_peak_rms(out, frames, channels, &peak, &rms);
        update_meter_state(&mix->master_meter, peak, rms, hold_blocks);
    }
    if (engine) engine_live_meter_observe(engine,mix,-1,start_frame,out,frames,channels);
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
    if (!mix || !routing_begin(mix, frames, channels)) return;
    int track_index = engine->midi_audition_track_index;
    if (track_index < 0 || track_index >= mix->track_count) {
        track_index = 0;
    }
    if (!engine->midi_audition_source ||
        (engine->midi_audition_notes.note_count <= 0 && !engine->midi_audition_tail_until) ||
        track_index < 0 || track_index >= mix->track_count || (mix->tracks[track_index].is_return || mix->tracks[track_index].is_group)) {
        return;
    }

    int hold_blocks = (int)lroundf((0.45f * (float)engine->config.sample_rate) / (float)frames);
    if (hold_blocks < 1) {
        hold_blocks = 1;
    }

    if(mix->group_mix) {
        if(mix->fxm)fxm_begin_render_block(mix->fxm,frames);
        if(!mix_groups(engine,engine_render_source_graph(engine),mix,start_frame,frames,out,channels,hold_blocks,true))return;
    } else {
    memset(track_buffer, 0, (size_t)frames * (size_t)channels * sizeof(float));
    engine_instrument_source_render(engine->midi_audition_source, track_buffer, frames, start_frame);

    for (int frame = 0; frame < frames; ++frame) {
        float gain = fx_sample_ramp_next(&mix->audition_gain_ramps[track_index]);
        for (int ch = 0; ch < channels; ++ch) track_buffer[frame * channels + ch] *= gain;
    }
    if (mix->fxm) {
        fxm_begin_render_block(mix->fxm, frames);
        atomic_store(&engine->diag_processing_latency, fxm_processing_latency(mix->fxm));
        fxm_set_automation_clock(mix->fxm,start_frame,0);
        fxm_render_track(mix->fxm, track_index, track_buffer, frames, channels);
    }
    engine_eq_process(&mix->tracks[track_index].track_eq, track_buffer, frames, channels);
    engine_spectrum_update_track(engine, track_index, track_buffer, frames, channels);
    apply_track_pan(&mix->track_pan_ramps[track_index], track_buffer, frames, channels);
    if (mix->fxm) fxm_align_track(mix->fxm, track_index, track_buffer, frames, channels);
    engine_live_meter_observe(engine,mix,track_index,start_frame,track_buffer,frames,channels);
    if (mix->track_meters && track_index < mix->track_count) {
        float peak = 0.0f;
        float rms = 0.0f;
        compute_peak_rms(track_buffer, frames, channels, &peak, &rms);
        update_meter_state(&mix->track_meters[track_index], peak, rms, hold_blocks);

    }
    routing_send(mix, track_index, track_buffer, frames, channels,start_frame);
    if (routing_dry_enabled(mix)) memcpy(out, track_buffer, (size_t)frames * (size_t)channels * sizeof(float));
    routing_returns(engine, mix, out, frames, channels, hold_blocks, start_frame);

    }

    engine_eq_process(&mix->master_eq, out, frames, channels);
    if (mix->fxm) {
        fxm_set_automation_clock(mix->fxm,start_frame,(mix->group_mix?engine_group_mix_latency(mix->group_mix,mix):fxm_processing_latency(mix->fxm))-fxm_track_processing_latency(mix->fxm,-1,false));
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
    engine_live_meter_observe(engine,mix,-1,start_frame,out,frames,channels);
    engine_publish_mix_meters(mix);
}
