#include "engine/engine_internal.h"
#include "engine/instrument.h"
#include "engine/sampler.h"

#include <stdlib.h>

// Owns one independently prepared source and its immutable media pin, or an audition tap.
typedef struct EngineOwnedSource {
    EngineSamplerSource* sampler;
    EngineInstrumentSource* instrument;
    const AudioMediaClip* media;
    Engine* audition_engine;
    int audition_track;
    uint64_t export_end;
} EngineOwnedSource;

// Captures all scalar mixer targets so coalescing retains every accepted edit.
typedef struct EngineScalarTrack {
    uint64_t identity;
    float gain, pan, source_gain, audition_gain;
    bool muted, solo;
} EngineScalarTrack;

// Holds one complete source revision until the worker retires it to the control thread.
struct EngineSourcePlan {
    EngineScalarTrack* scalars;
    int scalar_count;
    EngineGraph* graph;
    EngineMixState* mix;
    EngineOwnedSource* sources;
    size_t source_count;
    EngineSourcePlan* retired_next;
};

// Renders the worker-owned live instrument without reading editable region or track storage.
static void source_plan_render_audition(void* userdata, float* output, int frames, uint64_t start) {
    EngineOwnedSource* source = userdata;
    Engine* engine = source->audition_engine;
    if (engine && engine->midi_audition_track_index == source->audition_track &&
        engine->midi_audition_notes.note_count > 0) {
        engine_instrument_source_render(engine->midi_audition_source, output, frames, start);
    }
}

static const EngineGraphSourceOps audition_ops = {.render = source_plan_render_audition};

// Renders a captured source only through its export input boundary, leaving latency flush silent.
static void source_plan_render_export(void* userdata, float* output, int frames, uint64_t start) {
    EngineOwnedSource* source = userdata;
    if (start >= source->export_end) return;
    uint64_t left = source->export_end - start;
    if ((uint64_t)frames > left) frames = (int)left;
    if (source->instrument) engine_instrument_source_render(source->instrument, output, frames, start);
    else engine_sampler_source_render(source->sampler, output, frames, start);
}

// Configures only captured source format; export gates remain part of the immutable snapshot.
static void source_plan_reset_export(void* userdata, int rate, int channels) {
    EngineOwnedSource* source = userdata;
    if (source->instrument) engine_instrument_source_reset(source->instrument, rate, channels);
    else engine_sampler_source_reset(source->sampler, rate, channels);
}

static const EngineGraphSourceOps export_ops = {
    .render = source_plan_render_export, .reset = source_plan_reset_export
};

// Frees only plans whose worker ownership has ended, including their control-owned cache pins.
static void source_plan_destroy(Engine* engine, EngineSourcePlan* plan) {
    if (!plan) return;
    engine_graph_destroy(plan->graph);
    if (plan->mix) {
        fxm_destroy(plan->mix->fxm);
        engine_eq_free(&plan->mix->master_eq);
        for (int t = 0; t < plan->mix->track_count; ++t) engine_eq_free(&plan->mix->tracks[t].track_eq);
        free(plan->mix->tracks);
        free(plan->mix->previous_tracks);
        free(plan->mix->track_pan_ramps);
        free(plan->mix->audition_gain_ramps);
        free(plan->mix->track_meters);
        free(plan->mix->track_fx_meters);
        free(plan->mix);
    }
    for (size_t i = 0; i < plan->source_count; ++i) {
        engine_sampler_source_destroy(plan->sources[i].sampler);
        engine_instrument_source_destroy(plan->sources[i].instrument);
        if (plan->sources[i].media) {
            audio_media_cache_release(&engine->media_cache, plan->sources[i].media);
        }
    }
    free(plan->scalars);
    free(plan->sources);
    free(plan);
}

