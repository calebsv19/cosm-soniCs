#include "engine/engine_internal.h"
#include "engine/hat_choke.h"
#include "test_assert.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x, m) daw_test_expect("track_hat_choke_test", (x), (m))

// Adds a region with explicit hat settings through normal model/source publication.
static int region(Engine* engine, int track, uint64_t start, uint64_t length, uint64_t hit, bool open) {
    int clip;
    CHECK(engine_add_midi_clip_to_track(engine, track, start, length, &clip), "create MIDI region");
    EngineInstrumentPresetId preset = open ? ENGINE_INSTRUMENT_PRESET_OPEN_HAT : ENGINE_INSTRUMENT_PRESET_CLOSED_HAT;
    CHECK(engine_clip_midi_set_instrument_preset(engine, track, clip, preset), "hat voice");
    EngineInstrumentParams p = engine_instrument_default_params(preset); p.hat_choke = 1; p.noise_decay_ms = open ? 900 : 50;
    CHECK(engine_clip_midi_set_instrument_params(engine, track, clip, p), "opt-in group");
    EngineMidiNote note = {hit, 48, open ? 46 : 42, .8f};
    CHECK(engine_clip_midi_set_notes(engine, track, clip, &note, 1), "hat note");
    return clip;
}

// Captures an isolated region while preserving its full track choke context.
static EngineSourcePlan* capture(Engine* engine, int track, int clip, uint64_t end, uint64_t tail) {
    EngineBounceOptions options = engine_bounce_options_default();
    options.isolate_track = true; options.track_index = track; options.clip_id = engine->tracks[track].clips[clip].creation_index;
    options.bypass_master_fx = true; options.bypass_track_fx = true;
    EngineSourcePlan* plan = engine_source_plan_prepare_export_filtered(engine, end, tail, &options);
    CHECK(plan != NULL, "capture production source/mix graph");
    return plan;
}

// Renders a private snapshot with independent source and scratch buffers.
static void render(EngineSourcePlan* plan, uint64_t at, int frames, float* output) {
    float* scratch = calloc((size_t)frames * 2, sizeof(float));
    memset(output, 0, (size_t)frames * 2 * sizeof(float));
    CHECK(scratch && engine_source_plan_render_export(plan, at, frames, output, scratch, 2), "export block");
    free(scratch);
}

// Measures exact silence independently of any chosen noise sample's accidental zero crossing.
static double energy(const float* output, int frames) {
    double sum = 0;
    for (int i = 0; i < frames * 2; ++i) { CHECK(isfinite(output[i]), "finite samples"); sum += output[i] * output[i]; }
    return sum;
}

// Verifies adjacent region spill, scope, timing edits, captured lifetime and excluded export triggers.
static void track_routing(int rate) {
    EngineRuntimeConfig config; config_set_defaults(&config); config.sample_rate = rate; config.block_size = 128;
    Engine* engine = engine_create(&config); CHECK(engine != NULL, "engine");
    int open = region(engine, 0, 0, rate / 4, rate / 4 - 1000, true);
    int closed = region(engine, 0, rate / 4, rate / 2, 1000, false);
    uint64_t choke = rate / 4 + 1000;
    float sample[256];
    EngineSourcePlan* original = capture(engine, 0, open, rate, 0);
    render(original, choke - 2000, 128, sample); CHECK(energy(sample, 128) > 1e-7, "open hat before choke");
    render(original, choke, 128, sample); CHECK(energy(sample, 128) == 0, "adjacent region cuts open tail exactly");
    CHECK(engine_clip_midi_set_instrument_param(engine, 0, closed, ENGINE_INSTRUMENT_PARAM_HAT_CHOKE, 0), "disable trigger participation");
    EngineSourcePlan* disabled = capture(engine, 0, open, rate, 0);
    render(disabled, choke, 128, sample); CHECK(energy(sample, 128) > 1e-7, "open tail crosses region edge when trigger disabled");
    render(original, choke, 128, sample); CHECK(energy(sample, 128) == 0, "captured plan survives later edits");
    engine_source_plan_discard(engine, disabled);
    CHECK(engine_clip_midi_set_instrument_param(engine, 0, closed, ENGINE_INSTRUMENT_PARAM_HAT_CHOKE, 1), "reenable trigger");
    EngineSourcePlan* boundary = capture(engine, 0, open, choke, rate / 2);
    render(boundary, choke, 128, sample); CHECK(energy(sample, 128) > 1e-7, "hit exactly at export end cannot choke release tail");
    engine_source_plan_discard(engine, boundary);
    CHECK(engine_add_track(engine) == 1, "independent destination"); int moved;
    CHECK(engine_move_clip_to_track(engine, 0, closed, 1, rate / 4, &moved), "move trigger off track");
    EngineSourcePlan* separate = capture(engine, 0, open, rate, 0);
    render(separate, choke, 128, sample); CHECK(energy(sample, 128) > 1e-7, "other tracks do not choke");
    engine_source_plan_discard(engine, separate);
    CHECK(engine_move_clip_to_track(engine, 1, moved, 0, rate / 4, &closed), "move trigger back");
    EngineMidiNote delayed = {rate / 4, 48, 42, .8f};
    CHECK(engine_clip_midi_set_notes(engine, 0, closed, &delayed, 1), "retime closed note");
    EngineSourcePlan* retimed = capture(engine, 0, open, rate, 0);
    render(retimed, choke, 128, sample); CHECK(energy(sample, 128) > 1e-7, "retime rebuilds shared fences");
    render(retimed, rate / 2, 128, sample); CHECK(energy(sample, 128) == 0, "new trigger position cuts tail");
    engine_source_plan_discard(engine, retimed);
    CHECK(engine_remove_clip(engine, 0, closed), "delete trigger");
    EngineSourcePlan* removed = capture(engine, 0, open, rate, 0);
    render(removed, rate / 2, 128, sample); CHECK(energy(sample, 128) > 1e-7, "deletion removes fence");
    engine_source_plan_discard(engine, removed); engine_source_plan_discard(engine, original); engine_destroy(engine);
}

