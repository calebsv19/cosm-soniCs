#include "input/effects_panel_input.h"
#include "ui/layout.h"
#include "ui/effects_panel_meter_detail.h"
#include "engine/engine_internal.h"
#include "app_state.h"
#include "input/timeline_drag.h"
#include "input/inspector_input_numeric_edit.h"
#include "input/inspector_input.h"
#include "input/input_manager.h"
#include "input/inspector_fade_input.h"
#include "input/timeline/timeline_midi_trim.h"
#include "input/timeline_selection.h"
#include "test_assert.h"
#include <math.h>
#include <string.h>
#include "audio/wav_writer.h"
#include <unistd.h>
#include <stdlib.h>

#include "input/effects_panel_track_snapshot.h"
#include "input/effects_panel_eq_detail_input.h"
#include "input/effects_panel_input_helpers.h"

#define CHECK(v, msg) daw_test_expect("engine_parameter_transaction_test", (v), (msg))
static bool reject_publication;
static int publication_attempts;
// Injects a preparation rejection before any render revision can be published.
static bool publish_or_reject(Engine* engine) {
    ++publication_attempts;
    return !reject_publication && engine_request_rebuild_sources(engine);
}
// Rejects lightweight publication at the same transactional boundary as structural preparation.
static bool mixer_or_reject(Engine* engine) {
    ++publication_attempts;
    return !reject_publication && engine_request_mixer_update(engine);
}
static int capacity_allocation_fail, capacity_allocation_count;
static int scope_ring_fail, scope_ring_count;
static int automation_malloc_fail, automation_malloc_count;
static bool reject_midi_allocation;
// Rejects note-list allocation without changing the existing list.
static void* midi_test_realloc(void* ptr, size_t size) {
    return reject_midi_allocation ? NULL : realloc(ptr, size);
}
// Rejects point-storage allocations in the included automation implementation.
static void* automation_test_malloc(size_t bytes) {
    if (++automation_malloc_count == automation_malloc_fail) return NULL;
    return malloc(bytes);
}
// Injects each capacity-array allocation failure without altering production allocation APIs.
static void* capacity_calloc(size_t count, size_t size) {
    if (++capacity_allocation_count == capacity_allocation_fail) return NULL;
    return calloc(count, size);
}
// Injects failure at each newly constructed scope ring while preserving normal queue allocation.
static bool capacity_ring_init(RingBuffer* ring, size_t bytes) {
    if (++scope_ring_count == scope_ring_fail) return false;
    return ringbuf_init(ring, bytes);
}
#define calloc capacity_calloc
#define engine_request_rebuild_sources publish_or_reject
#define engine_request_mixer_update mixer_or_reject
#include "../src/engine/engine_tracks.c"
#include "../src/engine/engine_fx.c"
#include "../src/engine/engine_clips.c"
#include "../src/engine/engine_clip_history.c"
#include "../src/engine/engine_clips_midi.c"
#define malloc automation_test_malloc
#include "../src/engine/automation.c"
#define realloc midi_test_realloc
#include "../src/engine/midi.c"
#undef realloc
#include "../src/engine/sampler.c"
#include "../src/engine/engine_clips_automation.c"
#include "../src/engine/engine_clips_no_overlap.c"
#undef malloc
#define ringbuf_init capacity_ring_init
#include "../src/engine/engine_scope_host.c"
#undef ringbuf_init
#undef calloc
#undef engine_request_rebuild_sources
#undef engine_request_mixer_update

#include "input/timeline/timeline_clip_helpers.h"
#include "input/timeline/timeline_clipboard.h"
static int clipboard_copy_fail, clipboard_copy_count;
// Injects a failed copied entry after its owned notes/automation have been prepared.
static bool clipboard_capture_or_reject(const EngineClip* clip, SessionClip* out) {
    bool copied = timeline_session_clip_from_engine(clip, out);
    return copied && ++clipboard_copy_count != clipboard_copy_fail;
}
#define calloc capacity_calloc
#define timeline_session_clip_from_engine clipboard_capture_or_reject
#define timeline_clipboard_copy tested_clipboard_copy
#define timeline_clipboard_paste tested_clipboard_paste
#include "../src/input/timeline/timeline_clipboard.c"
#undef timeline_clipboard_paste
#undef timeline_clipboard_copy
#undef timeline_session_clip_from_engine
#undef calloc


// Fails every capacity-array and new scope-ring allocation, then checks row-stride preservation on retry.
static void test_capacity_transactions(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* engine = engine_create(&config);
    CHECK(engine != NULL, "capacity fixture");
    int old_capacity = engine->track_capacity;
    EngineTrack* tracks = engine->tracks;
    EngineMeterSnapshot* snapshots = engine->track_meter_snapshots;
    EngineFxScopeBank* scopes = engine->scope_host.tracks;
    uint64_t identity = tracks[0].runtime_id;
    snapshots[0].runtime_id = 11;
    snapshots[old_capacity].runtime_id = 22;
    engine->track_fx_meter_snapshots[0].runtime_id = 31;
    engine->track_fx_meter_snapshots[old_capacity].runtime_id = 32;
    for (int row = 0; row < 2; ++row) {
        EngineFxMeterSnapshotBank* bank = &engine->track_fx_meter_snapshots[row * old_capacity];
        bank->count = FX_MASTER_MAX;
        for (int i = 0; i < FX_MASTER_MAX; ++i) {
            bank->taps[i].id = (FxInstId)(100 + i);
            bank->taps[i].snapshot.valid = true;
            bank->taps[i].snapshot.peak = (float)(row + i);
        }
    }
    float scalar = 3.5f;
    CHECK(ringbuf_write_exact(&scopes[0].taps[0].buffer, &scalar, sizeof(scalar)), "scope capacity fixture");
    for (int allocation = 1; allocation <= 7; ++allocation) {
        capacity_allocation_count = 0;
        capacity_allocation_fail = allocation;
        CHECK(!engine_ensure_track_capacity(engine, old_capacity + 1), "array allocation failure ignored");
        CHECK(engine->tracks == tracks && engine->track_meter_snapshots == snapshots &&
              engine->scope_host.tracks == scopes && engine->track_capacity == old_capacity &&
              engine->track_meter_capacity == old_capacity && engine->track_fx_meter_capacity == old_capacity,
              "failed capacity growth changed storage");
    }
    capacity_allocation_fail = 0;
    for (int ring = 1; ring <= old_capacity * FX_MASTER_MAX; ++ring) {
        scope_ring_count = 0;
        scope_ring_fail = ring;
        CHECK(!engine_ensure_track_capacity(engine, old_capacity + 1), "scope ring failure ignored");
        CHECK(engine->tracks == tracks && engine->scope_host.tracks == scopes &&
              engine->scope_host.track_capacity == old_capacity && engine->track_capacity == old_capacity,
              "partial scope bank escaped failed preparation");
    }
    scope_ring_fail = 0;
    CHECK(engine_ensure_track_capacity(engine, old_capacity + 1), "capacity retry");
    int capacity = engine->track_capacity;
    CHECK(capacity == old_capacity * 2 && engine->tracks[0].runtime_id == identity, "capacity identity");
    CHECK(engine->track_meter_snapshots[0].runtime_id == 11 &&
          engine->track_meter_snapshots[capacity].runtime_id == 22 &&
          engine->track_fx_meter_snapshots[0].runtime_id == 31 &&
          engine->track_fx_meter_snapshots[capacity].runtime_id == 32, "snapshot second-row stride lost");
    for (int row = 0; row < 2; ++row) {
        const EngineFxMeterSnapshotBank* bank = &engine->track_fx_meter_snapshots[row * capacity];
        CHECK(bank->count == FX_MASTER_MAX, "FX count lost during growth");
        for (int i = 0; i < FX_MASTER_MAX; ++i) {
            CHECK(bank->taps[i].id == (FxInstId)(100 + i) && bank->taps[i].snapshot.valid &&
                  bank->taps[i].snapshot.peak == (float)(row + i), "FX payload lost during growth");
        }
    }
    float result = 0;
    CHECK(ringbuf_read_exact(&engine->scope_host.tracks[0].taps[0].buffer, &result, sizeof(result)) && result == scalar,
          "scope history lost across capacity growth");
    engine_destroy(engine);
}

// Exercises split, trim, shift, removal, and newer-clip priority in one atomic overlap transaction.
static void test_overlap_transaction(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* engine = engine_create(&config);
    float samples[1024];
    for (int i = 0; i < 1024; ++i) samples[i] = 0.25f;
    char path[] = "/tmp/daw-overlap-transaction-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0, "overlap fixture path");
    close(fd);
    CHECK(engine && wav_write_f32(path, samples, 1024, 1, config.sample_rate), "overlap fixture media");
    uint64_t starts[] = {0, 0, 192, 160, 128, 140};
    uint64_t lengths[] = {512, 192, 192, 40, 128, 60};
    uint64_t ids[6];
    EngineSamplerSource* anchor = NULL;
    for (int i = 0; i < 6; ++i) {
        int index;
        CHECK(engine_add_clip_to_track(engine, 0, path, starts[i], &index), "overlap fixture clip");
        CHECK(engine_clip_set_region(engine, 0, index, 0, lengths[i]), "overlap fixture trim");
        CHECK(engine_clip_set_fades(engine, 0, index, 0, 0), "overlap fixture fades");
        if (i == 0) CHECK(engine_clip_add_automation_point(engine, 0, index,
                          ENGINE_AUTOMATION_TARGET_VOLUME, 32, 0.25f, NULL), "overlap automation fixture");
        ids[i] = engine->tracks[0].clips[index].creation_index;
        if (i == 4) anchor = engine->tracks[0].clips[index].sampler;
    }
    int history_track = 0;
    EngineClipContentSnapshot* content_before = engine_clip_content_capture(engine, &history_track, 1);
    CHECK(content_before != NULL, "overlap content before capture");
    EngineClip* original = engine->tracks[0].clips;
    EngineMixState* revision = engine_render_mix_state(engine);
    float before[256], after[256];
    engine_graph_render_track(engine_render_source_graph(engine), before, 128, 0, 0);
    CHECK(fabsf(before[64]) > 0.01f, "overlap baseline silent");
    int output = 6543;
    reject_publication = true;
    capacity_allocation_count = automation_malloc_count = publication_attempts = 0;
    CHECK(!engine_track_apply_no_overlap(engine, 0, anchor, &output), "overlap ignored publication rejection");
    int allocations = capacity_allocation_count, point_allocations = automation_malloc_count;
    CHECK(publication_attempts == 1, "overlap attempted intermediate publications");
    CHECK(engine->tracks[0].clips == original && engine->tracks[0].clip_count == 6 && output == 6543 &&
          engine_render_mix_state(engine) == revision, "rejected overlap changed project or revision");
    for (int i = 0; i < 6; ++i) {
        int index = engine_track_find_clip_by_creation_index(&engine->tracks[0], ids[i]);
        CHECK(index >= 0 && engine->tracks[0].clips[index].duration_frames == lengths[i] &&
              engine->tracks[0].clips[index].timeline_start_frames == starts[i], "rejected overlap partially applied");
    }
    engine_graph_render_track(engine_render_source_graph(engine), after, 128, 0, 0);
    CHECK(!memcmp(before, after, sizeof(before)), "rejected overlap changed samples");
    reject_publication = false;
    for (int allocation = 1; allocation <= allocations; ++allocation) {
        capacity_allocation_count = 0;
        capacity_allocation_fail = allocation;
        CHECK(!engine_track_apply_no_overlap(engine, 0, anchor, &output), "overlap ignored allocation rejection");
        CHECK(engine->tracks[0].clips == original && output == 6543 && engine_render_mix_state(engine) == revision,
              "failed overlap allocation changed ownership");
    }
    capacity_allocation_fail = 0;
    for (int allocation = 1; allocation <= point_allocations; ++allocation) {
        automation_malloc_count = 0;
        automation_malloc_fail = allocation;
        CHECK(!engine_track_apply_no_overlap(engine, 0, anchor, &output), "overlap ignored point-copy rejection");
        CHECK(engine->tracks[0].clips == original && output == 6543, "failed overlap point-copy changed ownership");
    }
    automation_malloc_fail = 0;
    publication_attempts = 0;
    CHECK(engine_track_apply_no_overlap(engine, 0, anchor, &output), "overlap retry failed");
    CHECK(publication_attempts == 1, "accepted overlap published more than once");
    EngineTrack* track = &engine->tracks[0];
    CHECK(track->clip_count == 6 && track->clips[output].sampler == anchor, "overlap count/anchor incorrect");
    CHECK(engine_track_find_clip_by_creation_index(track, ids[3]) < 0, "covered clip survived");
    int split = engine_track_find_clip_by_creation_index(track, ids[0]);
    int trim = engine_track_find_clip_by_creation_index(track, ids[1]);
    int shift = engine_track_find_clip_by_creation_index(track, ids[2]);
    int newer = engine_track_find_clip_by_creation_index(track, ids[5]);
    CHECK(split >= 0 && trim >= 0 && shift >= 0 && newer >= 0, "surviving overlap identity missing");
    CHECK(track->clips[split].duration_frames == 128 && track->clips[trim].duration_frames == 128,
          "left overlap regions incorrect");
    CHECK(track->clips[shift].timeline_start_frames == 256 && track->clips[shift].offset_frames == 64 &&
          track->clips[shift].duration_frames == 128, "shifted overlap region incorrect");
    CHECK(track->clips[newer].timeline_start_frames == 140 && track->clips[newer].duration_frames == 60,
          "newer clip priority changed");
    bool right_found = false;
    for (int i = 0; i < track->clip_count; ++i)
        if (track->clips[i].creation_index > ids[5] && track->clips[i].timeline_start_frames == 256 &&
            track->clips[i].offset_frames == 256 && track->clips[i].duration_frames == 256) right_found = true;
    CHECK(right_found, "split right region missing");
    EngineClipContentSnapshot* content_after = engine_clip_content_capture(engine, &history_track, 1);
    CHECK(content_after && engine_clip_content_retain(content_before), "overlap content after capture");
    engine_clip_content_release(content_before);
    original = track->clips; revision = engine_render_mix_state(engine);
    reject_publication = true; capacity_allocation_count = 0;
    CHECK(!engine_clip_content_restore(engine, content_before), "history ignored publication rejection");
    int restore_allocations = capacity_allocation_count;
    CHECK(track->clips == original && engine_render_mix_state(engine) == revision, "history restore partially applied");
    reject_publication = false;
    for (int allocation = 1; allocation <= restore_allocations; ++allocation) {
        capacity_allocation_count = 0; capacity_allocation_fail = allocation;
        CHECK(!engine_clip_content_restore(engine, content_before), "history ignored allocation failure");
        CHECK(track->clips == original && engine_render_mix_state(engine) == revision, "history allocation changed project");
    }
    capacity_allocation_fail = 0;
    CHECK(unlink(path) == 0, "remove source before history restore");
    publication_attempts = 0;
    CHECK(engine_clip_content_restore(engine, content_before) && publication_attempts == 1, "history restore reopened source or split publication");
    for (int i = 0; i < 6; ++i) {
        int c = engine_track_find_clip_by_creation_index(track, ids[i]);
        CHECK(c >= 0 && track->clips[c].timeline_start_frames == starts[i] && track->clips[c].duration_frames == lengths[i],
              "history lost covered or trimmed neighbor");
    }
    engine_graph_render_track(engine_render_source_graph(engine), after, 128, 0, 0);
    CHECK(!memcmp(before, after, sizeof(before)), "content history did not restore rendered samples");
    int restored_automation = engine_track_find_clip_by_creation_index(track, ids[0]);
    CHECK(track->clips[restored_automation].automation_lane_count == 1, "history lost neighbor automation");
    CHECK(engine_clip_content_restore(engine, content_after) && engine_track_find_clip_by_creation_index(track, ids[3]) < 0,
          "history redo lost overlap removal");
    engine_clip_content_release(content_after);

    engine_destroy(engine);
    CHECK(!engine_clip_content_retain(content_before), "destroyed project retained live history");
    engine_clip_content_release(content_before);
    unlink(path);
}