// Prepares independent scalar track metadata and DSP state from the exclusively editable model.
static bool source_plan_prepare_mix(Engine* engine, EngineSourcePlan* plan, bool offline) {
    EngineMixState* mix = plan->mix = calloc(1, sizeof(*mix));
    if (!mix) return false;
    mix->owner = offline ? NULL : engine;
    size_t count = engine->track_count > 0 ? (size_t)engine->track_count : 1;
    mix->track_pan_ramps = calloc(count, sizeof(*mix->track_pan_ramps));
    mix->audition_gain_ramps = calloc(count, sizeof(*mix->audition_gain_ramps));
    if (!mix->track_pan_ramps || !mix->audition_gain_ramps) return false;
    mix->tracks = calloc(count, sizeof(*mix->tracks));
    mix->previous_tracks = malloc(count * sizeof(*mix->previous_tracks));
    mix->track_meters = calloc(count, sizeof(*mix->track_meters));
    mix->track_fx_meters = calloc(count, sizeof(*mix->track_fx_meters));
    if (!mix->tracks || !mix->previous_tracks || !mix->track_meters || !mix->track_fx_meters ||
        !engine_eq_clone_configuration(&mix->master_eq, &engine->master_eq)) return false;
    mix->track_count = engine->track_count;
    for (int t = 0; t < mix->track_count; ++t) {
        EngineTrack* dst = &mix->tracks[t];
        const EngineTrack* src = &engine->tracks[t];
        dst->runtime_id = src->runtime_id;
        dst->gain = src->gain;
        dst->pan = src->pan;
        fx_sample_ramp_reset(&mix->track_pan_ramps[t], src->pan);
        bool any_solo = false;
        for (int q = 0; q < engine->track_count; ++q)
            if (engine->tracks[q].active && !engine->tracks[q].muted && engine->tracks[q].solo) any_solo = true;
        float gain = src->active && !src->muted && (!any_solo || src->solo) ? src->gain : 0;
        fx_sample_ramp_reset(&mix->audition_gain_ramps[t], gain);
        dst->muted = src->muted;
        dst->solo = src->solo;
        dst->active = src->active;
        if (!engine_eq_clone_configuration(&dst->track_eq, &src->track_eq)) return false;
    }
    if (engine->fxm) {
        mix->fxm = fxm_clone_for_render(engine->fxm);
        if (!mix->fxm) return false;
        if (!offline) {
            engine_bind_fx_meter_tap(mix);
            engine_bind_fx_scope_tap(engine, mix->fxm);
        }
    }
    return true;
}

// Reclaims a detached retirement list; the worker never accesses these plans again.
void engine_source_plan_collect(Engine* engine) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id) return;
    EngineSourcePlan* plan = atomic_exchange_explicit(&engine->retired_source_plans, NULL, memory_order_acq_rel);
    while (plan) {
        EngineSourcePlan* next = plan->retired_next;
        source_plan_destroy(engine, plan);
        plan = next;
    }
}

