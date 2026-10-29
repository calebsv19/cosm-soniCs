#include "app_state.h"
#include "daw/composition.h"
#include "daw/midi_actions.h"
#include "engine/analysis_math.h"
#include <math.h>
#include <string.h>

// Measures the actual rendered range at original level with bounded calibrated plot output.
bool daw_composition_measure(const EngineBounceBuffer* buffer, uint64_t window, DawCompositionAnalysis* out) {
    if (!buffer || !buffer->data || !buffer->frame_count || buffer->channels <= 0 || buffer->channels > DAW_COMPOSITION_CHANNELS || window >= buffer->frame_count) return false;
    if(!out)return false;memset(out, 0, sizeof(*out)); out->window_frame = window;out->channel_count=buffer->channels;
    uint64_t count = buffer->frame_count * (uint64_t)buffer->channels;
    double power = 0, sum = 0;
    for (uint64_t i = 0; i < count; ++i) {
        double sample = buffer->data[i]; if (!isfinite(sample)) return false;
        if (fabs(sample) > out->peak) out->peak = fabs(sample);
        if (fabs(sample) >= 1) ++out->clipped_samples;
        if (fabs(sample) > 1) ++out->overload_samples;
        DawChannelMeter* meter=&out->channel_meters[i % buffer->channels];
        if(fabs(sample)>meter->peak)meter->peak=fabs(sample);
        meter->rms+=sample*sample;
        if(fabs(sample)>=1)++meter->full_scale_samples;
        if(fabs(sample)>1)++meter->overload_samples;
        power += sample * sample; sum += sample;
    }
    out->rms = sqrt(power / count); out->dc = sum / count;
    for(int ch=0;ch<buffer->channels;++ch){DawChannelMeter* m=&out->channel_meters[ch];m->rms=sqrt(m->rms/buffer->frame_count);
        m->peak_dbfs=m->peak>0 ? 20*log10(m->peak) : -INFINITY;m->rms_dbfs=m->rms>0 ? 20*log10(m->rms) : -INFINITY;}
    uint64_t wave_length = (uint64_t)buffer->sample_rate / 50;
    if (!wave_length) wave_length = 1;
    for (int p = 0; p < DAW_COMPOSITION_POINTS; ++p) {
        uint64_t frame = window + (uint64_t)p * (wave_length - 1) / (DAW_COMPOSITION_POINTS - 1);
        if (frame < buffer->frame_count) {
            double mono = 0;
            for (int ch = 0; ch < buffer->channels; ++ch) mono += buffer->data[frame * buffer->channels + ch];
            out->waveform[p] = (float)(mono / buffer->channels);
        }
        uint64_t begin = (uint64_t)p * buffer->frame_count / DAW_COMPOSITION_POINTS;
        uint64_t end = (uint64_t)(p + 1) * buffer->frame_count / DAW_COMPOSITION_POINTS;
        double energy = 0;
        for (uint64_t f = begin; f < end; ++f) for (int ch = 0; ch < buffer->channels; ++ch) {
            double v = buffer->data[f * buffer->channels + ch]; energy += v * v;
        }
        out->envelope_rms[p] = end > begin ? (float)sqrt(energy / ((end - begin) * buffer->channels)) : 0;
    }
    float mono[ENGINE_ANALYSIS_MAX_FRAMES] = {0};
    for (int f = 0; f < ENGINE_ANALYSIS_MAX_FRAMES && window + (uint64_t)f < buffer->frame_count; ++f) {
        double sum_channels = 0;
        for (int ch = 0; ch < buffer->channels; ++ch) sum_channels += buffer->data[(window + f) * buffer->channels + ch];
        mono[f] = (float)(sum_channels / buffer->channels);
    }
    float maximum = fminf(20000, buffer->sample_rate * .5f);
    if (maximum <= 20) return false;
    EngineAnalysisPlan plan;
    if (!engine_analysis_prepare(&plan, ENGINE_ANALYSIS_MAX_FRAMES, DAW_COMPOSITION_BINS, buffer->sample_rate, 20, maximum)) return false;
    engine_analysis_compute(&plan, mono, out->spectrum_db);
    for (int b = 0; b < DAW_COMPOSITION_BINS; ++b)
        out->frequency_hz[b] = engine_analysis_frequency(b, DAW_COMPOSITION_BINS, buffer->sample_rate, 20, maximum);
    return true;
}

// Renders one bounded project, track or region snapshot without changing authored or live state.
DawActionStatus daw_action_render_composition(AppState* state, const DawCompositionRequest* request, DawCompositionRender* out) {
    if (!state || !state->engine || !request || !out || out->audio.data) return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(state->engine)) return DAW_ACTION_WRONG_THREAD;
    if (daw_midi_actions_busy(state)) return DAW_ACTION_BUSY;
    const EngineRuntimeConfig* config = engine_get_config(state->engine);
    double rate = config ? config->sample_rate : 0;
    if (!isfinite(rate) || rate <= 40 || request->end <= request->start || request->end > INT64_MAX ||
        request->end - request->start > (uint64_t)rate * DAW_COMPOSITION_SECONDS || request->tail > (uint64_t)rate * DAW_COMPOSITION_TAIL_SECONDS ||
        request->window >= request->end - request->start + request->tail ||
        request->end - request->start + request->tail > UINT64_C(8388608)) return DAW_ACTION_INVALID;
    EngineBounceOptions options = engine_bounce_options_default(); options.normalize_if_clipping = false;
    options.preroll_frames = (uint64_t)rate*(request->comparison_context?60:1); options.tail_frames = request->tail;
    options.bypass_track_fx = options.bypass_master_fx = request->dry;
    int track = -1, clip = -1;
    if (request->has_target) {
        DawActionStatus resolved = daw_action_target_resolve(state, request->target, &track, &clip);
        if (resolved != DAW_ACTION_UNCHANGED) return resolved;
        options.isolate_track = true; options.track_index = track;
        if (request->target.kind == DAW_ACTION_TARGET_MIDI_REGION) options.clip_id = request->target.clip_id;
    }
    *out = (DawCompositionRender){.generation = engine_instance_id(state->engine), .request = *request};
    uint64_t track_id = track < 0 ? 0 : engine_get_tracks(state->engine)[track].runtime_id;
    if (daw_action_get_fx(state, out->generation, 0, &out->master_fx) != DAW_ACTION_UNCHANGED ||
        (track >= 0 && daw_action_get_fx(state, out->generation, track_id, &out->track_fx) != DAW_ACTION_UNCHANGED)) return DAW_ACTION_ENGINE_REJECTED;
    if (!engine_bounce_range_to_buffer_with_options(state->engine, request->start, request->end, &options, NULL, NULL, &out->audio)) return DAW_ACTION_ENGINE_REJECTED;
    if (!daw_composition_measure(&out->audio, request->window, &out->analysis)) {
        daw_composition_render_free(out); return DAW_ACTION_ENGINE_REJECTED;
    }
    return DAW_ACTION_UNCHANGED;
}

// Releases only the private preview buffer and clears its metadata after its consumer finishes.
void daw_composition_render_free(DawCompositionRender* render) {
    if (!render) return;
    engine_bounce_buffer_free(&render->audio); memset(render, 0, sizeof(*render));
}