// Verifies combined placement/content edits remain indivisible for audio and MIDI, including new tracks.
static void test_complete_clip_transform(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* engine = engine_create(&config);
    CHECK(engine && engine_add_track(engine) == 1, "complete transform fixture");
    CHECK(engine_add_midi_clip_to_track(engine, 0, 0, 4096, NULL), "complete transform MIDI");
    EngineMidiNote note = {.duration_frames = 4096, .note = 60, .velocity = 0.5f};
    CHECK(engine_clip_midi_add_note(engine, 0, 0, note, NULL), "complete transform note");
    float samples[128];
    for (int i = 0; i < 128; ++i) samples[i] = 0.25f;
    char path[] = "/tmp/daw-complete-transform-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0, "complete transform fixture path");
    close(fd);
    CHECK(wav_write_f32(path, samples, 128, 1, config.sample_rate), "complete transform fixture audio");
    CHECK(engine_add_clip_to_track(engine, 0, path, 8192, NULL), "complete transform audio");
    EngineClip* original_array = engine->tracks[0].clips;
    EngineClip original_midi = original_array[0], original_audio = original_array[1];
    EngineMixState* revision = engine_render_mix_state(engine);
    EngineClipTransform audio = {.start_frame = 4096, .offset_frames = 16, .duration_frames = 64,
        .gain = 0, .fade_in_frames = 8, .fade_out_frames = 8};
    note.duration_frames = 1024;
    note.note = 72;
    EngineClipTransform midi = {.start_frame = 16384, .duration_frames = 1024, .gain = 0.5f,
        .midi_notes = &note, .midi_note_count = 1, .instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE,
        .instrument_params = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE)};
    float before[256], after[256];
    engine_sampler_source_render(original_audio.sampler, before, 128, 8192);
    int output = 9876;
    reject_publication = true;
    publication_attempts = 0;
    CHECK(!engine_transform_clip(engine, 0, 1, 0, &audio, &output), "same-track complete transform ignored rejection");
    CHECK(publication_attempts == 1, "complete transform published intermediate edits");
    CHECK(!engine_transform_clip(engine, 0, 1, 1, &audio, &output), "audio transform/transfer ignored rejection");
    CHECK(!engine_transform_clip(engine, 0, 0, 0, &midi, &output), "MIDI complete transform ignored rejection");
    CHECK(!engine_transform_clip(engine, 0, 0, 3, &midi, &output), "MIDI complete transform/new track ignored rejection");
    CHECK(engine->tracks[0].clips == original_array && engine->track_count == 2 && engine->tracks[1].clip_count == 0 &&
          original_array[0].midi_notes.notes == original_midi.midi_notes.notes && original_array[0].midi_notes.notes[0].note == 60 &&
          original_array[0].duration_frames == 4096 && original_array[1].gain == original_audio.gain &&
          original_array[1].timeline_start_frames == 8192 && original_array[1].offset_frames == 0 &&
          engine_render_mix_state(engine) == revision && output == 9876, "complete transform partially applied");
    engine_sampler_source_render(original_audio.sampler, after, 128, 8192);
    CHECK(!memcmp(before, after, sizeof(before)), "complete transform rejection changed sampler timing");
    reject_publication = false;
    reject_midi_allocation = true;
    CHECK(!engine_transform_clip(engine, 0, 0, 0, &midi, &output), "complete transform note allocation failure ignored");
    reject_midi_allocation = false;
    for (int allocation = 1; allocation <= 2; ++allocation) {
        capacity_allocation_count = 0;
        capacity_allocation_fail = allocation;
        CHECK(!engine_transform_clip(engine, 0, 0, 0, &midi, &output), "complete transform descriptor failure ignored");
        CHECK(engine->tracks[0].clips == original_array && original_array[0].midi_notes.notes == original_midi.midi_notes.notes,
              "complete transform allocation failure changed ownership");
    }
    capacity_allocation_fail = 0;
    publication_attempts = 0;
    CHECK(engine_transform_clip(engine, 0, 1, 0, &audio, &output), "complete audio transform retry");
    CHECK(publication_attempts == 1 && engine->tracks[0].clips[output].gain == 0 &&
          engine->tracks[0].clips[output].duration_frames == 64 && engine->tracks[0].clips[output].offset_frames == 16 &&
          engine->tracks[0].clips[output].creation_index == original_audio.creation_index,
          "complete audio transform retry incomplete");
    engine_graph_render_track(engine_render_source_graph(engine), after, 128, 4096, 0);
    for (int i = 0; i < 256; ++i) CHECK(after[i] == 0, "complete audio transform gain not rendered");
    CHECK(engine_transform_clip(engine, 0, 0, 3, &midi, &output), "complete MIDI transform retry");
    CHECK(engine->track_count == 4 && engine->tracks[0].clip_count == 1 &&
          engine->tracks[3].clips[output].creation_index == original_midi.creation_index &&
          engine->tracks[3].clips[output].duration_frames == 1024 && engine->tracks[3].clips[output].midi_notes.notes[0].note == 72,
          "complete MIDI transform retry incomplete");
    engine_graph_render_track(engine_render_source_graph(engine), after, 128, 16384, 3);
    float peak = 0;
    for (int i = 0; i < 256; ++i) if (fabsf(after[i]) > peak) peak = fabsf(after[i]);
    CHECK(peak > 0.001f, "accepted MIDI transform silent");
    engine_destroy(engine);
    unlink(path);
}

// Verifies a complete mixer/instrument restore publishes once or retains every previous field.
static void test_track_settings_transaction(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* engine = engine_create(&config);
    CHECK(engine != NULL, "track settings fixture");
    EngineTrack previous = engine->tracks[0];
    EngineMixState* revision = engine_render_mix_state(engine);
    EngineTrackSettings settings = {.gain = 0.25f, .pan = 0.75f, .muted = true, .solo = true,
        .instrument_enabled = true, .instrument_preset = ENGINE_INSTRUMENT_PRESET_SOFT_PAD,
        .instrument_params = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_SOFT_PAD)};
    reject_publication = true;
    publication_attempts = 0;
    CHECK(!engine_track_set_settings(engine, 0, &settings), "track snapshot ignored rejection");
    CHECK(publication_attempts == 1 && engine->tracks[0].gain == previous.gain &&
          engine->tracks[0].pan == previous.pan && engine->tracks[0].muted == previous.muted &&
          engine->tracks[0].solo == previous.solo && engine->tracks[0].midi_instrument_enabled == previous.midi_instrument_enabled &&
          engine->tracks[0].midi_instrument_preset == previous.midi_instrument_preset &&
          engine_instrument_params_equal(engine->tracks[0].midi_instrument_params, previous.midi_instrument_params) &&
          engine_render_mix_state(engine) == revision, "track snapshot partially applied");
    reject_publication = false;
    publication_attempts = 0;
    CHECK(engine_track_set_settings(engine, 0, &settings), "track snapshot retry");
    CHECK(publication_attempts == 1 && engine->tracks[0].gain == 0.25f && engine->tracks[0].pan == 0.75f &&
          engine->tracks[0].muted && engine->tracks[0].solo && engine->tracks[0].midi_instrument_enabled &&
          engine->tracks[0].midi_instrument_preset == ENGINE_INSTRUMENT_PRESET_SOFT_PAD,
          "track snapshot retry incomplete");
    engine_destroy(engine);
}

// Verifies MIDI note and instrument metadata rejection preserves both control and render state.
static void test_midi_transactions(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* engine = engine_create(&config);
    CHECK(engine && engine_add_midi_clip_to_track(engine, 0, 0, 4096, NULL), "MIDI transaction fixture");
    EngineMidiNote note = {.duration_frames = 2048, .note = 60, .velocity = 0.5f};
    CHECK(engine_clip_midi_add_note(engine, 0, 0, note, NULL), "MIDI initial note");
    EngineClip* clip = &engine->tracks[0].clips[0];
    EngineAutomationPoint track_points[] = {{0, 0.25f}, {4096, 0.25f}};
    CHECK(engine_track_midi_set_instrument_automation_lane_points(engine, 0,
          ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL, track_points, 2), "initial track automation");
    EngineClip previous = *clip;
    EngineTrack previous_track = engine->tracks[0];
    EngineMixState* revision = engine_render_mix_state(engine);
    float before[256], after[256];
    engine_graph_render_track(engine_render_source_graph(engine), before, 128, 0, 0);
    float peak = 0;
    for (int i = 0; i < 256; ++i) if (fabsf(before[i]) > peak) peak = fabsf(before[i]);
    CHECK(peak > 0.001f, "MIDI transaction baseline silent");
    note.note = 72;
    int output = 5432;
    EngineInstrumentParams params = engine_track_midi_instrument_params(engine, 0);
    params.level = 0;
    reject_publication = true;
    CHECK(!engine_clip_midi_add_note(engine, 0, 0, note, &output), "MIDI add ignored rejection");
    CHECK(!engine_clip_midi_update_note(engine, 0, 0, 0, note, &output), "MIDI update ignored rejection");
    CHECK(!engine_clip_midi_remove_note(engine, 0, 0, 0), "MIDI remove ignored rejection");
    CHECK(!engine_clip_midi_set_notes(engine, 0, 0, &note, 1), "MIDI replacement ignored rejection");
    CHECK(!engine_clip_midi_set_notes(engine, 0, 0, NULL, 0), "MIDI clear ignored rejection");
    CHECK(!engine_track_midi_set_instrument_enabled(engine, 0, !previous_track.midi_instrument_enabled), "track enable ignored rejection");
    CHECK(!engine_track_midi_set_instrument_preset(engine, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE + 1), "track preset ignored rejection");
    CHECK(!engine_track_midi_set_instrument_params(engine, 0, params), "track params ignored rejection");
    CHECK(!engine_clip_midi_set_inherits_track_instrument(engine, 0, 0, !previous.instrument_inherits_track), "clip inheritance ignored rejection");
    CHECK(!engine_clip_midi_set_instrument_preset(engine, 0, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE + 1), "clip preset ignored rejection");
    CHECK(!engine_clip_midi_set_instrument_params(engine, 0, 0, params), "clip params ignored rejection");
    CHECK(!engine_clip_midi_set_instrument_param(engine, 0, 0, ENGINE_INSTRUMENT_PARAM_LEVEL, 0), "clip param ignored rejection");
    CHECK(!engine_track_midi_set_instrument_automation_lanes(engine, 0, NULL, 0), "track automation clear ignored rejection");
    CHECK(!engine_track_midi_set_instrument_automation_lane_points(engine, 0,
          ENGINE_AUTOMATION_TARGET_INSTRUMENT_TONE, track_points, 2), "track automation new lane ignored rejection");
    CHECK(!engine_track_midi_set_instrument_automation_lane_points(engine, 0,
          ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL, NULL, 0), "track automation point clear ignored rejection");
    CHECK(engine->tracks[0].midi_instrument_automation_lanes == previous_track.midi_instrument_automation_lanes &&
          engine->tracks[0].midi_instrument_automation_lane_count == 1 &&
          engine->tracks[0].midi_instrument_automation_lanes[0].point_count == 2,
          "track automation rejection changed original lanes");
    CHECK(output == 5432 && clip->midi_notes.notes == previous.midi_notes.notes && clip->midi_notes.note_count == 1 &&
          clip->midi_notes.notes[0].note == 60 && clip->instrument_inherits_track == previous.instrument_inherits_track &&
          clip->instrument_preset == previous.instrument_preset && engine_instrument_params_equal(clip->instrument_params, previous.instrument_params),
          "MIDI rejection changed notes or clip instrument");
    CHECK(engine->tracks[0].midi_instrument_enabled == previous_track.midi_instrument_enabled &&
          engine->tracks[0].midi_instrument_preset == previous_track.midi_instrument_preset &&
          engine_instrument_params_equal(engine->tracks[0].midi_instrument_params, previous_track.midi_instrument_params) &&
          engine_render_mix_state(engine) == revision, "MIDI rejection changed track or revision");
    engine_graph_render_track(engine_render_source_graph(engine), after, 128, 0, 0);
    CHECK(!memcmp(before, after, sizeof(before)), "MIDI rejection changed audible output");
    reject_publication = false;
    reject_midi_allocation = true;
    CHECK(!engine_clip_midi_add_note(engine, 0, 0, note, &output) &&
          !engine_clip_midi_update_note(engine, 0, 0, 0, note, &output) &&
          !engine_clip_midi_set_notes(engine, 0, 0, &note, 1) &&
          clip->midi_notes.notes == previous.midi_notes.notes, "MIDI allocation rejection changed notes");
    reject_midi_allocation = false;
    capacity_allocation_count = 0;
    capacity_allocation_fail = 1;
    CHECK(!engine_track_midi_set_instrument_automation_lane_points(engine, 0,
          ENGINE_AUTOMATION_TARGET_INSTRUMENT_TONE, track_points, 2), "track automation allocation failure ignored");
    capacity_allocation_fail = 0;
    automation_malloc_count = 0;
    automation_malloc_fail = 1;
    CHECK(!engine_track_midi_set_instrument_automation_lanes(engine, 0,
          previous_track.midi_instrument_automation_lanes, 1), "track automation point allocation failure ignored");
    automation_malloc_fail = 0;
    CHECK(engine->tracks[0].midi_instrument_automation_lanes == previous_track.midi_instrument_automation_lanes,
          "track allocation failure replaced original ownership");
    CHECK(engine_track_midi_set_instrument_automation_lanes(engine, 0,
          previous_track.midi_instrument_automation_lanes, 1), "aliased track automation retry failed");
    CHECK(engine_track_midi_set_instrument_automation_lane_points(engine, 0,
          ENGINE_AUTOMATION_TARGET_INSTRUMENT_TONE, track_points, 2), "track automation new lane retry failed");
    CHECK(engine->tracks[0].midi_instrument_automation_lane_count == 2, "new track lane missing");
    note.velocity = NAN;
    CHECK(!engine_clip_midi_add_note(engine, 0, 0, note, NULL), "NaN velocity accepted");
    params.level = NAN;
    CHECK(!engine_track_midi_set_instrument_params(engine, 0, params) &&
          !engine_clip_midi_set_instrument_params(engine, 0, 0, params), "NaN instrument level accepted");
    note.velocity = 0.5f;
    CHECK(engine_clip_midi_set_notes(engine, 0, 0, &note, 1), "MIDI replacement retry");
    CHECK(clip->midi_notes.note_count == 1 && clip->midi_notes.notes[0].note == 72, "MIDI retry missing note");
    CHECK(engine_track_midi_set_instrument_automation_lanes(engine, 0, NULL, 0), "track automation clear retry");
    CHECK(engine_clip_midi_set_instrument_param(engine, 0, 0, ENGINE_INSTRUMENT_PARAM_LEVEL, 0), "clip level retry");
    engine_graph_render_track(engine_render_source_graph(engine), after, 128, 0, 0);
    for (int i = 0; i < 256; ++i) CHECK(after[i] == 0, "accepted clip level did not silence render source");
    CHECK(engine_clip_midi_set_inherits_track_instrument(engine, 0, 0, true), "inheritance retry");
    params = engine_track_midi_instrument_params(engine, 0);
    params.level = 0;
    CHECK(engine_track_midi_set_instrument_params(engine, 0, params), "track level retry");
    engine_graph_render_track(engine_render_source_graph(engine), after, 128, 0, 0);
    for (int i = 0; i < 256; ++i) CHECK(after[i] == 0, "accepted inherited track level did not silence render source");
    params.level = 0.5f;
    CHECK(engine_track_midi_set_instrument_params(engine, 0, params), "track level restore");
    CHECK(engine_clip_midi_remove_note(engine, 0, 0, 0), "MIDI remove retry");
    engine_graph_render_track(engine_render_source_graph(engine), after, 128, 0, 0);
    for (int i = 0; i < 256; ++i) CHECK(after[i] == 0, "MIDI removal did not silence render source");
    engine_destroy(engine);
}