// Prepares all sources transactionally so allocation failure cannot publish a partial graph.
static EngineSourcePlan* source_plan_build(Engine* engine, bool offline, uint64_t end, uint64_t tail) {
    EngineSourcePlan* plan = calloc(1, sizeof(*plan));
    if (!plan) return NULL;
    if (!source_plan_prepare_mix(engine, plan, offline)) goto fail;
    plan->graph = engine_graph_create(engine->config.sample_rate,
                                     engine_graph_get_channels(engine->graph), engine->config.block_size);
    size_t capacity = (size_t)engine->track_count;
    bool any_solo = false;
    int armed = offline ? -1 : atomic_load_explicit(&engine->record_armed_track_index, memory_order_acquire);
    for (int t = 0; t < engine->track_count; ++t) {
        const EngineTrack* track = &engine->tracks[t];
        capacity += (size_t)track->clip_count;
        if (!track->muted && track->solo &&
            (t == armed || (track->active && track->clip_count > 0))) any_solo = true;
    }
    plan->sources = calloc(capacity ? capacity : 1, sizeof(*plan->sources));
    if (!plan->graph || !plan->sources) goto fail;
    for (int t = 0; t < engine->track_count; ++t) {
        const EngineTrack* track = &engine->tracks[t];
        float track_gain = track->active && !track->muted && (!any_solo || track->solo) ? track->gain : 0;
        for (int c = 0; c < track->clip_count; ++c) {
            const EngineClip* clip = &track->clips[c];
            if (!clip->active || (offline && clip->timeline_start_frames >= end)) continue;
            EngineOwnedSource* owned = &plan->sources[plan->source_count++];
            const EngineGraphSourceOps* ops = NULL;
            void* source = NULL;
            if (clip->kind == ENGINE_CLIP_KIND_MIDI) {
                owned->instrument = engine_instrument_source_create();
                const EngineAutomationLane* track_lanes = NULL;
                int lane_count = 0;
                if (engine_clip_midi_inherits_track_instrument(clip)) {
                    track_lanes = track->midi_instrument_automation_lanes;
                    lane_count = track->midi_instrument_automation_lane_count;
                }
                if (!owned->instrument || !engine_instrument_source_set_midi_clip(
                        owned->instrument, clip->timeline_start_frames, clip->duration_frames,
                        engine_clip_midi_effective_instrument_preset(engine, t, c),
                        engine_clip_midi_effective_instrument_params(engine, t, c),
                        clip->midi_notes.notes, clip->midi_notes.note_count, track_lanes, lane_count,
                        clip->automation_lanes, clip->automation_lane_count)) goto fail;
                if (offline) engine_instrument_source_set_export_end(owned->instrument, end, tail);
                ops = &engine->instrument_ops;
                source = owned->instrument;
            } else {
                const AudioMediaClip* media = engine_sampler_get_media(clip->sampler);
                if (!media || !audio_media_cache_retain(&engine->media_cache, media)) goto fail;
                owned->media = media;
                owned->sampler = engine_sampler_source_clone(clip->sampler);
                if (!owned->sampler) goto fail;
                engine_sampler_source_set_fade_curves(owned->sampler, clip->fade_in_curve, clip->fade_out_curve);
                ops = &engine->sampler_ops;
                source = owned->sampler;
            }
            if (offline) {
                owned->export_end = owned->instrument ? end + tail : end;
                ops = &export_ops;
                source = owned;
            }
            if (!engine_graph_add_source_identified(plan->graph, ops, source, track_gain * clip->gain, t, clip->creation_index)) goto fail;
            engine_graph_set_last_clip_gain(plan->graph, clip->gain);
            if (owned->sampler) {
                uint64_t first = engine_sampler_get_start_frame(owned->sampler);
                uint64_t length = engine_sampler_get_frame_count(owned->sampler);
                if (offline && (first >= end || length > end - first)) length = first >= end ? 0 : end - first;
                engine_graph_bound_last_source(plan->graph, first, length);
            }
        }
        if (offline) continue;
        EngineOwnedSource* audition = &plan->sources[plan->source_count++];
        audition->audition_engine = engine;
        audition->audition_track = t;
        if (!engine_graph_add_source_identified(plan->graph, &audition_ops, audition, track_gain, t, UINT64_MAX)) goto fail;
        engine_graph_set_last_clip_gain(plan->graph, 1.0f);
    }
    if (!engine_graph_prepare_identity_lookup(plan->graph)) goto fail;
    return plan;
fail:
    source_plan_destroy(engine, plan);
    return NULL;
}

// Publishes the latest fully prepared revision and reclaims superseded unpublished work.
bool engine_source_plan_publish(Engine* engine) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id) return false;
    engine_source_plan_collect(engine);
    if (engine->fxm && !engine_fx_set_track_count(engine, engine->track_count)) return false;
    EngineSourcePlan* plan = source_plan_build(engine, false, 0, 0);
    if (!plan) {
        engine_trace(engine, "engine_rebuild_sources: preparation failed; retaining previous render revision");
        return false;
    }
    EngineSourcePlan* superseded = atomic_exchange_explicit(&engine->pending_source_plan, plan, memory_order_acq_rel);
    source_plan_destroy(engine, superseded);
    engine->scalar_mix_supported = true;
    return true;
}

// Carries meter history only for effect identities still present in the prepared chain.
static void source_plan_transfer_fx_meters(EngineFxMeterBank* destination, const EngineFxMeterBank* previous,
                                           const FxMasterSnapshot* chain) {
    destination->count = 0;
    for (int i = 0; i < chain->count; ++i) {
        for (int p = 0; p < previous->count; ++p) {
            if (chain->items[i].id == previous->taps[p].id) {
                destination->taps[destination->count++] = previous->taps[p];
                break;
            }
        }
    }
}