// Proves deterministic source partitions, seeks, simulated loop wraps and simultaneous region tie ordering.
static void deterministic(void) {
    EngineRuntimeConfig config; config_set_defaults(&config); config.sample_rate = 48000; config.block_size = 128;
    Engine* engine = engine_create(&config); CHECK(engine != NULL, "engine");
    int first = region(engine, 0, 0, 24000, 10000, true);
    int last = region(engine, 0, 0, 24000, 10000, false);
    EngineSourcePlan* earlier = capture(engine, 0, first, 24000, 0); float sample[256];
    render(earlier, 10000, 128, sample); CHECK(energy(sample, 128) == 0, "later persistent clip identity wins simultaneous hits");
    engine_source_plan_discard(engine, earlier);
    CHECK(engine_remove_clip(engine, 0, last), "remove tie winner");
    EngineMidiNote notes[] = {{16000, 48, 46, .6f}, {10000, 48, 46, .8f}};
    CHECK(engine_clip_midi_set_notes(engine, 0, first, notes, 2), "unsorted notes");
    engine_source_plan_apply(engine);
    EngineGraph* graph = engine_render_source_graph(engine);
    float* whole = calloc(48000 * 2, sizeof(float)); float* split = calloc(48000 * 2, sizeof(float)); CHECK(whole && split, "buffers");
    engine_graph_render_track(graph, whole, 48000, 0, 0);
    for (int at = 0; at < 48000; at += 127) engine_graph_render_track(graph, split + at * 2, 48000 - at < 127 ? 48000 - at : 127, at, 0);
    CHECK(!memcmp(whole, split, 48000 * 2 * sizeof(float)), "whole/block byte parity including natural spill");
    for (int pass = 0; pass < 3; ++pass) {
        memset(sample, 0, sizeof(sample)); engine_graph_render_track(graph, sample, 128, pass == 1 ? 15980 : 10000, 0);
        CHECK(!memcmp(sample, whole + (pass == 1 ? 15980 : 10000) * 2, sizeof(sample)), "seek/loop wrap byte parity");
    }
    EngineHatChokeTimeline timeline = {0};
    engine->tracks[0].clips[first].active = false;
    CHECK(engine_hat_choke_prepare(&timeline, engine, 0, UINT64_MAX) && timeline.count == 0, "inactive regions excluded"); engine_hat_choke_free(&timeline);
    engine->tracks[0].clips[first].active = true;
    EngineMidiNote invalid[] = {{23999, 1, 42, .8f}, {100, 48, 42, 0}, {101, 1, 42, .8f}};
    CHECK(engine_clip_midi_set_notes(engine, 0, first, invalid, 3), "silent and out-of-region test notes");
    for (int n = 0; n < 3; ++n) {
        EngineMidiNote* note = &engine->tracks[0].clips[first].midi_notes.notes[n];
        if (note->start_frame == 23999) note->start_frame = 24000;
        if (note->start_frame == 101) note->duration_frames = 0;
    }
    CHECK(engine_hat_choke_prepare(&timeline, engine, 0, UINT64_MAX) && timeline.count == 0, "silent and region-external onsets excluded"); engine_hat_choke_free(&timeline);
    free(whole); free(split); engine_destroy(engine);
}

// Exercises the production graph and immutable preparation contract at supported sample rates.
int main(void) { track_routing(44100); track_routing(48000); track_routing(96000); deterministic(); puts("track_hat_choke_test: success"); return 0; }