// Verifies complete automation edits retain old allocations, sources, and samples when rejected.
static void test_automation_transactions(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* engine = engine_create(&config);
    CHECK(engine && engine_add_track(engine) == 1, "automation fixture tracks");
    CHECK(engine_add_midi_clip_to_track(engine, 0, 0, 4096, NULL), "automation MIDI clip");
    EngineMidiNote note = {.duration_frames = 4096, .note = 60, .velocity = 0.5f};
    CHECK(engine_clip_midi_add_note(engine, 0, 0, note, NULL), "automation MIDI note");
    float samples[128];
    for (int i = 0; i < 128; ++i) samples[i] = 0.25f;
    char path[] = "/tmp/daw-automation-transaction-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0, "automation fixture path");
    close(fd);
    CHECK(wav_write_pcm16_dithered(path, samples, 128, 1, config.sample_rate, 4), "automation WAV");
    CHECK(engine_add_clip_to_track(engine, 1, path, 0, NULL), "automation audio clip");
    EngineAutomationPoint points[] = {{0, 0.25f}, {64, 0.5f}};
    EngineAutomationPoint changed[] = {{0, -1}, {4096, -1}};
    for (int track = 0; track < 2; ++track) {
        EngineAutomationTarget target = track == 0 ? ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL : ENGINE_AUTOMATION_TARGET_VOLUME;
        EngineAutomationLane initial = {.target = target, .points = points, .point_count = 2, .point_capacity = 2};
        CHECK(engine_clip_set_automation_lanes(engine, track, 0, &initial, 1), "automation initial points");
        EngineClip* clip = &engine->tracks[track].clips[0];
        EngineAutomationLane* lane = &clip->automation_lanes[0];
        CHECK(engine_automation_lane_copy(lane, lane), "automation self-copy lost points");
        EngineAutomationLane* snapshot = NULL;
        int snapshot_count = 99;
        automation_malloc_count = 0;
        automation_malloc_fail = 1;
        CHECK(!engine_clip_snapshot_automation(clip, &snapshot, &snapshot_count) && !snapshot && snapshot_count == 0,
              "partial automation snapshot reported success");
        automation_malloc_fail = 0;
        CHECK(!engine_clip_add_automation_point(engine, track, 0, target, 0, NAN, NULL), "nonfinite automation accepted");
        CHECK(!engine_clip_update_automation_point(engine, track, 0, ENGINE_AUTOMATION_TARGET_PAN, 0, 0, 0, NULL) &&
              clip->automation_lane_count == 1, "invalid point update created a lane");
        EngineClip previous = *clip;
        EngineAutomationPoint* old_points = clip->automation_lanes[0].points;
        EngineMixState* revision = engine_render_mix_state(engine);
        float before[256] = {0}, after[256] = {0};
        if (clip->sampler) engine_sampler_source_render(clip->sampler, before, 128, 0);
        else {
            engine_instrument_source_reset(clip->instrument, config.sample_rate, 2);
            engine_instrument_source_render(clip->instrument, before, 128, 0);
        }
        float peak = 0;
        for (int i = 0; i < 256; ++i) if (fabsf(before[i]) > peak) peak = fabsf(before[i]);
        CHECK(peak > 0.001f, "automation baseline silent");
        int output_index = 4321;
        EngineAutomationLane* output_lane = NULL;
        EngineAutomationLane replacement = {.target = target,
            .points = changed, .point_count = 2, .point_capacity = 2};
        reject_publication = true;
        CHECK(!engine_clip_add_automation_point(engine, track, 0, target,
              32, 0.75f, &output_index), "automation add ignored rejection");
        CHECK(!engine_clip_update_automation_point(engine, track, 0, target,
              0, 96, 0.75f, &output_index), "automation update ignored rejection");
        CHECK(!engine_clip_remove_automation_point(engine, track, 0, target, 0),
              "automation remove ignored rejection");
        CHECK(!engine_clip_set_automation_lane_points(engine, track, 0, ENGINE_AUTOMATION_TARGET_PAN,
              changed, 2), "automation new lane points ignored rejection");
        CHECK(!engine_clip_set_automation_lanes(engine, track, 0, &replacement, 1), "automation replacement ignored rejection");
        CHECK(!engine_clip_ensure_automation_lane(engine, track, 0, ENGINE_AUTOMATION_TARGET_PAN, &output_lane),
              "automation lane ignored rejection");
        CHECK(!output_lane && output_index == 4321, "rejected automation returned success outputs");
        CHECK(clip->automation_lanes == previous.automation_lanes && clip->automation_lanes[0].points == old_points &&
              clip->automation_lane_count == 1 && clip->automation_lanes[0].point_count == 2 &&
              !memcmp(old_points, points, sizeof(points)) && clip->sampler == previous.sampler &&
              clip->instrument == previous.instrument && engine_render_mix_state(engine) == revision,
              "rejected automation replaced original ownership");
        if (clip->sampler) engine_sampler_source_render(clip->sampler, after, 128, 0);
        else {
            engine_instrument_source_reset(clip->instrument, config.sample_rate, 2);
            engine_instrument_source_render(clip->instrument, after, 128, 0);
        }
        CHECK(!memcmp(before, after, sizeof(before)), "rejected automation changed rendered samples");
        reject_publication = false;
        reject_publication = true;
        automation_malloc_count = capacity_allocation_count = 0;
        CHECK(!engine_clip_set_automation_lanes(engine, track, 0, &replacement, 1), "automation allocation probe");
        int malloc_count = automation_malloc_count, calloc_count = capacity_allocation_count;
        reject_publication = false;
        for (int allocation = 1; allocation <= malloc_count; ++allocation) {
            automation_malloc_count = 0;
            automation_malloc_fail = allocation;
            CHECK(!engine_clip_set_automation_lanes(engine, track, 0, &replacement, 1), "point-copy failure ignored");
            CHECK(clip->automation_lanes == previous.automation_lanes && clip->automation_lanes[0].points == old_points &&
                  clip->sampler == previous.sampler && clip->instrument == previous.instrument,
                  "point-copy failure destroyed original ownership");
        }
        automation_malloc_fail = 0;
        for (int allocation = 1; allocation <= calloc_count; ++allocation) {
            capacity_allocation_count = 0;
            capacity_allocation_fail = allocation;
            CHECK(!engine_clip_set_automation_lanes(engine, track, 0, &replacement, 1), "lane/source allocation failure ignored");
            CHECK(clip->automation_lanes == previous.automation_lanes && clip->automation_lanes[0].points == old_points &&
                  clip->sampler == previous.sampler && clip->instrument == previous.instrument,
                  "lane/source allocation failure destroyed original ownership");
        }
        capacity_allocation_fail = 0;
        CHECK(engine_clip_set_automation_lanes(engine, track, 0, &replacement, 1), "automation retry failed");
        CHECK(clip->automation_lanes[0].points[0].value == -1 && clip->automation_lanes[0].point_count == 2,
              "automation retry missing points");
        memset(after, 0, sizeof(after));
        if (clip->sampler) engine_sampler_source_render(clip->sampler, after, 128, 0);
        else {
            engine_instrument_source_reset(clip->instrument, config.sample_rate, 2);
            engine_instrument_source_render(clip->instrument, after, 128, 0);
        }
        float changed_peak = 0;
        for (int i = 0; i < 256; ++i) if (fabsf(after[i]) > changed_peak) changed_peak = fabsf(after[i]);
        CHECK(changed_peak < peak * 0.75f, "accepted automation did not reduce source level");
        if (clip->sampler) CHECK(changed_peak == 0, "accepted audio volume automation did not silence source");
        CHECK(engine_clip_add_automation_point(engine, track, 0, ENGINE_AUTOMATION_TARGET_PAN, 0, 0.5f, &output_index),
              "automation add/new lane retry");
        CHECK(engine_clip_update_automation_point(engine, track, 0, ENGINE_AUTOMATION_TARGET_PAN, 0, 32, 0.25f, &output_index),
              "automation update retry");
        CHECK(engine_clip_remove_automation_point(engine, track, 0, ENGINE_AUTOMATION_TARGET_PAN, 0), "automation remove retry");
    }
    engine_destroy(engine);
    unlink(path);
}

// Verifies destructive rejection preserves owned sources, track identity, effect chains, and recording isolation.
static void test_delete_transactions(void) {
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* engine = engine_create(&config);
    CHECK(engine != NULL && engine_add_track(engine) == 1, "delete tracks");
    int midi, audio;
    CHECK(engine_add_midi_clip_to_track(engine, 0, 0, 4096, &midi), "delete MIDI clip");
    EngineMidiNote note = {.duration_frames = 4096, .note = 60, .velocity = 0.5f};
    CHECK(engine_clip_midi_add_note(engine, 0, midi, note, NULL), "delete MIDI notes");
    CHECK(engine_add_midi_clip_to_track(engine, 1, 0, 4096, NULL), "surviving MIDI clip");
    float samples[128];
    for (int i = 0; i < 128; ++i) samples[i] = 0.25f;
    char path[] = "/tmp/daw-delete-transaction-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0, "delete media path");
    close(fd);
    CHECK(wav_write_pcm16_dithered(path, samples, 128, 1, config.sample_rate, 4), "delete media fixture");
    CHECK(engine_add_clip_to_track(engine, 0, path, 8192, &audio), "delete audio clip");
    FxInstId fx0 = engine_fx_track_add(engine, 0, 1), fx1 = engine_fx_track_add(engine, 1, 1);
    CHECK(fx0 && fx1 && engine_set_record_armed_track(engine, 1), "delete effects/arming");
    EngineClip saved_midi = engine->tracks[0].clips[0], saved_audio = engine->tracks[0].clips[1];
    uint64_t first_id = engine->tracks[0].runtime_id, second_id = engine->tracks[1].runtime_id;
    EngineMixState* active = engine_render_mix_state(engine);
    reject_publication = true;
    int creation_output = 8765;
    EngineClip* original_clips = engine->tracks[0].clips;
    EffectsManager* original_fx = engine->fxm;
    publication_attempts = 0;
    CHECK(!engine_move_clip_to_track(engine, 0, 1, 1, 0, &creation_output), "audio transfer ignored rejection");
    CHECK(publication_attempts == 1, "audio transfer attempted intermediate publications");
    CHECK(!engine_move_clip_to_track(engine, 0, 0, 1, 0, &creation_output), "MIDI transfer ignored rejection");
    CHECK(!engine_move_clip_to_track(engine, 0, 1, 3, 0, &creation_output), "implicit-track transfer ignored rejection");
    CHECK(engine->tracks[1].clip_count == 1 && engine->tracks[0].clips == original_clips &&
          engine->track_count == 2 && engine->tracks[0].clips[1].timeline_start_frames == 8192 && creation_output == 8765,
          "rejected move partially transferred ownership");
    reject_publication = false;
    for (int allocation = 1; allocation <= 2; ++allocation) {
        capacity_allocation_count = 0;
        capacity_allocation_fail = allocation;
        CHECK(!engine_move_clip_to_track(engine, 0, 1, 1, 0, &creation_output), "move allocation failure ignored");
        CHECK(engine->tracks[0].clips == original_clips && engine->tracks[1].clip_count == 1,
              "move allocation failure changed tracks");
    }
    capacity_allocation_fail = 0;
    reject_publication = true;
    CHECK(!engine_add_clip_to_track(engine, 0, path, 0, &creation_output), "audio creation ignored rejection");
    CHECK(!engine_add_midi_clip_to_track(engine, 0, 0, 4096, &creation_output), "MIDI creation ignored rejection");
    CHECK(!engine_duplicate_clip(engine, 0, 1, 0, &creation_output), "duplication ignored rejection");
    CHECK(!engine_add_clip_segment(engine, 0, &engine->tracks[0].clips[1], 16, 64, 0, &creation_output),
          "segment creation ignored rejection");
    CHECK(!engine_add_midi_clip_to_track(engine, 3, 0, 4096, &creation_output), "implicit MIDI track creation ignored rejection");
    CHECK(!engine_add_clip_to_track(engine, 3, path, 0, &creation_output), "implicit audio track creation ignored rejection");
    CHECK(engine->track_count == 2 && engine->tracks[0].clips == original_clips && engine->tracks[0].clip_count == 2 &&
          engine->fxm == original_fx && engine->tracks[0].clips[0].creation_index == saved_midi.creation_index &&
          engine->tracks[0].clips[1].sampler == saved_audio.sampler && creation_output == 8765,
          "failed creation replaced original project ownership");
    capacity_allocation_count = 0;
    CHECK(!engine_add_clip_to_track(engine, 0, path, 0, NULL), "creation allocation probe");
    int creation_allocations = capacity_allocation_count;
    reject_publication = false;
    for (int allocation = 1; allocation <= creation_allocations; ++allocation) {
        capacity_allocation_count = 0;
        capacity_allocation_fail = allocation;
        CHECK(!engine_add_clip_to_track(engine, 0, path, 0, &creation_output), "creation allocation failure ignored");
        CHECK(engine->tracks[0].clips == original_clips && engine->tracks[0].clip_count == 2 && creation_output == 8765,
              "creation allocation failure changed original clips");
    }
    capacity_allocation_fail = 0;
    reject_publication = true;
    CHECK(!engine_insert_track(engine, 0) && engine_add_track(engine) == -1, "track insertion ignored rejection");
    CHECK(engine->track_count == 2 && engine->tracks[0].runtime_id == first_id &&
          engine->tracks[1].runtime_id == second_id && atomic_load(&engine->record_armed_track_index) == 1,
          "track insertion rollback identity/arming");
    float sampler_before[256], sampler_after[256];
    engine_sampler_source_render(saved_audio.sampler, sampler_before, 128, 8192);
    float baseline_peak = 0;
    for (int i = 0; i < 256; ++i) if (fabsf(sampler_before[i]) > baseline_peak) baseline_peak = fabsf(sampler_before[i]);
    CHECK(baseline_peak > 0.01f, "sampler rollback fixture was silent");
    int moved_index = 1234;
    CHECK(!engine_clip_set_timeline_start(engine, 0, 0, 16384, &moved_index) && moved_index == 1234,
          "rejected MIDI move changed output index");
    CHECK(!engine_clip_set_timeline_start(engine, 0, 1, 0, NULL), "audio move rejection");
    CHECK(!engine_clip_set_region(engine, 0, 0, 0, 8192), "MIDI trim rejection");
    CHECK(!engine_clip_set_region(engine, 0, 1, 16, 64), "audio trim rejection");
    CHECK(!engine_clip_set_gain(engine, 0, 0, 0) && !engine_clip_set_gain(engine, 0, 1, 0), "clip gain rejection");
    CHECK(!engine_clip_set_fades(engine, 0, 1, 8, 8), "fade rejection");
    CHECK(!engine_clip_set_fade_curves(engine, 0, 1, ENGINE_FADE_CURVE_EXPONENTIAL, ENGINE_FADE_CURVE_LOGARITHMIC),
          "fade curve rejection");
    CHECK(engine->tracks[0].clips[1].fade_in_curve == saved_audio.fade_in_curve &&
          engine->tracks[0].clips[1].fade_out_curve == saved_audio.fade_out_curve, "curve rollback");
    CHECK(engine->tracks[0].clips[0].creation_index == saved_midi.creation_index &&
          engine->tracks[0].clips[0].timeline_start_frames == saved_midi.timeline_start_frames &&
          engine->tracks[0].clips[0].duration_frames == saved_midi.duration_frames &&
          engine->tracks[0].clips[0].gain == saved_midi.gain, "MIDI scalar rollback");
    EngineClip* restored = &engine->tracks[0].clips[1];
    CHECK(restored->creation_index == saved_audio.creation_index && restored->gain == saved_audio.gain &&
          restored->offset_frames == saved_audio.offset_frames && restored->duration_frames == saved_audio.duration_frames &&
          restored->fade_in_frames == saved_audio.fade_in_frames && restored->fade_out_frames == saved_audio.fade_out_frames,
          "audio scalar rollback");
    engine_sampler_source_render(saved_audio.sampler, sampler_after, 128, 8192);
    CHECK(!memcmp(sampler_before, sampler_after, sizeof(sampler_before)), "rejected timing changed sampler output");
    CHECK(!engine_remove_clip(engine, 0, 0) && !engine_remove_clip(engine, 0, 1), "clip deletion ignored rejection");
    CHECK(engine->tracks[0].clip_count == 2 && engine->tracks[0].clips[0].instrument == saved_midi.instrument &&
          engine->tracks[0].clips[0].midi_notes.notes[0].note == 60 &&
          engine->tracks[0].clips[1].sampler == saved_audio.sampler &&
          engine->tracks[0].clips[1].media == saved_audio.media, "clip deletion lost source ownership");
    CHECK(!engine_remove_clip(engine, 1, 0) && engine->tracks[1].active, "last clip rejection deactivated track");
    CHECK(!engine_remove_track(engine, 0), "track deletion ignored rejection");
    CHECK(engine->track_count == 2 && engine->tracks[0].runtime_id == first_id &&
          engine->tracks[1].runtime_id == second_id && atomic_load(&engine->record_armed_track_index) == 1,
          "track deletion rollback identity/arming");
    FxMasterSnapshot chain;
    CHECK(engine_fx_track_snapshot(engine, 0, &chain) && chain.items[0].id == fx0, "deleted track FX rollback");
    CHECK(engine_fx_track_snapshot(engine, 1, &chain) && chain.items[0].id == fx1, "surviving track FX rollback");
    CHECK(engine_render_mix_state(engine) == active, "delete rejection changed render revision");
    reject_publication = false;
    CHECK(!engine_clip_set_gain(engine, 0, 0, NAN), "nonfinite clip gain accepted");
    moved_index = -1;
    publication_attempts = 0;
    CHECK(engine_move_clip_to_track(engine, 0, 1, 1, 4096, &moved_index), "audio move retry");
    CHECK(publication_attempts == 1 && engine->tracks[0].clip_count == 1 && engine->tracks[1].clip_count == 2 &&
          engine->tracks[1].clips[moved_index].sampler == saved_audio.sampler &&
          engine->tracks[1].clips[moved_index].creation_index == saved_audio.creation_index,
          "accepted audio move changed identity or duplicated source");
    CHECK(engine_move_clip_to_track(engine, 1, moved_index, 0, 8192, &moved_index), "audio move return");
    CHECK(engine_move_clip_to_track(engine, 0, 0, 3, 0, &moved_index), "MIDI move to implicit track");
    CHECK(engine->track_count == 4 && engine->tracks[3].clips[moved_index].creation_index == saved_midi.creation_index &&
          engine->tracks[3].clips[moved_index].midi_notes.notes == saved_midi.midi_notes.notes,
          "MIDI transfer lost note ownership or identity");
    CHECK(engine_move_clip_to_track(engine, 3, moved_index, 0, 0, &moved_index), "MIDI move return");
    CHECK(engine_remove_track(engine, 3) && engine_remove_track(engine, 2), "move track cleanup");
    CHECK(engine_move_clip_to_track(engine, 0, 1, 0, 8192, NULL) && engine->tracks[0].clip_count == 2,
          "same-track audio move duplicated a clip");
    int duplicate = -1;
    CHECK(engine_duplicate_clip(engine, 0, 1, 32, &duplicate), "duplicate retry");
    EngineClip* copied = &engine->tracks[0].clips[duplicate];
    CHECK(copied->timeline_start_frames == saved_audio.timeline_start_frames + saved_audio.duration_frames + 32 &&
          copied->media == saved_audio.media && copied->sampler != saved_audio.sampler && copied->gain == saved_audio.gain &&
          copied->automation_lanes != engine->tracks[0].clips[1].automation_lanes,
          "duplicate did not preserve independent source ownership");
    CHECK(engine_remove_clip(engine, 0, duplicate), "duplicate cleanup");
    CHECK(engine_add_clip_segment(engine, 0, &engine->tracks[0].clips[1], 16, 64, 4096, &duplicate), "segment retry");
    CHECK(engine->tracks[0].clips[duplicate].offset_frames == 16 && engine->tracks[0].clips[duplicate].duration_frames == 64,
          "segment bounds incorrect");
    CHECK(engine_remove_clip(engine, 0, duplicate), "segment cleanup");
    CHECK(!engine_duplicate_clip(engine, 0, 1, UINT64_MAX, NULL), "duplicate timeline overflow accepted");
    CHECK(engine_add_midi_clip_to_track(engine, 3, 0, 4096, &duplicate), "implicit track MIDI creation retry");
    CHECK(engine->track_count == 4 && engine->tracks[3].clip_count == 1 && duplicate == 0,
          "implicit track creation retry incomplete");
    CHECK(engine_remove_track(engine, 3) && engine_remove_track(engine, 2), "implicit track cleanup");
    CHECK(engine_clip_set_fades(engine, 0, 1, UINT64_MAX, UINT64_MAX), "bounded fade retry");
    CHECK(engine->tracks[0].clips[1].fade_in_frames == saved_audio.duration_frames &&
          engine->tracks[0].clips[1].fade_out_frames == 0, "fade length bounds overflowed");
    CHECK(engine_insert_track(engine, 0) && engine->tracks[1].runtime_id == first_id &&
          engine->tracks[2].runtime_id == second_id && atomic_load(&engine->record_armed_track_index) == 2,
          "track insertion retry/remap");
    CHECK(engine_remove_track(engine, 0), "remove inserted fixture track");
    CHECK(engine_remove_clip(engine, 0, 0) && engine->tracks[0].clip_count == 1 &&
          engine->tracks[0].clips[0].creation_index == saved_audio.creation_index, "clip deletion retry");
    CHECK(engine_remove_track(engine, 0) && engine->track_count == 1 &&
          engine->tracks[0].runtime_id == second_id && atomic_load(&engine->record_armed_track_index) == 0,
          "track deletion retry/remap");
    CHECK(engine_fx_track_snapshot(engine, 0, &chain) && chain.items[0].id == fx1, "FX identity after delete");
    CHECK(engine_remove_track(engine, 0) && engine->track_count == 0 &&
          atomic_load(&engine->record_armed_track_index) == -1, "final track deletion/disarm");
    engine_destroy(engine);
    unlink(path);
}