// Applies scalar targets only after validating the complete captured track lifetime map.
static bool source_plan_apply_scalars(EngineSourcePlan* target, const EngineSourcePlan* update, uint32_t ramp) {
    if (!target || target->mix->track_count != update->scalar_count) return false;
    for (int t = 0; t < update->scalar_count; ++t)
        if (target->mix->tracks[t].runtime_id != update->scalars[t].identity) return false;
    for (int t = 0; t < update->scalar_count; ++t) {
        const EngineScalarTrack* value = &update->scalars[t];
        EngineTrack* track = &target->mix->tracks[t];
        track->gain = value->gain;
        track->pan = value->pan;
        track->muted = value->muted;
        track->solo = value->solo;
        engine_graph_set_track_gain(target->graph, t, value->source_gain, ramp);
        if (ramp) {
            fx_sample_ramp_target(&target->mix->track_pan_ramps[t], value->pan, ramp);
            fx_sample_ramp_target(&target->mix->audition_gain_ramps[t], value->audition_gain, ramp);
        } else {
            fx_sample_ramp_reset(&target->mix->track_pan_ramps[t], value->pan);
            fx_sample_ramp_reset(&target->mix->audition_gain_ramps[t], value->audition_gain);
        }
    }
    return true;
}

// Returns detached plan ownership to the control thread without freeing on the worker.
static void source_plan_retire(Engine* engine, EngineSourcePlan* plan) {
    if (!plan) return;
    EngineSourcePlan* head = atomic_load_explicit(&engine->retired_source_plans, memory_order_acquire);
    do {
        plan->retired_next = head;
    } while (!atomic_compare_exchange_weak_explicit(&engine->retired_source_plans, &head, plan,
                                                    memory_order_release, memory_order_acquire));
}

// Adopts a complete revision and returns replaced ownership through the retirement list.
void engine_source_plan_adopt(Engine* engine, EngineSourcePlan* plan) {
    if (!engine || !plan) return;
    if (plan->scalars) {
        (void)source_plan_apply_scalars(engine->active_source_plan, plan,
            atomic_load(&engine->worker_running) ? (uint32_t)(engine->config.sample_rate / 200) : 0);
        source_plan_retire(engine, plan);
        return;
    }
    EngineSourcePlan* old = engine->active_source_plan;
    if (old) {
        for (int t = 0; t < plan->mix->track_count; ++t) {
            int previous = -1;
            for (int p = 0; p < old->mix->track_count; ++p) {
                if (plan->mix->tracks[t].runtime_id == old->mix->tracks[p].runtime_id) { previous = p; break; }
            }
            plan->mix->previous_tracks[t] = previous;
            if (previous >= 0) engine_eq_transfer_history(&plan->mix->tracks[t].track_eq,
                                                         &old->mix->tracks[previous].track_eq);
            if (previous >= 0) plan->mix->track_meters[t] = old->mix->track_meters[previous];
            if (previous >= 0 && atomic_load(&engine->worker_running)) {
                float gain = plan->mix->audition_gain_ramps[t].target;
                plan->mix->audition_gain_ramps[t] = old->mix->audition_gain_ramps[previous];
                fx_sample_ramp_target(&plan->mix->audition_gain_ramps[t], gain,
                                     (uint32_t)(engine->config.sample_rate / 200));
                plan->mix->track_pan_ramps[t] = old->mix->track_pan_ramps[previous];
                fx_sample_ramp_target(&plan->mix->track_pan_ramps[t], plan->mix->tracks[t].pan,
                                     (uint32_t)(engine->config.sample_rate / 200));
            }
        }
        if (atomic_load(&engine->worker_running))
            engine_graph_transfer_gains(plan->graph, old->graph, plan->mix->previous_tracks, plan->mix->track_count);
        engine_eq_transfer_history(&plan->mix->master_eq, &old->mix->master_eq);
        plan->mix->master_meter = old->mix->master_meter;
        if (plan->mix->fxm && old->mix->fxm)
            (void)fxm_transfer_render_state(plan->mix->fxm, old->mix->fxm,
                                           plan->mix->previous_tracks, plan->mix->track_count);
        if (plan->mix->fxm) {
            FxMasterSnapshot chain;
            if (fxm_master_snapshot(plan->mix->fxm, &chain))
                source_plan_transfer_fx_meters(&plan->mix->master_fx_meters, &old->mix->master_fx_meters, &chain);
            for (int t = 0; t < plan->mix->track_count; ++t) {
                int previous = plan->mix->previous_tracks[t];
                if (previous >= 0 && fxm_track_snapshot(plan->mix->fxm, t, &chain))
                    source_plan_transfer_fx_meters(&plan->mix->track_fx_meters[t], &old->mix->track_fx_meters[previous], &chain);
            }
        }
        int audition = engine->midi_audition_track_index;
        if (audition >= 0 && audition < old->mix->track_count) {
            int replacement = -1;
            for (int t = 0; t < plan->mix->track_count; ++t) {
                if (plan->mix->previous_tracks[t] == audition) { replacement = t; break; }
            }
            if (replacement < 0) engine_midi_audition_apply_all_off(engine);
            engine->midi_audition_track_index = replacement;
        }
    }
    engine->active_source_plan = plan;
    source_plan_retire(engine, old);
}

// Switches source revisions between blocks and hands retirement back without destroying objects.
void engine_source_plan_apply(Engine* engine) {
    if (!engine) return;
    engine_source_plan_adopt(engine, atomic_exchange_explicit(&engine->pending_source_plan, NULL, memory_order_acq_rel));
}

// Wraps a fixed-format external graph without taking ownership of its borrowed source userdata.
EngineSourcePlan* engine_source_plan_wrap_graph(Engine* engine, EngineGraph* graph) {
    if (!graph) return NULL;
    EngineSourcePlan* plan = NULL;
    if (engine && SDL_ThreadID() == engine->control_thread_id &&
        engine_graph_get_sample_rate(graph) == engine->config.sample_rate &&
        engine_graph_get_channels(graph) == engine_graph_get_channels(engine->graph) &&
        engine_graph_get_max_block(graph) >= engine->config.block_size) {
        plan = calloc(1, sizeof(*plan));
    }
    if (plan && !source_plan_prepare_mix(engine, plan, false)) {
        source_plan_destroy(engine, plan);
        plan = NULL;
    }
    if (plan) { plan->graph = graph; engine->scalar_mix_supported = false; }
    else engine_graph_destroy(graph);
    return plan;
}

// Reclaims a never-published command payload without involving the audio worker.
void engine_source_plan_discard(Engine* engine, EngineSourcePlan* plan) {
    source_plan_destroy(engine, plan);
}

// Returns the active worker graph; the configuration graph remains stable for control-side queries.
EngineGraph* engine_render_source_graph(Engine* engine) {
    return engine && engine->active_source_plan ? engine->active_source_plan->graph : (engine ? engine->graph : NULL);
}

// Exposes only the active revision's independently owned mixer state to the render worker.
EngineMixState* engine_render_mix_state(const Engine* engine) {
    return engine && engine->active_source_plan ? engine->active_source_plan->mix : NULL;
}

// Releases active, pending, and retired plans after worker shutdown and before cache shutdown.
void engine_source_plan_shutdown(Engine* engine) {
    if (!engine) return;
    source_plan_destroy(engine, atomic_exchange_explicit(&engine->pending_source_plan, NULL, memory_order_acq_rel));
    source_plan_destroy(engine, engine->active_source_plan);
    engine->active_source_plan = NULL;
    engine_source_plan_collect(engine);
}

// Preserves the synchronous offline API while live changes publish worker-owned revisions.
void engine_rebuild_sources(Engine* engine) {
    (void)engine_request_rebuild_sources(engine);
}