// Checks rollback against both editable state and the still-active render revision.
// Rejects every compound preparation allocation and verifies one revision for a cross-track swap.
static void test_batch_transform(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    Engine* engine = engine_create(&cfg);
    CHECK(engine && engine_add_midi_clip_to_track(engine, 0, 0, 4096, NULL) &&
          engine_add_midi_clip_to_track(engine, 1, 0, 4096, NULL), "batch fixture");
    EngineMidiNote note = {.start_frame = 0, .duration_frames = 100, .note = 60, .velocity = 0.8f};
    CHECK(engine_clip_midi_add_note(engine, 0, 0, note, NULL), "batch owned notes fixture");
    EngineClipBatchTransform edits[2] = {0};
    EngineClip* arrays[2] = {engine->tracks[0].clips, engine->tracks[1].clips};
    for (int i = 0; i < 2; ++i) {
        edits[i].creation_index = arrays[i][0].creation_index;
        edits[i].destination_track = 1 - i;
        edits[i].transform = (EngineClipTransform){.start_frame = 100 + i, .duration_frames = 4096,
            .gain = 0.5f, .instrument_params = arrays[i][0].instrument_params,
            .midi_notes = arrays[i][0].midi_notes.notes, .midi_note_count = arrays[i][0].midi_notes.note_count};
    }
    UndoClipState before[2] = {0}, after[2] = {0};
    for (int i = 0; i < 2; ++i) {
        CHECK(undo_clip_state_capture(engine, &arrays[i][0], i, &before[i]), "batch history capture");
        CHECK(undo_clip_state_clone(&after[i], &before[i]), "batch history clone");
        after[i].track_index = 1 - i; after[i].track_runtime_id = engine->tracks[1 - i].runtime_id; after[i].start_frame = 100 + i; after[i].gain = 0.5f;
    }
    const EngineMixState* active = engine_render_mix_state(engine);
    for (int fail = 1; fail <= 7; ++fail) {
        capacity_allocation_count = 0; capacity_allocation_fail = fail;
        CHECK(!engine_transform_clips(engine, edits, 2), "batch allocation rejection");
        CHECK(engine->tracks[0].clips == arrays[0] && engine->tracks[1].clips == arrays[1] &&
              engine_render_mix_state(engine) == active, "partial batch allocation changed project");
    }
    capacity_allocation_fail = 0;
    reject_midi_allocation = true;
    CHECK(!engine_transform_clips(engine, edits, 2), "batch note allocation rejection");
    CHECK(engine->tracks[0].clips == arrays[0], "failed MIDI preparation changed ownership");
    reject_midi_allocation = false;
    edits[1].transform.duration_frames = 0;
    CHECK(!engine_transform_clips(engine, edits, 2), "invalid later target accepted");
    CHECK(arrays[0][0].timeline_start_frames == 0, "earlier target changed on validation failure");
    edits[1].transform.duration_frames = 4096;
    reject_publication = true;
    CHECK(!engine_transform_clips(engine, edits, 2), "batch publication rejection");
    CHECK(engine->tracks[0].clips == arrays[0] && engine->tracks[1].clips == arrays[1] &&
          engine_render_mix_state(engine) == active, "batch publication rollback");
    reject_publication = false; publication_attempts = 0;
    CHECK(engine_transform_clips(engine, edits, 2), "batch retry");
    CHECK(publication_attempts == 1, "compound edit published intermediate states");
    CHECK(engine->tracks[0].clips[0].creation_index == edits[1].creation_index &&
          engine->tracks[1].clips[0].creation_index == edits[0].creation_index &&
          engine->tracks[0].clips[0].timeline_start_frames == 101, "cross-track batch identity");
    AppState* state = calloc(1, sizeof(*state));
    CHECK(state != NULL, "batch history app fixture");
    state->engine = engine; undo_manager_init(&state->undo);
    UndoCommand command = {.type = UNDO_CMD_MULTI_CLIP_TRANSFORM};
    command.data.multi_clip_transform = (UndoMultiClipTransform){.count = 2, .before = before, .after = after};
    CHECK(undo_manager_push(&state->undo, &command), "batch history push");
    reject_publication = true;
    CHECK(!undo_manager_undo(&state->undo, state), "batch rejected undo result");
    CHECK(state->undo.undo_count == 1 && state->undo.redo_count == 0 &&
          engine->tracks[0].clips[0].creation_index == edits[1].creation_index, "rejected undo consumed history");
    reject_publication = false;
    CHECK(undo_manager_undo(&state->undo, state), "batch successful undo");
    CHECK(engine->tracks[0].clips[0].creation_index == edits[0].creation_index &&
          engine->tracks[0].clips[0].timeline_start_frames == 0, "undo batch readback");
    CHECK(undo_manager_redo(&state->undo, state), "batch successful redo");
    undo_manager_free(&state->undo); free(state);
    for (int i = 0; i < 2; ++i) { undo_clip_state_clear(&before[i]); undo_clip_state_clear(&after[i]); }
    edits[0].transform.midi_notes = &note;
    edits[1].creation_index = edits[0].creation_index;
    CHECK(!engine_transform_clips(engine, edits, 2), "duplicate batch target accepted");
    engine_destroy(engine);
}

// Keeps transform history attached to its track after a preceding track is removed.
static void test_transform_track_identity(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    Engine* engine = engine_create(&cfg);
    CHECK(engine && engine_add_midi_clip_to_track(engine, 1, 0, 4096, NULL), "identity fixture");
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "identity app");
    state->engine = engine; undo_manager_init(&state->undo);
    UndoCommand cmd = {.type = UNDO_CMD_CLIP_TRANSFORM};
    CHECK(undo_clip_state_capture(engine, &engine->tracks[1].clips[0], 1, &cmd.data.clip_transform.before), "identity before");
    CHECK(engine_clip_set_timeline_start(engine, 1, 0, 400, NULL), "identity move");
    CHECK(undo_clip_state_capture(engine, &engine->tracks[1].clips[0], 1, &cmd.data.clip_transform.after), "identity after");
    CHECK(undo_manager_push(&state->undo, &cmd), "identity push");
    CHECK(engine_remove_track(engine, 0), "remove preceding track");
    CHECK(undo_manager_undo(&state->undo, state) && engine->tracks[0].clips[0].timeline_start_frames == 0,
          "history followed obsolete track index");
    CHECK(undo_manager_redo(&state->undo, state) && engine->tracks[0].clips[0].timeline_start_frames == 400,
          "redo stable track");
    CHECK(engine_remove_track(engine, 0), "remove history target");
    CHECK(!undo_manager_undo(&state->undo, state) && state->undo.undo_count == 1, "missing target consumed history");
    undo_clip_state_clear(&cmd.data.clip_transform.before); undo_clip_state_clear(&cmd.data.clip_transform.after);
    undo_manager_free(&state->undo); free(state); engine_destroy(engine);
}

// Rejects a compound move that needs new tracks, then verifies all new destinations publish together.
static void test_batch_track_growth(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    Engine* engine = engine_create(&cfg);
    CHECK(engine && engine_add_midi_clip_to_track(engine, 0, 0, 4096, NULL) &&
          engine_add_midi_clip_to_track(engine, 1, 0, 4096, NULL), "growing batch fixture");
    int count = engine->track_count;
    const EngineMixState* active = engine_render_mix_state(engine);
    EffectsManager* fx = engine->fxm;
    EngineClipBatchTransform edits[2] = {0};
    for (int i = 0; i < 2; ++i) {
        edits[i].creation_index = engine->tracks[i].clips[0].creation_index;
        edits[i].destination_track = count + i;
        edits[i].transform = (EngineClipTransform){.start_frame = 500, .duration_frames = 4096, .gain = 1,
            .instrument_params = engine->tracks[i].clips[0].instrument_params};
    }
    reject_publication = true;
    CHECK(!engine_transform_clips(engine, edits, 2), "growing batch rejection");
    CHECK(engine->track_count == count && engine->fxm == fx && engine_render_mix_state(engine) == active &&
          engine->tracks[0].clip_count == 1 && engine->tracks[1].clip_count == 1, "growth failure leaked project state");
    reject_publication = false; publication_attempts = 0;
    CHECK(engine_transform_clips(engine, edits, 2), "growing batch retry");
    CHECK(publication_attempts == 1 && engine->track_count == count + 2 &&
          engine->tracks[count].clips[0].creation_index == edits[0].creation_index &&
          engine->tracks[count + 1].clips[0].creation_index == edits[1].creation_index, "partial growing batch publication");
    for (int i = 0; i < 2; ++i) edits[i].destination_track = i;
    active = engine_render_mix_state(engine); fx = engine->fxm;
    reject_publication = true;
    CHECK(!engine_transform_clips_trim_tracks(engine, edits, 2, count), "batch shrink rejection");
    CHECK(engine->track_count == count + 2 && engine->fxm == fx && engine_render_mix_state(engine) == active,
          "failed shrink changed track topology");
    reject_publication = false;
    CHECK(engine_add_midi_clip_to_track(engine, count, 9000, 4096, NULL), "unrelated trailing content");
    CHECK(!engine_transform_clips_trim_tracks(engine, edits, 2, count), "shrink deleted unrelated clip");
    CHECK(engine_remove_clip(engine, count, 1), "remove unrelated test content");
    publication_attempts = 0;
    CHECK(engine_transform_clips_trim_tracks(engine, edits, 2, count), "batch shrink retry");
    CHECK(publication_attempts == 1 && engine->track_count == count && engine->tracks[0].clip_count == 1 &&
          engine->tracks[1].clip_count == 1, "batch shrink was not complete");
    engine_destroy(engine);
}

// Restores owned MIDI notes and automation, and rejects a removed track identity.
static void test_midi_content_history(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    Engine* engine = engine_create(&cfg);
    CHECK(engine && engine_add_midi_clip_to_track(engine, 0, 0, 4096, NULL), "MIDI content fixture");
    EngineMidiNote note = {.start_frame = 20, .duration_frames = 200, .note = 64, .velocity = 0.7f};
    CHECK(engine_clip_midi_add_note(engine, 0, 0, note, NULL), "MIDI content note");
    CHECK(engine_clip_add_automation_point(engine, 0, 0, ENGINE_AUTOMATION_TARGET_VOLUME, 0, 0.5f, NULL), "MIDI content automation");
    int track = 0;
    EngineClipContentSnapshot* snapshot = engine_clip_content_capture(engine, &track, 1);
    CHECK(snapshot != NULL, "MIDI content capture");
    uint64_t identity = engine->tracks[0].clips[0].creation_index;
    CHECK(engine_remove_clip(engine, 0, 0) && engine_clip_content_restore(engine, snapshot), "MIDI content restore");
    CHECK(engine->tracks[0].clips[0].creation_index == identity && engine->tracks[0].clips[0].midi_notes.note_count == 1 &&
          engine->tracks[0].clips[0].midi_notes.notes[0].note == 64 && engine->tracks[0].clips[0].automation_lane_count == 1,
          "MIDI content lost authored fields");
    CHECK(engine_move_clip_to_track(engine, 0, 0, 1, 0, NULL), "move content identity outside snapshot");
    CHECK(!engine_clip_content_restore(engine, snapshot) && engine->tracks[0].clip_count == 0 &&
          engine->tracks[1].clip_count == 1, "snapshot duplicated a moved identity");
    CHECK(engine_remove_track(engine, 0), "remove content target track");
    CHECK(!engine_clip_content_restore(engine, snapshot), "history restored into missing track");
    engine_clip_content_release(snapshot); engine_destroy(engine);
}

// Publishes placement and destructive neighbor overlap together, retaining both complete content states.
static void test_content_drop_transaction(bool growth) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg); Engine* engine = engine_create(&cfg);
    float samples[1024]; for (int i = 0; i < 1024; ++i) samples[i] = 0.25f;
    char path[] = "/tmp/daw-content-drop-XXXXXX"; int fd = mkstemp(path); CHECK(fd >= 0, "content drop path"); close(fd);
    CHECK(engine && wav_write_f32(path, samples, 1024, 1, cfg.sample_rate), "content drop media");
    CHECK(engine_add_clip_to_track(engine, 0, path, 0, NULL) && engine_clip_set_region(engine, 0, 0, 0, 512), "content older neighbor");
    int c; CHECK(engine_add_clip_to_track(engine, 0, path, 600, &c) && engine_clip_set_region(engine, 0, c, 0, 128), "content drop anchor");
    EngineClipBatchTransform initial = {.creation_index = engine->tracks[0].clips[c].creation_index, .destination_track = 0,
        .transform = {.start_frame = 600, .duration_frames = 128, .gain = 1}};
    EngineClipBatchTransform final = initial; final.transform.start_frame = 128;
    EngineClipBatchTransform starts[2] = {initial}, ends[2] = {final};
    if (growth) {
        CHECK(engine_add_midi_clip_to_track(engine, 1, 900, 128, NULL), "growth MIDI anchor");
        starts[1] = (EngineClipBatchTransform){.creation_index = engine->tracks[1].clips[0].creation_index,
            .destination_track = 1, .transform = {.start_frame = 900, .duration_frames = 128, .gain = 1,
                .instrument_inherits_track = true}};
        ends[1] = starts[1]; ends[1].destination_track = 3;
    }
    int track_count = engine->track_count;
    EngineClipContentSnapshot* before = NULL; EngineClipContentSnapshot* after = NULL;
    EngineClip* original = engine->tracks[0].clips; const EngineMixState* active = engine_render_mix_state(engine);
    reject_publication = true; publication_attempts = 0;
    CHECK(!engine_clip_content_drop(engine, starts, ends, growth ? 2 : 1, &before, &after), "drop publication rejection");
    CHECK(publication_attempts == 1 && !before && !after && engine->tracks[0].clips == original &&
          engine_render_mix_state(engine) == active && engine->clip_history_snapshots == NULL && engine->track_count == track_count, "drop rejection leaked partial state or history");
    capacity_allocation_count = 0;
    CHECK(!engine_clip_content_drop(engine, starts, ends, growth ? 2 : 1, &before, &after), "allocation sweep sizing");
    int allocations = capacity_allocation_count;
    reject_publication = false;
    for (int fail = 1; fail <= allocations; ++fail) {
        capacity_allocation_count = 0; capacity_allocation_fail = fail;
        CHECK(!engine_clip_content_drop(engine, starts, ends, growth ? 2 : 1, &before, &after), "drop allocation rejection");
        CHECK(!before && !after && engine->track_count == track_count && engine->tracks[0].clips == original &&
            engine_render_mix_state(engine) == active && !engine->clip_history_snapshots, "allocation rejection changed topology or content");
    }
    capacity_allocation_fail = 0;
    reject_publication = false; publication_attempts = 0;
    CHECK(engine_clip_content_drop(engine, starts, ends, growth ? 2 : 1, &before, &after), "content drop commit");
    CHECK(publication_attempts == 1 && engine->tracks[0].clip_count == 3, "placement and overlap published separately");
    CHECK(engine_clip_content_restore(engine, before) && engine->tracks[0].clip_count == 2 &&
          engine->tracks[0].clips[0].duration_frames == 512 && engine->tracks[0].clips[1].timeline_start_frames == 600,
          "drop history failed to restore neighbor and anchor");
    CHECK(engine_clip_content_restore(engine, after) && engine->tracks[0].clip_count == 3, "drop redo content");
    if (growth) {
        CHECK(engine->track_count == 4, "growth final topology");
        active = engine_render_mix_state(engine); reject_publication = true;
        CHECK(!engine_clip_content_restore(engine, before) && engine->track_count == 4 &&
            engine_render_mix_state(engine) == active, "shrink publication rejection");
        reject_publication = false;
        CHECK(engine_clip_content_restore(engine, before) && engine->track_count == track_count, "shrink history topology");
        active = engine_render_mix_state(engine); reject_publication = true;
        CHECK(!engine_clip_content_restore(engine, after) && engine->track_count == track_count &&
            engine_render_mix_state(engine) == active, "regrowth publication rejection");
        reject_publication = false;
        CHECK(engine_clip_content_restore(engine, after) && engine->track_count == 4, "regrowth history topology");
    }
    engine_clip_content_release(before); engine_clip_content_release(after); engine_destroy(engine); unlink(path);
}

// Rejects a complete audio trim before publication and preserves selected identities after sorting.
static void test_audio_trim_action(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "trim state");
    state->engine = engine_create(&cfg); CHECK(state->engine != NULL, "trim engine");
    char path[] = "/tmp/daw-trim-action-XXXXXX"; int fd = mkstemp(path); CHECK(fd >= 0, "trim path"); close(fd);
    float samples[1024] = {0}; CHECK(wav_write_f32(path, samples, 1024, 1, cfg.sample_rate), "trim media");
    CHECK(engine_add_clip_to_track(state->engine, 0, path, 100, NULL) &&
        engine_clip_set_region(state->engine, 0, 0, 0, 512) &&
        engine_add_clip_to_track(state->engine, 0, path, 200, NULL), "trim clips");
    state->timeline_drag.track_index = 0; state->timeline_drag.clip_index = 0;
    timeline_selection_add(state, 0, 0); timeline_selection_add(state, 0, 1);
    uint64_t anchor = state->engine->tracks[0].clips[0].creation_index;
    uint64_t neighbor = state->engine->tracks[0].clips[1].creation_index;
    TimelineSelectionEntry selected[2]; memcpy(selected, state->selection, sizeof(selected));
    const EngineMixState* active = engine_render_mix_state(state->engine);
    reject_publication = true; publication_attempts = 0;
    CHECK(!timeline_apply_audio_trim(state, 300, 200, 312) && publication_attempts == 1, "trim rejection");
    CHECK(engine_render_mix_state(state->engine) == active && state->engine->tracks[0].clips[0].duration_frames == 512 &&
        state->engine->tracks[0].clips[0].timeline_start_frames == 100 && !memcmp(selected, state->selection, sizeof(selected)),
        "trim rejection changed region position or selection");
    reject_publication = false; publication_attempts = 0;
    CHECK(timeline_apply_audio_trim(state, 300, 200, 312) && publication_attempts == 1, "atomic trim commit");
    const EngineClip* clips = state->engine->tracks[0].clips;
    CHECK(state->timeline_drag.clip_index == 1 && clips[1].creation_index == anchor && clips[1].offset_frames == 200 &&
        clips[1].duration_frames == 312 && clips[state->selection[0].clip_index].creation_index == anchor &&
        clips[state->selection[1].clip_index].creation_index == neighbor, "trim sorted selection identity");
    engine_destroy(state->engine); free(state); unlink(path);
}

// Rejects both MIDI trim entry points atomically, then verifies one publication and sorted identity.
static void test_midi_trim_action(bool from_baseline, bool extend) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    Engine* engine = engine_create(&cfg); CHECK(engine != NULL, "MIDI trim engine");
    CHECK(engine_add_midi_clip_to_track(engine, 0, 100, 512, NULL) &&
        engine_add_midi_clip_to_track(engine, 0, 200, 512, NULL), "MIDI trim fixture");
    EngineMidiNote note = {.start_frame = 20, .duration_frames = 300, .note = 60, .velocity = 0.4f};
    CHECK(engine_clip_midi_set_notes(engine, 0, 0, &note, 1), "MIDI trim notes");
    EngineClip original = engine->tracks[0].clips[0];
    const EngineMixState* active = engine_render_mix_state(engine);
    uint64_t target = extend ? 50 : 300;
    int index = 0;
    reject_publication = true; publication_attempts = 0; capacity_allocation_count = 0;
    bool ok = from_baseline
        ? timeline_midi_left_trim_apply_from_notes(engine, 0, &index, 100, 512, &note, 1, target)
        : timeline_midi_left_trim_apply(engine, 0, &index, target);
    CHECK(!ok && publication_attempts == 1 && index == 0, "MIDI trim publication rejection");
    const EngineClip* clip = &engine->tracks[0].clips[0];
    CHECK(engine_render_mix_state(engine) == active && clip->creation_index == original.creation_index &&
        clip->timeline_start_frames == 100 && clip->duration_frames == 512 &&
        engine_clip_midi_note_count(clip) == 1 &&
        !memcmp(engine_clip_midi_notes(clip), &note, sizeof(note)), "MIDI trim rejection changed content");
    int preparation_allocations = capacity_allocation_count;
    reject_publication = false;
    for (int fail = 1; fail <= preparation_allocations; ++fail) {
        capacity_allocation_count = 0; capacity_allocation_fail = fail;
        ok = from_baseline
            ? timeline_midi_left_trim_apply_from_notes(engine, 0, &index, 100, 512, &note, 1, target)
            : timeline_midi_left_trim_apply(engine, 0, &index, target);
        capacity_allocation_fail = 0;
        clip = &engine->tracks[0].clips[0];
        CHECK(!ok && index == 0 && engine_render_mix_state(engine) == active &&
            clip->timeline_start_frames == 100 && clip->duration_frames == 512 &&
            engine_clip_midi_note_count(clip) == 1 &&
            !memcmp(engine_clip_midi_notes(clip), &note, sizeof(note)), "MIDI trim allocation rollback");
    }
    publication_attempts = 0;
    ok = from_baseline
        ? timeline_midi_left_trim_apply_from_notes(engine, 0, &index, 100, 512, &note, 1, target)
        : timeline_midi_left_trim_apply(engine, 0, &index, target);
    CHECK(ok && publication_attempts == 1 && index == (extend ? 0 : 1), "MIDI trim single publication");
    clip = &engine->tracks[0].clips[index];
    const EngineMidiNote* result = engine_clip_midi_notes(clip);
    CHECK(clip->creation_index == original.creation_index && clip->timeline_start_frames == target &&
        clip->duration_frames == (extend ? 562 : 312) && clip->gain == original.gain &&
        clip->instrument_preset == original.instrument_preset &&
        clip->instrument_inherits_track == original.instrument_inherits_track &&
        engine_clip_midi_note_count(clip) == 1 && result[0].start_frame == (extend ? 70 : 0) &&
        result[0].duration_frames == (extend ? 300 : 120), "MIDI trim content or settings");
    engine_destroy(engine);
}

// Verifies inspector numeric history, sorted identity and retained input on failed publication/conversion.
static void test_inspector_numeric_transaction(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "numeric state");
    state->engine = engine_create(&cfg); CHECK(state->engine != NULL, "numeric engine");
    undo_manager_init(&state->undo);
    CHECK(engine_add_midi_clip_to_track(state->engine, 0, 100, 1000, NULL) &&
        engine_add_midi_clip_to_track(state->engine, 0, 200, 1000, NULL), "numeric clips");
    timeline_selection_add(state, 0, 0); timeline_selection_add(state, 0, 1);
    uint64_t anchor = engine_get_tracks(state->engine)[0].clips[0].creation_index;
    state->inspector.track_index = 0; state->inspector.clip_index = 0;
    inspector_numeric_begin_edit(state, &engine_get_tracks(state->engine)[0].clips[0],
                                 &state->inspector.edit.editing_timeline_start);
    strcpy(state->inspector.edit.timeline_start, "0.010");
    const EngineMixState* active = engine_render_mix_state(state->engine);
    reject_publication = true;
    CHECK(!inspector_numeric_commit_edit(state) && state->undo.undo_count == 0 && !state->undo.active_drag_valid &&
        engine_render_mix_state(state->engine) == active && state->inspector.edit.editing_timeline_start &&
        !strcmp(state->inspector.edit.timeline_start, "0.010"), "numeric rejection lost state/input");
    reject_publication = false;
    CHECK(inspector_numeric_commit_edit(state) && state->undo.undo_count == 1 && state->inspector.clip_index == 1 &&
        engine_get_tracks(state->engine)[0].clips[1].creation_index == anchor && state->selection[0].clip_index == 1 &&
        state->selection[1].clip_index == 0, "numeric sorted identity/history");
    CHECK(undo_manager_undo(&state->undo, state) && engine_get_tracks(state->engine)[0].clips[0].creation_index == anchor &&
        engine_get_tracks(state->engine)[0].clips[0].timeline_start_frames == 100, "numeric undo");
    state->inspector.track_index = 0; state->inspector.clip_index = 0;
    inspector_numeric_begin_edit(state, &engine_get_tracks(state->engine)[0].clips[0],
                                 &state->inspector.edit.editing_timeline_length);
    const char* invalid[] = {"nan", "inf", "1e300", "0", "-1", "bad"};
    for (int i = 0; i < 6; ++i) {
        strcpy(state->inspector.edit.timeline_length, invalid[i]);
        CHECK(!inspector_numeric_commit_edit(state) && state->inspector.edit.editing_timeline_length &&
            !strcmp(state->inspector.edit.timeline_length, invalid[i]) && state->undo.redo_count == 1,
            "invalid numeric entry changed history/input");
    }
    strcpy(state->inspector.edit.timeline_length, "0.020");
    CHECK(inspector_numeric_commit_edit(state) && state->undo.undo_count == 1 && state->undo.redo_count == 0,
        "numeric duration commit");
    uint64_t duration = engine_get_tracks(state->engine)[0].clips[0].duration_frames;
    CHECK(undo_manager_undo(&state->undo, state) && engine_get_tracks(state->engine)[0].clips[0].duration_frames == 1000 &&
        undo_manager_redo(&state->undo, state) && engine_get_tracks(state->engine)[0].clips[0].duration_frames == duration,
        "numeric duration undo/redo");
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state);
}

// Exercises every audio numeric field with engine readback and reversible source-bound clamping.
static void test_inspector_audio_numeric_fields(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "audio numeric state");
    state->engine = engine_create(&cfg); CHECK(state->engine != NULL, "audio numeric engine");
    undo_manager_init(&state->undo);
    char path[] = "/tmp/daw-numeric-audio-XXXXXX"; int fd = mkstemp(path); CHECK(fd >= 0, "numeric media path"); close(fd);
    float samples[2048] = {0}; CHECK(wav_write_f32(path, samples, 2048, 1, cfg.sample_rate), "numeric media");
    CHECK(engine_add_clip_to_track(state->engine, 0, path, 1000, NULL), "numeric audio clip");
    bool* flags[] = {&state->inspector.edit.editing_timeline_start, &state->inspector.edit.editing_timeline_end,
        &state->inspector.edit.editing_timeline_length, &state->inspector.edit.editing_source_start,
        &state->inspector.edit.editing_source_end};
    const uint64_t requested[] = {1200, 1400, 400, 1800, 500};
    for (int field = 0; field < 5; ++field) {
        CHECK(engine_clip_set_timeline_start(state->engine, 0, 0, 1000, NULL) &&
            engine_clip_set_region(state->engine, 0, 0, 100, 500), "numeric baseline");
        state->inspector.track_index = 0; state->inspector.clip_index = 0;
        inspector_numeric_begin_edit(state, &engine_get_tracks(state->engine)[0].clips[0], flags[field]);
        snprintf(inspector_numeric_active_buffer(&state->inspector.edit), 32, "%.9f", (double)requested[field] / cfg.sample_rate);
        CHECK(inspector_numeric_commit_edit(state), "audio numeric field commit");
        EngineClip after = engine_get_tracks(state->engine)[0].clips[0];
        CHECK(after.timeline_start_frames == (field == 0 ? 1200 : 1000) &&
            after.offset_frames == (field == 3 ? 1800 : 100) &&
            after.duration_frames == (field == 0 ? 500 : field == 3 ? 248 : 400), "audio numeric field readback");
        CHECK(undo_manager_undo(&state->undo, state), "audio numeric field undo");
        const EngineClip* restored = &engine_get_tracks(state->engine)[0].clips[0];
        CHECK(restored->timeline_start_frames == 1000 && restored->offset_frames == 100 && restored->duration_frames == 500,
            "audio numeric restored bounds");
        CHECK(undo_manager_redo(&state->undo, state), "audio numeric field redo");
        restored = &engine_get_tracks(state->engine)[0].clips[0];
        CHECK(restored->timeline_start_frames == after.timeline_start_frames && restored->offset_frames == after.offset_frames &&
            restored->duration_frames == after.duration_frames, "audio numeric replay bounds");
    }
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state); unlink(path);
}

// Exercises audio/MIDI inspector rename through reordering, cross-track moves and target removal.
static void test_inspector_rename_identity(bool audio) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "rename state");
    state->engine = engine_create(&cfg); CHECK(state->engine != NULL, "rename engine");
    undo_manager_init(&state->undo);
    char path[] = "/tmp/daw-rename-XXXXXX"; int fd = mkstemp(path); CHECK(fd >= 0, "rename path"); close(fd);
    float samples[1024] = {0}; CHECK(wav_write_f32(path, samples, 1024, 1, cfg.sample_rate), "rename media");
    CHECK(audio ? engine_add_clip_to_track(state->engine, 0, path, 100, NULL)
                : engine_add_midi_clip_to_track(state->engine, 0, 100, 512, NULL), "rename target");
    CHECK(engine_add_midi_clip_to_track(state->engine, 0, 200, 512, NULL) &&
        engine_clip_set_name(state->engine, 0, 0, "Original") && engine_add_track(state->engine), "rename neighbors");
    uint64_t identity = engine_get_tracks(state->engine)[0].clips[0].creation_index;
    state->inspector.track_index = 0; state->inspector.clip_index = 0;
    inspector_input_begin_rename(state); strcpy(state->inspector.name, "Renamed");
    UndoCommand occupied = {.type = UNDO_CMD_CLIP_RENAME}; occupied.data.clip_rename.creation_index = identity;
    CHECK(undo_manager_begin_drag(&state->undo, &occupied), "rename history reservation fixture");
    SDL_Event release = {.type = SDL_MOUSEBUTTONUP}; release.button.button = SDL_BUTTON_LEFT;
    inspector_input_handle_event(&state->input_manager, state, &release);
    CHECK(state->undo.active_drag_valid && state->undo.active_drag.type == UNDO_CMD_CLIP_RENAME &&
        state->undo.active_drag.data.clip_rename.creation_index == identity, "inspector release consumed unrelated history");
    inspector_input_commit_if_editing(state);
    CHECK(state->inspector.editing_name && !strcmp(state->inspector.name, "Renamed") &&
        !strcmp(engine_get_tracks(state->engine)[0].clips[0].name, "Original") && state->undo.active_drag_valid,
        "rename replaced active history or lost input");
    undo_manager_cancel_drag(&state->undo);
    inspector_input_commit_if_editing(state);
    CHECK(!state->inspector.editing_name && state->undo.undo_count == 1 &&
        !strcmp(engine_get_tracks(state->engine)[0].clips[0].name, "Renamed"), "rename commit");
    int index = 0;
    CHECK(engine_clip_set_timeline_start(state->engine, 0, 0, 300, &index) && index == 1 &&
        undo_manager_undo(&state->undo, state) && !strcmp(engine_get_tracks(state->engine)[0].clips[1].name, "Original"),
        "rename undo after reorder");
    CHECK(engine_move_clip_to_track(state->engine, 0, 1, 1, 400, &index) &&
        undo_manager_redo(&state->undo, state) && !strcmp(engine_get_tracks(state->engine)[1].clips[index].name, "Renamed"),
        "rename redo after track move");
    CHECK(engine_remove_clip(state->engine, 1, index) && !undo_manager_undo(&state->undo, state) &&
        state->undo.undo_count == 1 && state->undo.redo_count == 0, "removed rename target moved history cursor");
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state); unlink(path);
}

// Verifies complete MIDI history for inspector gain/fades and protects conflicting reservations.
static void test_inspector_scalar_history(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "scalar inspector state");
    state->engine = engine_create(&cfg); CHECK(state->engine != NULL, "scalar inspector engine");
    undo_manager_init(&state->undo);
    CHECK(engine_add_midi_clip_to_track(state->engine, 0, 100, 1000, NULL), "scalar MIDI region");
    EngineMidiNote note = {.start_frame = 10, .duration_frames = 300, .note = 67, .velocity = 0.7f};
    CHECK(engine_clip_midi_set_notes(state->engine, 0, 0, &note, 1), "scalar notes");
    timeline_selection_set_single(state, 0, 0);
    state->inspector.track_index = 0; state->inspector.clip_index = 0;
    CHECK(inspector_input_begin_clip_drag(state), "gain reserve");
    CHECK(!inspector_input_begin_clip_drag(state), "gain replaced active history");
    ClipInspectorLayout layout = {.fade_in_track_rect = {0, 0, 100, 20}};
    SDL_Point point = {50, 10};
    CHECK(inspector_fade_input_handle_track_mouse_down(state, &layout, &point, false, true) &&
        !state->inspector.adjusting_fade_in && state->undo.active_drag_valid, "fade started without own reservation");
    state->inspector.adjusting_gain = true;
    CHECK(engine_clip_set_gain(state->engine, 0, 0, 0.25f), "gain preview");
    SDL_Event release = {.type = SDL_MOUSEBUTTONUP}; release.button.button = SDL_BUTTON_LEFT;
    inspector_input_handle_event(&state->input_manager, state, &release);
    CHECK(state->undo.undo_count == 1 && !state->undo.active_drag_valid &&
        undo_manager_undo(&state->undo, state) && engine_get_tracks(state->engine)[0].clips[0].gain == 1.0f &&
        undo_manager_redo(&state->undo, state) && engine_get_tracks(state->engine)[0].clips[0].gain == 0.25f, "gain history");
    state->inspector.fade_in_selected = true;
    reject_publication = true;
    CHECK(inspector_fade_input_handle_keydown(state, SDLK_RIGHT) && state->undo.undo_count == 1 &&
        !state->undo.active_drag_valid, "rejected curve left history");
    reject_publication = false;
    CHECK(inspector_fade_input_handle_keydown(state, SDLK_RIGHT) && state->undo.undo_count == 2 &&
        undo_manager_undo(&state->undo, state) && undo_manager_redo(&state->undo, state), "curve history");
    const EngineClip* clip = &engine_get_tracks(state->engine)[0].clips[0];
    CHECK(engine_clip_midi_note_count(clip) == 1 && !memcmp(engine_clip_midi_notes(clip), &note, sizeof(note)) &&
        clip->instrument_inherits_track, "fade history lost MIDI content/settings");
    CHECK(inspector_input_begin_clip_drag(state), "no-op reserve");
    inspector_input_finish_clip_drag(state);
    CHECK(state->undo.undo_count == 2 && !state->undo.active_drag_valid, "no-op history entry");
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state);
}