// Prepares source changes off the worker and makes offline callers observe them immediately.
bool engine_request_rebuild_sources(Engine* engine) {
    if (!engine_source_plan_publish(engine)) return false;
    if (!engine->device_started || !engine->worker_thread) {
        engine_source_plan_apply(engine);
        engine_source_plan_collect(engine);
    }
    return true;
}

// Captures authored project state without touching live publications, histories, or observation taps.
EngineSourcePlan* engine_source_plan_prepare_export(Engine* engine, uint64_t end, uint64_t tail) {
    if (!engine_is_control_thread(engine) || !end || tail > UINT64_MAX - end) return NULL;
    EngineSourcePlan* plan = source_plan_build(engine, true, end, tail);
    if (plan) {
        engine_graph_reset(plan->graph);
        fxm_reset_render_state(plan->mix->fxm);
        engine_eq_reset(&plan->mix->master_eq);
        for (int t = 0; t < plan->mix->track_count; ++t) engine_eq_reset(&plan->mix->tracks[t].track_eq);
    }
    return plan;
}

// Returns the prepared snapshot's fixed serial-chain latency without consulting the live renderer.
uint64_t engine_source_plan_export_latency(EngineSourcePlan* plan) {
    if (!plan) return 0;
    fxm_begin_render_block(plan->mix->fxm, engine_graph_get_max_block(plan->graph));
    return fxm_processing_latency(plan->mix->fxm);
}

// Renders one export block through the same mixer with live telemetry disabled.
bool engine_source_plan_render_export(EngineSourcePlan* plan, uint64_t start, int frames,
                                     float* output, float* scratch, int channels) {
    return plan && engine_mix_prepared(NULL, plan->graph, plan->mix, start, frames, output, scratch, channels);
}

// Publishes a complete scalar snapshot through the same ordered slot as structural revisions.
bool engine_request_mixer_update(Engine* engine) {
    if (!engine_is_control_thread(engine)) return false;
    if (!engine->scalar_mix_supported) return engine_request_rebuild_sources(engine);
    engine_source_plan_collect(engine);
    EngineSourcePlan* update = calloc(1, sizeof(*update));
    if (!update) return false;
    update->scalar_count = engine->track_count;
    update->scalars = calloc(engine->track_count ? (size_t)engine->track_count : 1, sizeof(*update->scalars));
    if (!update->scalars) { source_plan_destroy(engine, update); return false; }
    bool source_solo = false, audition_solo = false;
    int armed = atomic_load(&engine->record_armed_track_index);
    for (int t = 0; t < engine->track_count; ++t) {
        const EngineTrack* track = &engine->tracks[t];
        if (!track->muted && track->solo) {
            if (t == armed || (track->active && track->clip_count > 0)) source_solo = true;
            if (track->active) audition_solo = true;
        }
    }
    for (int t = 0; t < engine->track_count; ++t) {
        const EngineTrack* track = &engine->tracks[t];
        update->scalars[t] = (EngineScalarTrack){
            .identity = track->runtime_id, .gain = track->gain, .pan = track->pan,
            .muted = track->muted, .solo = track->solo,
            .source_gain = track->active && !track->muted && (!source_solo || track->solo) ? track->gain : 0,
            .audition_gain = track->active && !track->muted && (!audition_solo || track->solo) ? track->gain : 0};
    }
    // Taking the pending slot gives control exclusive ownership; a worker can only take it before or after us.
    EngineSourcePlan* pending = atomic_exchange_explicit(&engine->pending_source_plan, NULL, memory_order_acq_rel);
    if (pending && !pending->scalars) {
        // Preserve a not-yet-adopted structural capture and amend its scalar targets before republishing.
        if (!source_plan_apply_scalars(pending, update, 0)) {
            atomic_store_explicit(&engine->pending_source_plan, pending, memory_order_release);
            source_plan_destroy(engine, update);
            return false;
        }
        source_plan_destroy(engine, update);
        update = pending;
    } else source_plan_destroy(engine, pending);
    atomic_store_explicit(&engine->pending_source_plan, update, memory_order_release);
    if (!engine->device_started || !engine->worker_thread) {
        engine_source_plan_apply(engine);
        engine_source_plan_collect(engine);
    }
    return true;
}