// Keeps undo/redo and competing reservations from invalidating a live scalar preview.
static void test_history_gesture_exclusion(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "gesture exclusion state");
    state->engine = engine_create(&cfg); CHECK(state->engine != NULL, "gesture exclusion engine");
    undo_manager_init(&state->undo);
    CHECK(engine_add_midi_clip_to_track(state->engine, 0, 0, 512, NULL), "gesture exclusion clip");
    timeline_selection_set_single(state, 0, 0);
    state->inspector.track_index = 0; state->inspector.clip_index = 0;
    CHECK(inspector_input_begin_clip_drag(state) && engine_clip_set_gain(state->engine, 0, 0, 0.5f), "first gain");
    inspector_input_finish_clip_drag(state);
    CHECK(inspector_input_begin_clip_drag(state) && engine_clip_set_gain(state->engine, 0, 0, 0.25f), "second gain");
    inspector_input_finish_clip_drag(state);
    CHECK(undo_manager_undo(&state->undo, state) && undo_manager_can_undo(&state->undo) && undo_manager_can_redo(&state->undo),
        "gesture exclusion history fixture");
    CHECK(inspector_input_begin_clip_drag(state) && engine_clip_set_gain(state->engine, 0, 0, 0.8f), "live gain preview");
    UndoCommand competing = {.type = UNDO_CMD_CLIP_RENAME};
    CHECK(!undo_manager_begin_drag(&state->undo, &competing) &&
        state->undo.active_drag.type == UNDO_CMD_CLIP_TRANSFORM &&
        state->undo.active_drag.data.clip_transform.before.gain == 0.5f, "competing begin replaced history");
    const EngineMixState* active = engine_render_mix_state(state->engine);
    CHECK(!undo_manager_can_undo(&state->undo) && !undo_manager_can_redo(&state->undo) &&
        !undo_manager_undo(&state->undo, state) && !undo_manager_redo(&state->undo, state), "history stepped during gesture");
    SDL_Event key = {.type = SDL_KEYDOWN}; key.key.keysym.sym = SDLK_z; key.key.keysym.mod = KMOD_GUI;
    input_manager_handle_event(&state->input_manager, state, &key);
    key.key.keysym.mod = KMOD_GUI | KMOD_SHIFT;
    input_manager_handle_event(&state->input_manager, state, &key);
    CHECK(state->undo.undo_count == 1 && state->undo.redo_count == 1 && state->undo.active_drag_valid &&
        engine_render_mix_state(state->engine) == active && engine_get_tracks(state->engine)[0].clips[0].gain == 0.8f &&
        state->selection_count == 1, "blocked history altered preview or cursor");
    inspector_input_finish_clip_drag(state);
    CHECK(state->undo.undo_count == 2 && state->undo.redo_count == 0 && undo_manager_undo(&state->undo, state) &&
        engine_get_tracks(state->engine)[0].clips[0].gain == 0.5f, "committed gesture lost undo baseline");
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state);
}

// Sweeps duplicate/delete preparation failures and verifies complete mixed-media retained history.
static void test_content_selection_edit(bool duplicate) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg); Engine* engine = engine_create(&cfg);
    char path[] = "/tmp/daw-selection-edit-XXXXXX"; int fd = mkstemp(path); CHECK(fd >= 0, "selection path"); close(fd);
    float samples[1024] = {0}; CHECK(wav_write_f32(path, samples, 1024, 1, cfg.sample_rate), "selection media");
    CHECK(engine && engine_add_clip_to_track(engine, 0, path, 100, NULL) &&
        engine_clip_set_region(engine, 0, 0, 0, 512) && engine_add_midi_clip_to_track(engine, 1, 200, 256, NULL), "selection fixture");
    EngineMidiNote note = {.start_frame = 20, .duration_frames = 100, .note = 64, .velocity = 0.7f};
    CHECK(engine_clip_midi_add_note(engine, 1, 0, note, NULL) &&
        engine_clip_add_automation_point(engine, 1, 0, ENGINE_AUTOMATION_TARGET_VOLUME, 0, 0.5f, NULL), "selection MIDI content");
    uint64_t ids[2] = {engine->tracks[0].clips[0].creation_index, engine->tracks[1].clips[0].creation_index};
    uint64_t outputs[2] = {UINT64_MAX, UINT64_MAX};
    EngineClipContentSnapshot* before = NULL; EngineClipContentSnapshot* after = NULL;
    EngineClip* original = engine->tracks[0].clips;
    const EngineMixState* active = engine_render_mix_state(engine);
    reject_publication = true; capacity_allocation_count = 0; publication_attempts = 0;
    CHECK(!engine_clip_content_edit(engine, ids, 2, duplicate, 32, outputs, &before, &after) && publication_attempts == 1,
        "selection publication rejection");
    int allocations = capacity_allocation_count; reject_publication = false;
    for (int fail = 1; fail <= allocations; ++fail) {
        capacity_allocation_count = 0; capacity_allocation_fail = fail;
        CHECK(!engine_clip_content_edit(engine, ids, 2, duplicate, 32, outputs, &before, &after), "selection allocation rejection");
        CHECK(!before && !after && !engine->clip_history_snapshots && engine->tracks[0].clips == original &&
            engine->tracks[1].clip_count == 1 && engine_render_mix_state(engine) == active && outputs[0] == UINT64_MAX,
            "selection failure exposed partial content history or results");
    }
    capacity_allocation_fail = 0;
    uint64_t invalid[2] = {ids[0], UINT64_MAX};
    CHECK(!engine_clip_content_edit(engine, invalid, 2, duplicate, 32, outputs, &before, &after), "missing later target accepted");
    invalid[1] = ids[0];
    CHECK(!engine_clip_content_edit(engine, invalid, 2, duplicate, 32, outputs, &before, &after), "duplicate target accepted");
    publication_attempts = 0;
    CHECK(engine_clip_content_edit(engine, ids, 2, duplicate, 32, outputs, &before, &after) && publication_attempts == 1,
        "selection single publication commit");
    CHECK(engine->tracks[0].clip_count == (duplicate ? 2 : 0) && engine->tracks[1].clip_count == (duplicate ? 2 : 0), "partial selection commit");
    if (duplicate) CHECK(engine->tracks[1].clips[1].midi_notes.note_count == 1 &&
        engine->tracks[1].clips[1].automation_lane_count == 1 && engine->tracks[1].clips[1].timeline_start_frames == 488 &&
        engine->tracks[1].clips[1].creation_index == outputs[1], "duplicate MIDI fields or placement lost");
    CHECK(unlink(path) == 0 && engine_clip_content_restore(engine, before), "selection undo without source file");
    CHECK(engine->tracks[0].clip_count == 1 && engine->tracks[1].clips[0].creation_index == ids[1] &&
        engine->tracks[1].clips[0].midi_notes.note_count == 1, "selection restore lost identity or content");
    CHECK(engine_clip_content_restore(engine, after), "selection redo");
    CHECK(engine_clip_content_restore(engine, before), "selection restore for action rejection");
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "selection action state");
    state->engine = engine; undo_manager_init(&state->undo);
    timeline_selection_add(state, 0, 0); timeline_selection_add(state, 1, 0);
    CHECK(timeline_selection_duplicate(state) && undo_manager_undo(&state->undo, state), "selection redo fixture");
    TimelineSelectionEntry selection[2]; memcpy(selection, state->selection, sizeof(selection));
    active = engine_render_mix_state(engine); reject_publication = true;
    CHECK(!timeline_selection_duplicate(state), "application duplicate rejection");
    timeline_selection_delete(state);
    CHECK(state->undo.undo_count == 0 && state->undo.redo_count == 1 && !state->undo.active_drag_valid &&
        !memcmp(selection, state->selection, sizeof(selection)) && engine_render_mix_state(engine) == active &&
        engine->tracks[0].clip_count == 1 && engine->tracks[1].clip_count == 1, "rejected action consumed history or selection");
    reject_publication = false;
    CHECK(undo_manager_redo(&state->undo, state), "rejected action destroyed redo");
    undo_manager_free(&state->undo); free(state);
    engine_clip_content_release(before); engine_clip_content_release(after); engine_destroy(engine);
}

// Preserves the last complete clipboard across candidate allocation and later-entry ownership failures.
static void test_clipboard_replacement_failure(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "clipboard state");
    state->engine = engine_create(&cfg); undo_manager_init(&state->undo);
    CHECK(state->engine && engine_add_midi_clip_to_track(state->engine, 0, 100, 256, NULL) &&
        engine_add_midi_clip_to_track(state->engine, 0, 500, 256, NULL), "clipboard source regions");
    EngineMidiNote note = {.start_frame = 20, .duration_frames = 100, .note = 69, .velocity = 0.7f};
    CHECK(engine_clip_midi_add_note(state->engine, 0, 0, note, NULL) &&
        engine_clip_midi_add_note(state->engine, 0, 1, note, NULL), "clipboard source notes");
    timeline_selection_set_single(state, 0, 0); tested_clipboard_copy(state);
    timeline_selection_add(state, 0, 1); timeline_selection_set_primary(state, 0, 0);
    const EngineMixState* revision = engine_render_mix_state(state->engine);
    capacity_allocation_count = 0; capacity_allocation_fail = 1;
    tested_clipboard_copy(state); capacity_allocation_fail = 0;
    CHECK(g_timeline_clipboard.count == 1 && g_timeline_clipboard.anchor_start_frame == 100,
        "clipboard allocation failure destroyed old clipboard");
    clipboard_copy_count = 0; clipboard_copy_fail = 2;
    tested_clipboard_copy(state); clipboard_copy_fail = 0;
    CHECK(g_timeline_clipboard.count == 1 && g_timeline_clipboard.entries[0].clip.midi_note_count == 1 &&
        engine_render_mix_state(state->engine) == revision && state->selection_count == 2, "later copy failure partially replaced clipboard");
    state->selection[1].clip_index = 999; tested_clipboard_copy(state);
    CHECK(g_timeline_clipboard.count == 1, "invalid selection partially copied");
    timeline_selection_set_single(state, engine_add_track(state->engine), -1);
    tested_clipboard_paste(state);
    CHECK(engine_get_tracks(state->engine)[1].clip_count == 1 &&
        engine_get_tracks(state->engine)[1].clips[0].midi_notes.notes[0].note == 69, "failed copy did not preserve usable clipboard");
    timeline_selection_set_single(state, 0, 0); timeline_selection_add(state, 0, 1);
    tested_clipboard_copy(state);
    CHECK(g_timeline_clipboard.count == 2, "valid replacement did not publish complete clipboard");
    // Simulate a clipboard copied from a larger project, with destination-track creation rejected.
    g_timeline_clipboard.entries[0].track_index = 3;
    timeline_selection_clear(state); reject_publication = true;
    int track_count = engine_get_track_count(state->engine);
    int undo_count = state->undo.undo_count, redo_count = state->undo.redo_count;
    tested_clipboard_paste(state);
    CHECK(engine_get_track_count(state->engine) == track_count && state->selection_count == 0 &&
        state->undo.undo_count == undo_count && state->undo.redo_count == redo_count && !state->undo.active_drag_valid,
        "rejected paste track creation mutated state");
    reject_publication = false;
    tested_clipboard_paste(state);
    CHECK(engine_get_track_count(state->engine) == 4 && engine_get_tracks(state->engine)[3].clip_count == 2 &&
        state->selection_count == 2, "complete generated-track paste");
    CHECK(engine_track_set_gain(state->engine, 3, 0.5f) && !undo_manager_undo(&state->undo, state), "paste undo discarded track settings");
    CHECK(engine_track_set_gain(state->engine, 3, 1), "restore paste track gain");
    FxInstId effect = engine_fx_track_add(state->engine, 3, 1);
    CHECK(effect && !undo_manager_undo(&state->undo, state), "paste undo discarded new effect");
    CHECK(engine_fx_track_remove(state->engine, 3, effect), "remove pasted track effect fixture");
    int extra = -1;
    CHECK(engine_add_midi_clip_to_track(state->engine, 3, 2000, 256, &extra) && !undo_manager_undo(&state->undo, state),
        "paste undo discarded unrelated clip");
    CHECK(engine_remove_clip(state->engine, 3, extra), "remove unrelated paste fixture");
    for (int i = 0; i < 3; ++i) {
        CHECK(undo_manager_undo(&state->undo, state) && engine_get_track_count(state->engine) == track_count &&
            state->selection_count == 0, "paste undo topology or selection");
        CHECK(undo_manager_redo(&state->undo, state) && engine_get_track_count(state->engine) == 4 &&
            state->selection_count == 2, "paste redo topology or selection");
    }
    timeline_clipboard_clear(&g_timeline_clipboard);
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state);
}

// Rejects partial mixed-media paste preparation and tests retained insertion history across topology changes.
static void test_content_insert_transaction(bool growth, bool empty) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg); Engine* engine = engine_create(&cfg);
    CHECK(engine != NULL, "insert engine");
    if (empty) CHECK(engine_remove_track(engine, 0), "empty insertion project");
    char path[] = "/tmp/daw-content-insert-XXXXXX"; int fd = mkstemp(path); CHECK(fd >= 0, "insert media path"); close(fd);
    float samples[1024]; for (int i = 0; i < 1024; ++i) samples[i] = 0.25f;
    CHECK(wav_write_f32(path, samples, 1024, 1, cfg.sample_rate), "insert media");
    EngineMidiNote note = {.start_frame = 20, .duration_frames = 100, .note = 64, .velocity = 0.7f};
    EngineAutomationPoint point = {.frame = 0, .value = 0.5f};
    EngineAutomationLane lane = {.target = ENGINE_AUTOMATION_TARGET_VOLUME, .points = &point, .point_count = 1};
    EngineClipInsert entries[2] = {
        {.kind = ENGINE_CLIP_KIND_AUDIO, .media_path = path, .name = "Inserted audio",
         .transform = {.start_frame = 100, .offset_frames = 64, .duration_frames = 256, .gain = 0.75f, .fade_in_frames = 16}},
        {.kind = ENGINE_CLIP_KIND_MIDI, .name = "Inserted MIDI", .automation_lanes = &lane, .automation_lane_count = 1,
         .transform = {.start_frame = 800, .duration_frames = 256, .gain = 1, .midi_notes = &note, .midi_note_count = 1,
             .instrument_inherits_track = true}}
    };
    int destination = growth ? 2 : 0, previous_count = engine->track_count;
    uint64_t outputs[2] = {UINT64_MAX, UINT64_MAX};
    EngineClipContentSnapshot* before = NULL; EngineClipContentSnapshot* after = NULL;
    const EngineMixState* revision = engine_render_mix_state(engine); EffectsManager* fx = engine->fxm;
    reject_publication = true; capacity_allocation_count = 0; publication_attempts = 0;
    CHECK(!engine_clip_content_insert(engine, destination, entries, 2, outputs, &before, &after) && publication_attempts == 1,
        "insert publication rejection");
    int allocations = capacity_allocation_count; reject_publication = false;
    for (int fail = 1; fail <= allocations; ++fail) {
        capacity_allocation_count = 0; capacity_allocation_fail = fail;
        CHECK(!engine_clip_content_insert(engine, destination, entries, 2, outputs, &before, &after), "insert allocation rejection");
        CHECK(engine->track_count == previous_count && (!previous_count || engine->tracks[0].clip_count == 0) && !before && !after &&
            engine_render_mix_state(engine) == revision && engine->fxm == fx && !engine->clip_history_snapshots && outputs[0] == UINT64_MAX,
            "insert allocation exposed partial topology content or output");
    }
    capacity_allocation_fail = 0;
    entries[1].transform.gain = NAN;
    CHECK(!engine_clip_content_insert(engine, destination, entries, 2, outputs, &before, &after) &&
        engine->track_count == previous_count && (!previous_count || engine->tracks[0].clip_count == 0), "invalid later descriptor partially inserted");
    entries[1].transform.gain = 1;
    entries[1].kind = ENGINE_CLIP_KIND_AUDIO; entries[1].media_path = "/no-such-daw-paste-source.wav";
    CHECK(!engine_clip_content_insert(engine, destination, entries, 2, outputs, &before, &after) &&
        engine->track_count == previous_count && engine_render_mix_state(engine) == revision && !before && !after,
        "later media failure partially inserted");
    entries[1].kind = ENGINE_CLIP_KIND_MIDI; entries[1].media_path = NULL;
    publication_attempts = 0;
    CHECK(engine_clip_content_insert(engine, destination, entries, 2, outputs, &before, &after) && publication_attempts == 1,
        "complete insertion publication");
    const EngineClip* inserted = engine->tracks[destination].clips;
    CHECK(inserted[0].offset_frames == 64 && inserted[0].gain == 0.75f && inserted[0].fade_in_frames == 16 &&
        inserted[1].midi_notes.note_count == 1 && inserted[1].automation_lane_count == 1 &&
        inserted[1].creation_index == outputs[1], "inserted metadata incomplete");
    float rendered[256] = {0}; engine_graph_render_track(engine_render_source_graph(engine), rendered, 128, 100, destination);
    float peak = 0; for (int i = 0; i < 256; ++i) if (fabsf(rendered[i]) > peak) peak = fabsf(rendered[i]);
    CHECK(peak > 0.01f, "inserted audio source silent");
    CHECK(unlink(path) == 0, "remove inserted source");
    reject_publication = true;
    CHECK(!engine_clip_content_restore(engine, before) && engine->tracks[destination].clip_count == 2, "insert undo rejection");
    reject_publication = false;
    CHECK(engine_clip_content_restore(engine, before) && engine->track_count == previous_count && (!previous_count || engine->tracks[0].clip_count == 0),
        "insert undo did not restore topology");
    CHECK(engine_clip_content_restore(engine, after) && engine->tracks[destination].clip_count == 2, "insert redo reopened source");
    int outside = engine_add_track(engine); CHECK(outside >= 0 && engine_move_clip_to_track(engine, destination, 0, outside, 100, NULL),
        "move inserted clip outside history");
    CHECK(!engine_clip_content_restore(engine, before) && engine->tracks[outside].clip_count == 1,
        "insertion undo ignored moved identity");
    engine_clip_content_release(before); engine_clip_content_release(after); engine_destroy(engine);
}

// Rejects history storage growth before a UI gesture changes the project.
static bool reject_history_allocation;
static void* history_realloc(void* ptr, size_t bytes) {
    return reject_history_allocation ? NULL : realloc(ptr, bytes);
}
#define realloc history_realloc
#include "../src/undo/undo_manager_stack.c"
#undef realloc

// Exercises real effects handlers with exhausted history and unrelated/stale reservations.
static void test_effects_history_reservation(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "effects state");
    state->engine = engine_create(&cfg); CHECK(state->engine != NULL, "effects engine");
    undo_manager_init(&state->undo);
    state->effects_panel.target = FX_PANEL_TARGET_TRACK;
    state->effects_panel.target_track_index = 0;
    EffectsPanelLayout layout = {0};
    layout.track_snapshot.gain_hit_rect = (SDL_Rect){100, 100, 100, 20};
    SDL_Event down = {0}; down.type = SDL_MOUSEBUTTONDOWN; down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 180; down.button.y = 110;
    SDL_Event up = down; up.type = SDL_MOUSEBUTTONUP;
    float gain = engine_get_tracks(state->engine)[0].gain;
    reject_history_allocation = true;
    CHECK(effects_panel_track_snapshot_handle_mouse_down(state, &layout, &down), "refused gain consumed");
    CHECK(!state->effects_panel.track_snapshot.dragging && !state->undo.active_drag_valid &&
          engine_get_tracks(state->engine)[0].gain == gain, "allocation refusal edited gain");
    state->effects_panel.chain_count = 1;
    CHECK(!begin_fx_param_drag(state, 0, 0), "FX ignored history allocation failure");
    reject_history_allocation = false;
    UndoCommand other = {.type = UNDO_CMD_NONE};
    CHECK(undo_manager_begin_drag(&state->undo, &other), "unrelated reservation");
    uint64_t serial = state->undo.drag_serial;
    effects_panel_track_snapshot_handle_mouse_down(state, &layout, &down);
    CHECK(!state->effects_panel.track_snapshot.dragging && engine_get_tracks(state->engine)[0].gain == gain,
          "conflicting gain changed project");
    CHECK(!begin_fx_param_drag(state, 0, 0), "FX stole reservation");
    effects_panel_track_snapshot_handle_mouse_up(state, &up);
    state->effects_panel.eq_detail.dragging = true; // A stale UI flag must not imply ownership.
    state->effects_panel.eq_detail.pending_apply = true;
    effects_panel_eq_detail_handle_mouse_up(state, &up);
    CHECK(state->undo.active_drag_valid && state->undo.drag_serial == serial, "release consumed another gesture");
    undo_manager_cancel_drag(&state->undo);
    CHECK(effects_panel_track_snapshot_handle_mouse_down(state, &layout, &down), "first reservation lifetime");
    uint64_t stale_serial = state->effects_panel.track_snapshot.history_serial;
    undo_manager_cancel_drag(&state->undo);
    CHECK(undo_manager_begin_drag(&state->undo, &other) && state->undo.drag_serial != stale_serial,
          "reservation identity reused");
    effects_panel_track_snapshot_handle_mouse_up(state, &up);
    CHECK(state->undo.active_drag_valid && state->undo.active_drag.type == UNDO_CMD_NONE,
          "stale track release consumed newer reservation");
    undo_manager_cancel_drag(&state->undo);
    CHECK(engine_track_set_gain(state->engine, 0, gain), "restore stale-preview fixture");
    CHECK(effects_panel_track_snapshot_handle_mouse_down(state, &layout, &down), "accepted gain");
    CHECK(state->effects_panel.track_snapshot.dragging && engine_get_tracks(state->engine)[0].gain != gain,
          "gain did not apply");
    effects_panel_track_snapshot_handle_mouse_up(state, &up);
    CHECK(state->undo.undo_count == 1 && undo_manager_undo(&state->undo, state) &&
          engine_get_tracks(state->engine)[0].gain == gain, "gain lost undo baseline");
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state);
}

// Keeps UI state and history in place when an EQ undo cannot publish its engine state.
static void test_eq_history_rejection(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "EQ state");
    state->engine = engine_create(&cfg); CHECK(state->engine != NULL, "EQ engine");
    undo_manager_init(&state->undo);
    UndoCommand command = {.type = UNDO_CMD_EQ_CURVE}; command.data.eq_curve_edit.is_master = true;
    command.data.eq_curve_edit.before.bands[0].enabled = true;
    command.data.eq_curve_edit.before.bands[0].freq_hz = 1000;
    command.data.eq_curve_edit.before.bands[0].gain_db = 6;
    command.data.eq_curve_edit.before.bands[0].q_width = 1;
    CHECK(undo_manager_push(&state->undo, &command), "EQ history");
    EqCurveState before = state->effects_panel.eq_curve_master;
    reject_publication = true;
    CHECK(!undo_manager_undo(&state->undo, state) && state->undo.undo_count == 1 && state->undo.redo_count == 0 &&
          memcmp(&before, &state->effects_panel.eq_curve_master, sizeof(before)) == 0, "EQ rejection consumed history or UI");
    reject_publication = false;
    CHECK(undo_manager_undo(&state->undo, state), "EQ retry");
    undo_manager_clear(&state->undo);
    EngineEqCurve accepted;
    CHECK(engine_get_eq_curve(state->engine, -1, &accepted), "EQ accepted readback");
    command.data.eq_curve_edit.before = command.data.eq_curve_edit.after = (SessionEqCurve){0};
    command.data.eq_curve_edit.before.low_cut.enabled = accepted.low_cut.enabled;
    command.data.eq_curve_edit.before.low_cut.freq_hz = accepted.low_cut.freq_hz;
    command.data.eq_curve_edit.before.high_cut.enabled = accepted.high_cut.enabled;
    command.data.eq_curve_edit.before.high_cut.freq_hz = accepted.high_cut.freq_hz;
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i) {
        command.data.eq_curve_edit.before.bands[i].enabled = accepted.bands[i].enabled;
        command.data.eq_curve_edit.before.bands[i].freq_hz = accepted.bands[i].freq_hz;
        command.data.eq_curve_edit.before.bands[i].gain_db = accepted.bands[i].gain_db;
        command.data.eq_curve_edit.before.bands[i].q_width = accepted.bands[i].q_width;
    }
    command.data.eq_curve_edit.after = command.data.eq_curve_edit.before;
    CHECK(undo_manager_begin_drag(&state->undo, &command), "EQ release reservation");
    state->effects_panel.eq_detail.history_serial = state->undo.drag_serial;
    state->effects_panel.eq_detail.dragging = true;
    state->effects_panel.eq_detail.pending_apply = true;
    state->effects_panel.eq_detail.view_mode = EQ_DETAIL_VIEW_MASTER;
    state->effects_panel.eq_curve = state->effects_panel.eq_curve_master;
    state->effects_panel.eq_curve.bands[0].gain_db = 12;
    SDL_Event up = {0}; up.type = SDL_MOUSEBUTTONUP; up.button.button = SDL_BUTTON_LEFT;
    reject_publication = true;
    CHECK(effects_panel_eq_detail_handle_mouse_up(state, &up), "EQ rejected release");
    reject_publication = false;
    CHECK(!state->undo.active_drag_valid && state->undo.undo_count == 0 &&
          state->effects_panel.eq_curve.bands[0].gain_db == accepted.bands[0].gain_db,
          "rejected final EQ draft was committed");
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state);
}

// Rejects typed edits when selection changes while preserving both original and new targets.
static void test_inspector_edit_target_change(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "target state");
    state->engine = engine_create(&cfg); CHECK(state->engine != NULL, "target engine");
    undo_manager_init(&state->undo);
    CHECK(engine_add_midi_clip_to_track(state->engine, 0, 100, 1000, NULL) &&
          engine_add_midi_clip_to_track(state->engine, 0, 200, 1000, NULL), "target clips");
    state->inspector.track_index = 0; state->inspector.clip_index = 0;
    inspector_numeric_begin_edit(state, &engine_get_tracks(state->engine)[0].clips[0],
                                 &state->inspector.edit.editing_timeline_start);
    strcpy(state->inspector.edit.timeline_start, "0.020");
    state->inspector.clip_index = 1;
    CHECK(!inspector_numeric_commit_edit(state) && state->undo.undo_count == 0 &&
          engine_get_tracks(state->engine)[0].clips[0].timeline_start_frames == 100 &&
          engine_get_tracks(state->engine)[0].clips[1].timeline_start_frames == 200 &&
          !strcmp(state->inspector.edit.timeline_start, "0.020"), "numeric edit retargeted");
    state->inspector.clip_index = 0;
    inspector_input_begin_rename(state); strcpy(state->inspector.name, "Must not retarget");
    state->inspector.clip_index = 1;
    inspector_input_commit_if_editing(state);
    CHECK(state->inspector.editing_name && state->undo.undo_count == 0 &&
          strcmp(engine_get_tracks(state->engine)[0].clips[1].name, "Must not retarget"), "rename retargeted");
    undo_manager_free(&state->undo); engine_destroy(state->engine); free(state);
}

// Proves complete track undo retains neighbors, media, identities, FX and automation across failures.
static void test_whole_track_history(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "track history state");
    Engine* engine = state->engine = engine_create(&cfg); CHECK(engine != NULL, "track history engine");
    undo_manager_init(&state->undo);
    CHECK(engine_add_track(engine) == 1 && engine_add_midi_clip_to_track(engine, 0, 0, 4096, NULL) &&
          engine_add_midi_clip_to_track(engine, 1, 0, 4096, NULL), "track history fixture");
    EngineMidiNote note = {.duration_frames = 512, .note = 65, .velocity = 0.7f};
    CHECK(engine_clip_midi_add_note(engine, 0, 0, note, NULL), "track history notes");
    EngineAutomationPoint points[] = {{0, 0.4f}, {512, 0.8f}};
    CHECK(engine_track_midi_set_instrument_automation_lane_points(engine, 0,
          ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL, points, 2) &&
          engine_clip_add_automation_point(engine, 0, 0, ENGINE_AUTOMATION_TARGET_VOLUME, 0, 0.5f, NULL),
          "track history automation");
    char path[] = "/tmp/daw-track-history-XXXXXX"; int fd = mkstemp(path); CHECK(fd >= 0, "track media"); close(fd);
    float samples[128]; for (int i = 0; i < 128; ++i) samples[i] = 0.25f;
    CHECK(wav_write_f32(path, samples, 128, 1, cfg.sample_rate) &&
          engine_add_clip_to_track(engine, 0, path, 8192, NULL), "track audio");
    FxInstId fx = engine_fx_track_add(engine, 0, 1), neighbor_fx = engine_fx_track_add(engine, 1, 1);
    CHECK(fx && neighbor_fx && engine_fx_track_set_param(engine, 0, fx, 0, 0.5f) &&
          engine_track_set_name(engine, 0, "Retained track") && engine_track_set_gain(engine, 0, 0.75f), "track settings");
    uint64_t track_id = engine->tracks[0].runtime_id, neighbor_id = engine->tracks[1].runtime_id;
    uint64_t midi_id = engine->tracks[0].clips[0].creation_index, audio_id = engine->tracks[0].clips[1].creation_index;
    EngineMixState* revision = engine_render_mix_state(engine);
    reject_history_allocation = true;
    CHECK(!undo_manager_edit_track(state, 0, false) && engine->track_count == 2 &&
          engine_render_mix_state(engine) == revision && !engine->clip_history_snapshots, "track history allocation mutated project");
    reject_history_allocation = false;
    reject_publication = true; capacity_allocation_count = 0;
    CHECK(!undo_manager_edit_track(state, 0, false), "track deletion publication failure");
    int captures = capacity_allocation_count;
    reject_publication = false;
    for (int fail = 1; fail <= captures; ++fail) {
        capacity_allocation_count = 0; capacity_allocation_fail = fail;
        CHECK(!undo_manager_edit_track(state, 0, false) && engine->track_count == 2 &&
              engine->tracks[0].runtime_id == track_id && engine->tracks[1].runtime_id == neighbor_id &&
              engine_render_mix_state(engine) == revision && state->undo.undo_count == 0 && !engine->clip_history_snapshots,
              "track capture allocation exposed mutation");
    }
    capacity_allocation_fail = 0;
    CHECK(undo_manager_edit_track(state, 0, false) && engine->track_count == 1 &&
          engine->tracks[0].runtime_id == neighbor_id && state->undo.undo_count == 1, "track deletion");
    unlink(path); // Undo must use retained decoded media rather than reopening this file.
    revision = engine_render_mix_state(engine);
    reject_publication = true; capacity_allocation_count = 0;
    CHECK(!undo_manager_undo(&state->undo, state), "track restore publication failure");
    int restores = capacity_allocation_count;
    reject_publication = false;
    for (int fail = 1; fail <= restores; ++fail) {
        capacity_allocation_count = 0; capacity_allocation_fail = fail;
        CHECK(!undo_manager_undo(&state->undo, state) && engine->track_count == 1 &&
              engine->tracks[0].runtime_id == neighbor_id && engine_render_mix_state(engine) == revision &&
              state->undo.undo_count == 1 && state->undo.redo_count == 0, "track restore allocation exposed partial state");
    }
    capacity_allocation_fail = 0;
    reject_midi_allocation = true;
    CHECK(!undo_manager_undo(&state->undo, state) && engine->track_count == 1 &&
          engine_render_mix_state(engine) == revision, "track note-copy failure exposed mutation");
    reject_midi_allocation = false;
    automation_malloc_count = 0; automation_malloc_fail = 1;
    CHECK(!undo_manager_undo(&state->undo, state) && engine->track_count == 1 &&
          engine_render_mix_state(engine) == revision, "track automation-copy failure exposed mutation");
    automation_malloc_fail = 0;
    for (int repeat = 0; repeat < 3; ++repeat) {
        publication_attempts = 0;
        CHECK(undo_manager_undo(&state->undo, state) && publication_attempts == 1 && engine->track_count == 2 &&
              engine->tracks[0].runtime_id == track_id && engine->tracks[1].runtime_id == neighbor_id,
              "track restore replaced neighbor or published incrementally");
        const EngineTrack* restored = &engine->tracks[0];
        CHECK(restored->clip_count == 2 && restored->clips[0].creation_index == midi_id &&
              restored->clips[1].creation_index == audio_id && restored->clips[0].midi_notes.note_count == 1 &&
              restored->clips[0].automation_lane_count == 1 && restored->midi_instrument_automation_lane_count == 1 &&
              restored->midi_instrument_automation_lanes[0].point_count == 2 && restored->gain == 0.75f &&
              !strcmp(restored->name, "Retained track"), "track content loss");
        FxMasterSnapshot chain = {0}, neighbor = {0};
        CHECK(engine_fx_track_snapshot(engine, 0, &chain) && engine_fx_track_snapshot(engine, 1, &neighbor) &&
              chain.count == 1 && chain.items[0].id == fx && neighbor.count == 1 && neighbor.items[0].id == neighbor_fx,
              "track FX identity changed");
        if (repeat < 2) CHECK(undo_manager_redo(&state->undo, state) && engine->track_count == 1, "track deletion redo");
    }
    CHECK(undo_manager_edit_track(state, 1, true) && engine->track_count == 3 && engine->tracks[2].runtime_id == neighbor_id,
          "track insertion displaced neighbor");
    uint64_t added_id = engine->tracks[1].runtime_id;
    CHECK(engine_track_set_gain(engine, 1, 0.3f) && !undo_manager_undo(&state->undo, state) &&
          engine->track_count == 3 && engine->tracks[1].gain == 0.3f, "track undo discarded unrecorded setting");
    CHECK(engine_track_set_gain(engine, 1, 1.0f), "restore guarded setting");
    CHECK(undo_manager_undo(&state->undo, state) && engine->track_count == 2 &&
          undo_manager_redo(&state->undo, state) && engine->tracks[1].runtime_id == added_id &&
          engine->tracks[2].runtime_id == neighbor_id, "empty track identity history");
    CHECK(undo_manager_undo(&state->undo, state), "remove added row for topology guard");
    CHECK(engine_insert_track(engine, 1) && !undo_manager_redo(&state->undo, state) &&
          engine->track_count == 3 && engine->tracks[2].runtime_id == neighbor_id, "track redo ignored changed neighbors");
    CHECK(engine_remove_track(engine, 1) && undo_manager_redo(&state->undo, state), "track redo after topology restored");
    // Destroying the project before releasing history must invalidate media pins safely.
    engine_destroy(engine); state->engine = NULL;
    undo_manager_free(&state->undo); free(state);
}

// Checks effect deletion undo is atomic, preserves disabled state and identity, and follows its original track.
static void test_effect_restore_history(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "effect restore state");
    Engine* engine = state->engine = engine_create(&cfg); CHECK(engine != NULL, "effect restore engine");
    undo_manager_init(&state->undo);
    FxInstId id = engine_fx_track_add(engine, 0, 1);
    CHECK(id && engine_fx_track_set_param(engine, 0, id, 0, 0.4f) &&
          engine_fx_track_set_enabled(engine, 0, id, false), "effect restore fixture");
    FxMasterSnapshot original;
    CHECK(engine_fx_track_snapshot(engine, 0, &original), "effect restore snapshot");
    UndoCommand command = {.type = UNDO_CMD_FX_EDIT};
    command.data.fx_edit.target = UNDO_FX_TARGET_TRACK;
    command.data.fx_edit.track_index = 0; command.data.fx_edit.kind = UNDO_FX_EDIT_REMOVE;
    command.data.fx_edit.id = id; command.data.fx_edit.before_index = 0;
    SessionFxInstance* saved = &command.data.fx_edit.before_state;
    saved->type = original.items[0].type; saved->enabled = original.items[0].enabled;
    saved->param_count = original.items[0].param_count;
    for (uint32_t p = 0; p < saved->param_count; ++p) {
        saved->params[p] = original.items[0].params[p]; saved->param_mode[p] = original.items[0].param_mode[p];
        saved->param_beats[p] = original.items[0].param_beats[p];
    }
    CHECK(undo_command_bind_track(state, &command) && undo_manager_push(&state->undo, &command) &&
          engine_fx_track_remove(engine, 0, id) && engine_insert_track(engine, 0), "effect removal and row shift");
    EngineMixState* revision = engine_render_mix_state(engine);
    reject_publication = true;
    CHECK(!undo_manager_undo(&state->undo, state) && engine_render_mix_state(engine) == revision &&
          state->undo.undo_count == 1 && state->undo.redo_count == 0, "effect rejection changed revision/history");
    reject_publication = false;
    FxMasterSnapshot empty;
    CHECK(engine_fx_track_snapshot(engine, 1, &empty) && empty.count == 0, "partial default effect escaped");
    for (int n = 0; n < 3; ++n) {
        publication_attempts = 0;
        CHECK(undo_manager_undo(&state->undo, state) && publication_attempts == 1, "effect restore was not one publication");
        FxMasterSnapshot restored, untouched;
        CHECK(engine_fx_track_snapshot(engine, 1, &restored) && engine_fx_track_snapshot(engine, 0, &untouched) &&
              restored.count == 1 && !memcmp(&restored, &original, sizeof(original)) && untouched.count == 0,
              "effect restore lost identity/state or targeted neighbor");
        if (n < 2) CHECK(undo_manager_redo(&state->undo, state), "effect remove redo");
    }
    CHECK(engine_remove_track(engine, 1) && !undo_manager_redo(&state->undo, state), "effect history accepted removed target");
    undo_manager_free(&state->undo); engine_destroy(engine);
    free(state->effects_panel.eq_curve_tracks); free(state->effects_panel.last_open_track_fx_ids); free(state);
}

// Keeps the effects gesture target fixed through a selection change and resolves scalar history after insertion.
static void test_effect_gesture_target(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "effect target state");
    Engine* engine = state->engine = engine_create(&cfg); CHECK(engine && engine_add_track(engine) == 1, "effect target engine");
    undo_manager_init(&state->undo);
    state->selected_track_index = 0;
    effects_panel_sync_from_engine(state);
    EffectsPanelLayout layout = {0}; layout.track_snapshot.gain_hit_rect = (SDL_Rect){100, 100, 100, 20};
    SDL_Event down = {0}; down.type = SDL_MOUSEBUTTONDOWN; down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 180; down.button.y = 110;
    CHECK(effects_panel_track_snapshot_handle_mouse_down(state, &layout, &down), "start owned track gesture");
    state->selected_track_index = 1;
    effects_panel_sync_from_engine(state);
    CHECK(state->effects_panel.target_track_index == 0 && engine->tracks[1].gain == 1.0f, "selection redirected gesture");
    SDL_Event up = down; up.type = SDL_MOUSEBUTTONUP;
    effects_panel_track_snapshot_handle_mouse_up(state, &up);
    CHECK(engine_insert_track(engine, 0) && undo_manager_undo(&state->undo, state) &&
          engine->tracks[1].gain == 1.0f && engine->tracks[0].gain == 1.0f, "scalar history retargeted inserted row");
    undo_manager_free(&state->undo); engine_destroy(engine);
    free(state->effects_panel.eq_curve_tracks); free(state->effects_panel.last_open_track_fx_ids); free(state);
}

// Covers pre-reserved effect actions and truthful refusal state through their production entry points.
static void test_discrete_effect_actions(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "discrete state");
    Engine* engine = state->engine = engine_create(&cfg); CHECK(engine != NULL, "discrete engine");
    undo_manager_init(&state->undo);
    EngineMixState* baseline = engine_render_mix_state(engine);
    reject_history_allocation = true;
    CHECK(!undo_manager_add_effect(state, 0, 1) && engine_render_mix_state(engine) == baseline &&
          undo_manager_rejection_message(&state->undo), "effect history allocation failed without refusal");
    reject_history_allocation = false;
    reject_publication = true;
    CHECK(!undo_manager_add_effect(state, 0, 1) && engine_render_mix_state(engine) == baseline &&
          state->undo.undo_count == 0 && !state->undo.active_drag_valid, "rejected effect add left history/project");
    reject_publication = false;
    publication_attempts = 0;
    FxInstId id = undo_manager_add_effect(state, 0, 1);
    CHECK(id && publication_attempts == 1 && state->undo.undo_count == 1 &&
          !undo_manager_rejection_message(&state->undo), "accepted effect add missing history");
    CHECK(undo_manager_undo(&state->undo, state) && undo_manager_redo(&state->undo, state), "effect add round trip");
    FxMasterSnapshot snapshot;
    CHECK(engine_fx_track_snapshot(engine, 0, &snapshot) && snapshot.count == 1 && snapshot.items[0].id == id,
          "effect add redo identity");
    state->selected_track_index = 0;
    effects_panel_sync_from_engine(state);
    float value = snapshot.items[0].params[0];
    int history_count = state->undo.undo_count;
    reject_publication = true;
    apply_slider_value(state, 0, 0, 0.3f);
    CHECK(state->undo.undo_count == history_count && state->effects_panel.chain[0].param_values[0] == value,
          "discrete rejected parameter changed UI/history");
    reject_publication = false;
    apply_slider_value(state, 0, 0, 0.3f);
    CHECK(state->undo.undo_count == history_count + 1 && undo_manager_undo(&state->undo, state) &&
          engine_fx_track_snapshot(engine, 0, &snapshot) && snapshot.items[0].params[0] == value, "discrete param undo");
    effects_panel_sync_from_engine(state);
    int capacity = state->undo.undo_capacity;
    state->undo.undo_capacity = state->undo.undo_count;
    reject_history_allocation = true;
    CHECK(!toggle_slot_enabled(state, &state->effects_panel, 0) &&
          engine_fx_track_snapshot(engine, 0, &snapshot) && snapshot.items[0].enabled, "bypass ignored reservation failure");
    reject_history_allocation = false; state->undo.undo_capacity = capacity;
    CHECK(toggle_slot_enabled(state, &state->effects_panel, 0) && undo_manager_undo(&state->undo, state) &&
          engine_fx_track_snapshot(engine, 0, &snapshot) && snapshot.items[0].enabled, "bypass undo");
    CHECK(engine_track_midi_set_instrument_preset(engine, 0, ENGINE_INSTRUMENT_PRESET_PURE_SINE), "mute instrument fixture");
    EngineInstrumentParams params = engine_track_midi_instrument_params(engine, 0);
    EffectsPanelLayout layout = {0}; layout.track_snapshot.mute_rect = (SDL_Rect){100, 100, 30, 20};
    SDL_Event down = {0}; down.type = SDL_MOUSEBUTTONDOWN; down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 110; down.button.y = 110;
    CHECK(effects_panel_track_snapshot_handle_mouse_down(state, &layout, &down) && engine->tracks[0].muted &&
          undo_manager_undo(&state->undo, state) && !engine->tracks[0].muted && engine->tracks[0].midi_instrument_enabled,
          "mute undo changed instrument enable");
    for (int p = 0; p < ENGINE_INSTRUMENT_PARAM_COUNT; ++p)
        CHECK(engine_instrument_params_get(params, (EngineInstrumentParamId)p) ==
              engine_instrument_params_get(engine->tracks[0].midi_instrument_params, (EngineInstrumentParamId)p),
              "mute undo changed instrument params");
    undo_manager_free(&state->undo); engine_destroy(engine);
    free(state->effects_panel.eq_curve_tracks); free(state->effects_panel.last_open_track_fx_ids); free(state);
}

// Exercises actual rack palette hit targets, engine readback, undo and rejected publication.
static void test_rack_spectrogram_palettes(void) {
    EngineRuntimeConfig cfg; config_set_defaults(&cfg);
    AppState* state = calloc(1, sizeof(*state)); CHECK(state != NULL, "palette state");
    state->engine = engine_create(&cfg); CHECK(state->engine != NULL, "palette engine");
    undo_manager_init(&state->undo);
    FxInstId meter = engine_fx_track_add(state->engine, 0, 105);
    CHECK(meter != 0, "palette meter");
    state->selected_track_index = 0;
    ui_init_panes(state);
    ui_layout_panes(state, 1280, 748);
    effects_panel_refresh_catalog(state);
    effects_panel_sync_from_engine(state);
    state->effects_panel.view_mode = FX_PANEL_VIEW_STACK;
    for (int mode = 0; mode < 3; ++mode) {
        EffectsPanelLayout layout; effects_panel_compute_layout(state, &layout);
        SDL_Rect buttons[3]; effects_panel_spectrogram_card_palette_rects(&layout.slots[0].body_rect, buttons);
        CHECK(buttons[mode].w > 0 && buttons[mode].h > 0, "palette button missing");
        SDL_Event event = {.type = SDL_MOUSEBUTTONDOWN};
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = buttons[mode].x + buttons[mode].w / 2;
        event.button.y = buttons[mode].y + buttons[mode].h / 2;
        effects_panel_input_handle_event(NULL, state, &event);
        FxMasterSnapshot fx;
        CHECK(engine_fx_track_snapshot(state->engine, 0, &fx) && fx.items[0].params[2] == mode &&
              state->effects_panel.chain[0].param_values[2] == mode, "rack palette click did not reach engine");
    }
    CHECK(undo_manager_undo(&state->undo, state), "palette undo");
    effects_panel_sync_from_engine(state); // Match the application readback before rendering.
    CHECK(state->effects_panel.chain[0].param_values[2] == 1, "palette undo display readback");
    EffectsPanelLayout layout; effects_panel_compute_layout(state, &layout);
    SDL_Rect buttons[3]; effects_panel_spectrogram_card_palette_rects(&layout.slots[0].body_rect, buttons);
    SDL_Event event = {.type = SDL_MOUSEBUTTONDOWN}; event.button.button = SDL_BUTTON_LEFT;
    event.button.x = buttons[2].x + 2; event.button.y = buttons[2].y + 2;
    int count = state->undo.undo_count;
    reject_publication = true; effects_panel_input_handle_event(NULL, state, &event); reject_publication = false;
    CHECK(state->effects_panel.chain[0].param_values[2] == 1 && state->undo.undo_count == count,
          "rejected palette changed display/history");
    undo_manager_free(&state->undo); engine_destroy(state->engine);
    free(state->effects_panel.eq_curve_tracks); free(state->effects_panel.last_open_track_fx_ids); free(state);
}

int main(void) {
    test_discrete_effect_actions();
    test_rack_spectrogram_palettes();
    test_effect_restore_history();
    test_effect_gesture_target();
    test_whole_track_history();
    test_inspector_edit_target_change();
    test_effects_history_reservation();
    test_eq_history_rejection();
    test_content_insert_transaction(false, false);
    test_content_insert_transaction(true, false);
    test_content_insert_transaction(true, true);
    test_clipboard_replacement_failure();
    test_content_selection_edit(false);
    test_content_selection_edit(true);
    test_audio_trim_action();
    test_inspector_numeric_transaction();
    test_history_gesture_exclusion();
    test_inspector_scalar_history();
    test_inspector_rename_identity(false);
    test_inspector_rename_identity(true);
    test_inspector_audio_numeric_fields();
    test_midi_trim_action(false, false);
    test_midi_trim_action(false, true);
    test_midi_trim_action(true, false);
    test_midi_trim_action(true, true);
    test_content_drop_transaction(false);
    test_content_drop_transaction(true);
    test_midi_content_history();
    test_batch_track_growth();
    test_batch_transform();
    test_transform_track_identity();
    test_capacity_transactions();
    test_automation_transactions();
    test_midi_transactions();
    test_track_settings_transaction();
    test_complete_clip_transform();
    test_overlap_transaction();
    test_delete_transactions();
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    Engine* engine = engine_create(&config);
    CHECK(engine != NULL, "create");
    FxInstId track_fx = engine_fx_track_add(engine, 0, 1);
    FxInstId master_fx = engine_fx_master_add(engine, 1);
    CHECK(track_fx && master_fx, "fixture effects");
    CHECK(engine_fx_track_add(engine, 0, 1) != 0 && engine_fx_master_add(engine, 1) != 0, "second effects");
    CHECK(engine_track_set_gain(engine, 0, 0.5f), "initial gain");
    CHECK(engine_track_set_pan(engine, 0, 0.25f), "initial pan");
    CHECK(engine_fx_track_set_param(engine, 0, track_fx, 0, -3.0f), "initial track parameter");
    CHECK(engine_fx_master_set_param(engine, master_fx, 0, -4.0f), "initial master parameter");
    EngineMixState* active = engine_render_mix_state(engine);
    EngineEqState master_eq = engine->master_eq, track_eq = engine->tracks[0].track_eq;
    reject_publication = true;
    CHECK(!engine_set_record_armed_track(engine, 0), "record arm rejection");
    CHECK(atomic_load(&engine->record_armed_track_index) == -1, "record arm rollback");
    CHECK(!engine_track_set_gain(engine, 0, 0.0f), "gain rejection");
    CHECK(!engine_track_set_pan(engine, 0, -1.0f), "pan rejection");
    CHECK(!engine_track_set_muted(engine, 0, true), "mute rejection");
    CHECK(!engine_track_set_solo(engine, 0, true), "solo rejection");
    CHECK(engine->tracks[0].gain == 0.5f && engine->tracks[0].pan == 0.25f &&
          !engine->tracks[0].muted && !engine->tracks[0].solo, "scalar rollback");
    EngineEqCurve curve = {.low_cut = {.enabled = true, .freq_hz = 400}};
    CHECK(!engine_set_master_eq_curve(engine, &curve) && !engine_set_track_eq_curve(engine, 0, &curve), "EQ rejection");
    CHECK(!memcmp(&master_eq, &engine->master_eq, sizeof(master_eq)) &&
          !memcmp(&track_eq, &engine->tracks[0].track_eq, sizeof(track_eq)), "EQ rollback");
    CHECK(!engine_fx_track_set_param(engine, 0, track_fx, 0, -12.0f), "track FX rejection");
    CHECK(!engine_fx_master_set_param(engine, master_fx, 0, -18.0f), "master FX rejection");
    CHECK(!engine_fx_track_add(engine, 0, 1) && !engine_fx_master_add(engine, 1), "FX add rejection");
    CHECK(!engine_fx_track_remove(engine, 0, track_fx) && !engine_fx_master_remove(engine, master_fx), "FX removal rejection");
    CHECK(!engine_fx_track_reorder(engine, 0, track_fx, 1) && !engine_fx_master_reorder(engine, master_fx, 1), "FX reorder rejection");
    CHECK(!engine_fx_track_set_enabled(engine, 0, track_fx, false) &&
          !engine_fx_master_set_enabled(engine, master_fx, false), "FX bypass rejection");
    FxMasterSnapshot snapshot;
    CHECK(engine_fx_track_snapshot(engine, 0, &snapshot) && snapshot.count == 2 && snapshot.items[0].id == track_fx &&
          snapshot.items[0].enabled && snapshot.items[0].params[0] == -3.0f, "track FX rollback");
    CHECK(engine_fx_master_snapshot(engine, &snapshot) && snapshot.count == 2 && snapshot.items[0].id == master_fx &&
          snapshot.items[0].enabled && snapshot.items[0].params[0] == -4.0f, "master FX rollback");
    CHECK(engine_render_mix_state(engine) == active && active->tracks[0].gain == 0.5f, "rejected revision became audible");
    reject_publication = false;
    CHECK(!engine_track_set_gain(engine, 0, NAN) && !engine_track_set_pan(engine, 0, INFINITY), "invalid scalar accepted");
    CHECK(!engine_fx_master_set_param(engine, master_fx, 0, NAN), "invalid FX value accepted");
    CHECK(!engine_fx_master_set_param(engine, master_fx, FX_MAX_PARAMS, 0), "invalid parameter index accepted");
    CHECK(engine_track_set_gain(engine, 0, 0.0f), "zero gain retry");
    CHECK(engine_render_mix_state(engine)->tracks[0].gain == 0.0f, "zero gain not adopted");
    CHECK(engine_fx_track_set_param(engine, 0, track_fx, 0, -12.0f), "parameter retry");
    CHECK(engine_fx_track_snapshot(engine, 0, &snapshot) && snapshot.items[0].params[0] == -12.0f, "retry snapshot");
    CHECK(engine_set_track_eq_curve(engine, 0, &curve), "EQ retry");
    CHECK(engine_render_mix_state(engine)->tracks[0].track_eq.active, "EQ retry not adopted");
    // Hold publication pending without a real reader to test supersession boundaries deterministically.
    engine->device_started = true;
    engine->worker_thread = (SDL_Thread*)engine;
    CHECK(engine_track_set_gain(engine, 0, 0.75f), "pending accepted edit");
    EngineSourcePlan* pending = atomic_load(&engine->pending_source_plan);
    CHECK(pending != NULL, "pending revision missing");
    reject_publication = true;
    CHECK(!engine_track_set_gain(engine, 0, 0.25f), "pending edit rejection");
    CHECK(pending == atomic_load(&engine->pending_source_plan), "rejection replaced accepted pending edit");
    CHECK(engine->tracks[0].gain == 0.75f, "pending control rollback");
    engine_source_plan_apply(engine);
    CHECK(engine_render_mix_state(engine)->tracks[0].gain == 0.75f, "accepted pending edit lost");
    engine->worker_thread = NULL;
    engine->device_started = false;
    reject_publication = false;
    engine_destroy(engine);
    puts("engine_parameter_transaction_test: success");
    return 0;
}
