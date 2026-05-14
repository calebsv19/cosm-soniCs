#include "app/bounce_region.h"
#include "app_state.h"
#include "audio/media_registry.h"
#include "audio/wav_writer.h"
#include "config.h"
#include "engine/engine.h"
#include "engine/engine_internal.h"
#include "engine/midi.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void fail(const char* message) {
    fprintf(stderr, "midi_instrument_render_test: %s\n", message);
    exit(1);
}

static void expect(int condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

static void ensure_dir(const char* path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fail("failed to create test directory");
    }
}

static float peak_window(const EngineBounceBuffer* bounce, uint64_t start_frame, uint64_t end_frame) {
    if (!bounce || !bounce->data || bounce->channels <= 0 || end_frame <= start_frame) {
        return 0.0f;
    }
    if (end_frame > bounce->frame_count) {
        end_frame = bounce->frame_count;
    }
    float peak = 0.0f;
    for (uint64_t frame = start_frame; frame < end_frame; ++frame) {
        for (int ch = 0; ch < bounce->channels; ++ch) {
            float sample = bounce->data[frame * (uint64_t)bounce->channels + (uint64_t)ch];
            float abs_sample = fabsf(sample);
            if (abs_sample > peak) {
                peak = abs_sample;
            }
        }
    }
    return peak;
}

static void test_clip_midi_instrument_render(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = 48000;
    cfg.block_size = 128;

    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed");

    const uint64_t clip_start = 1000;
    const uint64_t note_start = 200;
    const uint64_t note_duration = 800;
    const uint64_t clip_duration = 2000;

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(engine, 0, clip_start, clip_duration, &clip_index),
           "failed to add MIDI clip");
    expect(clip_index >= 0, "invalid MIDI clip index");
    expect(engine_clip_midi_add_note(engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){note_start, note_duration, 69, 1.0f},
                                     NULL),
           "failed to add MIDI note");

    EngineBounceBuffer bounce = {0};
    expect(engine_bounce_range_to_buffer(engine, 0, 3400, NULL, NULL, &bounce),
           "failed to bounce MIDI instrument range");
    expect(bounce.data != NULL, "bounce data missing");
    expect(bounce.channels == 2, "expected stereo bounce");

    float pre_peak = peak_window(&bounce, 0, clip_start + note_start - 50);
    float during_peak = peak_window(&bounce, clip_start + note_start + 120, clip_start + note_start + note_duration - 120);
    float post_peak = peak_window(&bounce, clip_start + note_start + note_duration + 50, bounce.frame_count);

    expect(pre_peak < 0.000001f, "pre-note range should be silent");
    expect(during_peak > 0.001f, "note range should contain rendered audio");
    expect(post_peak < 0.000001f, "post-note range should be silent");

    engine_bounce_buffer_free(&bounce);
    engine_destroy(engine);
}

static void test_live_midi_audition_renders_without_clip_notes(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = 48000;
    cfg.block_size = 128;

    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed for audition test");

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(engine, 0, 0, (uint64_t)cfg.sample_rate, &clip_index),
           "failed to add empty MIDI clip for audition test");
    expect(engine_midi_audition_note_on(engine,
                                        0,
                                        ENGINE_INSTRUMENT_PRESET_PURE_SINE,
                                        engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE),
                                        60,
                                        1.0f),
           "audition note-on failed");

    EngineBounceBuffer bounce = {0};
    expect(engine_bounce_range_to_buffer(engine, 0, 2048, NULL, NULL, &bounce),
           "failed to bounce MIDI audition range");
    expect(peak_window(&bounce, 128, 1800) > 0.001f, "audition range should contain rendered audio");
    engine_bounce_buffer_free(&bounce);

    expect(engine_midi_audition_note_off(engine, 60), "audition note-off failed");
    expect(engine_bounce_range_to_buffer(engine, 0, 2048, NULL, NULL, &bounce),
           "failed to bounce post-audition range");
    expect(peak_window(&bounce, 0, 2048) < 0.000001f, "post-audition range should be silent");
    engine_bounce_buffer_free(&bounce);

    const EngineTrack* tracks = engine_get_tracks(engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_note_count(clip) == 0, "audition should not create clip notes");

    engine_destroy(engine);
}

static void test_midi_instrument_presets_change_render_shape(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = 48000;
    cfg.block_size = 128;

    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed for preset render test");

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(engine, 0, 0, 4096, &clip_index),
           "failed to add MIDI clip for preset render test");
    expect(engine_clip_midi_add_note(engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){0, 4096, 69, 1.0f},
                                     NULL),
           "failed to add preset render MIDI note");

    EngineBounceBuffer sine = {0};
    expect(engine_clip_midi_set_instrument_preset(engine, 0, clip_index, ENGINE_INSTRUMENT_PRESET_PURE_SINE),
           "failed to set sine preset");
    expect(engine_bounce_range_to_buffer(engine, 0, 2048, NULL, NULL, &sine),
           "failed to bounce sine preset");

    EngineBounceBuffer saw = {0};
    expect(engine_clip_midi_set_instrument_preset(engine, 0, clip_index, ENGINE_INSTRUMENT_PRESET_SAW_LEAD),
           "failed to set saw preset");
    expect(engine_bounce_range_to_buffer(engine, 0, 2048, NULL, NULL, &saw),
           "failed to bounce saw preset");

    expect(sine.data != NULL && saw.data != NULL, "preset bounce data missing");
    float diff_sum = 0.0f;
    uint64_t compare_frames = sine.frame_count < saw.frame_count ? sine.frame_count : saw.frame_count;
    if (compare_frames > 2048) {
        compare_frames = 2048;
    }
    for (uint64_t i = 128; i < compare_frames; ++i) {
        diff_sum += fabsf(sine.data[i * (uint64_t)sine.channels] - saw.data[i * (uint64_t)saw.channels]);
    }
    expect(diff_sum > 0.01f, "different presets should produce different rendered samples");

    engine_bounce_buffer_free(&sine);
    engine_bounce_buffer_free(&saw);
    engine_destroy(engine);
}

static void test_midi_instrument_params_clamp_and_affect_render(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = 48000;
    cfg.block_size = 128;

    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed for param render test");

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(engine, 0, 0, 4096, &clip_index),
           "failed to add MIDI clip for param render test");
    expect(engine_clip_midi_add_note(engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){0, 4096, 69, 1.0f},
                                     NULL),
           "failed to add param render MIDI note");

    EngineInstrumentParams params = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    params.level = 99.0f;
    params.tone = -3.0f;
    params.attack_ms = 9999.0f;
    params.release_ms = -9.0f;
    expect(engine_clip_midi_set_instrument_params(engine, 0, clip_index, params),
           "failed to set clamped instrument params");
    const EngineTrack* tracks = engine_get_tracks(engine);
    EngineInstrumentParams clamped = engine_clip_midi_instrument_params(&tracks[0].clips[clip_index]);
    expect(fabsf(clamped.level - 1.5f) < 0.001f, "level param should clamp high");
    expect(fabsf(clamped.tone - 0.0f) < 0.001f, "tone param should clamp low");
    expect(fabsf(clamped.attack_ms - 250.0f) < 0.001f, "attack param should clamp high");
    expect(fabsf(clamped.release_ms - 0.0f) < 0.001f, "release param should clamp low");

    EngineBounceBuffer quiet = {0};
    EngineInstrumentParams quiet_params = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
    quiet_params.level = 0.2f;
    expect(engine_clip_midi_set_instrument_params(engine, 0, clip_index, quiet_params),
           "failed to set quiet params");
    expect(engine_bounce_range_to_buffer(engine, 0, 2048, NULL, NULL, &quiet),
           "failed to bounce quiet params");

    EngineBounceBuffer loud = {0};
    EngineInstrumentParams loud_params = quiet_params;
    loud_params.level = 1.2f;
    expect(engine_clip_midi_set_instrument_params(engine, 0, clip_index, loud_params),
           "failed to set loud params");
    expect(engine_bounce_range_to_buffer(engine, 0, 2048, NULL, NULL, &loud),
           "failed to bounce loud params");

    float quiet_peak = peak_window(&quiet, 128, 1800);
    float loud_peak = peak_window(&loud, 128, 1800);
    expect(loud_peak > quiet_peak * 2.0f, "level param should affect render amplitude");

    engine_bounce_buffer_free(&quiet);
    engine_bounce_buffer_free(&loud);
    engine_destroy(engine);
}

static void test_stopped_midi_audition_uses_idle_clock_without_moving_transport(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = 48000;
    cfg.block_size = 128;

    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed for stopped audition test");

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(engine, 0, 0, (uint64_t)cfg.sample_rate, &clip_index),
           "failed to add MIDI clip for stopped audition test");
    expect(engine_clip_midi_add_note(engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){0, (uint64_t)cfg.sample_rate, 72, 1.0f},
                                     NULL),
           "failed to seed saved MIDI note for stopped audition test");

    int frames = 512;
    int channels = 2;
    float* out = (float*)calloc((size_t)frames * (size_t)channels, sizeof(float));
    float* track = (float*)calloc((size_t)frames * (size_t)channels, sizeof(float));
    expect(out != NULL && track != NULL, "stopped audition buffers missing");
    engine_mix_midi_audition_only(engine, 0, frames, out, track, channels);
    EngineBounceBuffer view = {
        .data = out,
        .frame_count = (uint64_t)frames,
        .channels = channels
    };
    expect(peak_window(&view, 0, (uint64_t)frames) < 0.000001f,
           "stopped audition-only mix should ignore saved region notes");

    expect(engine_midi_audition_note_on(engine,
                                        0,
                                        ENGINE_INSTRUMENT_PRESET_PURE_SINE,
                                        engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE),
                                        60,
                                        1.0f),
           "stopped audition first note-on failed");
    expect(engine->midi_audition_notes.note_count == 1, "first stopped audition note missing");
    expect(engine->midi_audition_notes.notes[0].start_frame == 0,
           "first stopped audition note should start at the transport frame");

    engine->midi_audition_idle_frame = 1024;
    expect(engine_midi_audition_note_on(engine,
                                        0,
                                        ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE,
                                        engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_SOFT_SQUARE),
                                        64,
                                        1.0f),
           "stopped audition second note-on failed");
    expect(engine->midi_audition_notes.note_count == 2, "second stopped audition note missing");
    const EngineMidiNote* notes = engine->midi_audition_notes.notes;
    uint64_t second_start = notes[0].note == 64 ? notes[0].start_frame : notes[1].start_frame;
    expect(second_start == 1024,
           "new stopped audition notes should start at the idle audition frame");

    memset(out, 0, (size_t)frames * (size_t)channels * sizeof(float));
    memset(track, 0, (size_t)frames * (size_t)channels * sizeof(float));
    engine_mix_midi_audition_only(engine,
                                  engine->transport_frame + engine->midi_audition_idle_frame,
                                  frames,
                                  out,
                                  track,
                                  channels);

    expect(peak_window(&view, 0, (uint64_t)frames) > 0.001f,
           "stopped audition mix should contain rendered audio");
    expect(engine->transport_frame == 0, "stopped audition should not move transport frame");

    free(track);
    free(out);
    engine_destroy(engine);
}

static void test_bounce_region_inserts_library_audio_track(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    cfg.sample_rate = 48000;
    cfg.block_size = 128;

    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed for bounce region test");

    int midi_clip_index = -1;
    expect(engine_add_midi_clip_to_track(engine, 0, 0, (uint64_t)cfg.sample_rate, &midi_clip_index),
           "failed to add MIDI clip for bounce region test");
    expect(engine_clip_midi_add_note(engine,
                                     0,
                                     midi_clip_index,
                                     (EngineMidiNote){0, (uint64_t)cfg.sample_rate / 2u, 60, 1.0f},
                                     NULL),
           "failed to seed MIDI note for bounce region test");

    EngineBounceBuffer bounce = {0};
    expect(engine_bounce_range_to_buffer(engine, 0, (uint64_t)cfg.sample_rate, NULL, NULL, &bounce),
           "failed to bounce MIDI source for region insert test");
    expect(peak_window(&bounce, 128, (uint64_t)cfg.sample_rate / 2u) > 0.001f,
           "bounce buffer should contain MIDI-rendered audio");

    const char* dir = "tmp/daw_bounce_region_test";
    ensure_dir("tmp");
    ensure_dir(dir);
    (void)unlink("tmp/daw_bounce_region_test/bounce.wav");
    (void)unlink("tmp/daw_bounce_region_test/bounce1.wav");

    AppState state;
    memset(&state, 0, sizeof(state));
    state.engine = engine;
    state.selected_track_index = -1;
    state.selected_clip_index = -1;
    snprintf(state.library.directory, sizeof(state.library.directory), "%s", dir);
    media_registry_init(&state.media_registry, NULL);

    char path[512];
    expect(daw_bounce_next_path_for_state(&state, path, sizeof(path)),
           "failed to allocate bounce path under library directory");
    expect(strncmp(path, dir, strlen(dir)) == 0, "bounce path should use selected library root");
    expect(strstr(path, "bounce.wav") != NULL, "first bounce path should use bounce.wav");
    expect(wav_write_pcm16_dithered(path,
                                    bounce.data,
                                    bounce.frame_count,
                                    bounce.channels,
                                    bounce.sample_rate,
                                    0),
           "failed to write test bounced WAV");

    int previous_track_count = engine_get_track_count(engine);
    int bounce_track = -1;
    int bounce_clip = -1;
    expect(daw_bounce_insert_audio_track(&state, path, 256, &bounce_track, &bounce_clip),
           "failed to insert bounced WAV on a new track");
    expect(engine_get_track_count(engine) == previous_track_count + 1,
           "bounce insert should add exactly one track");
    expect(bounce_track == previous_track_count, "bounce track should be appended");
    expect(bounce_clip >= 0, "bounce clip index should be valid");
    expect(state.selected_track_index == bounce_track && state.selected_clip_index == bounce_clip,
           "inserted bounce clip should become selected");
    expect(state.selection_count == 1, "inserted bounce clip should be the only selection");

    const EngineTrack* tracks = engine_get_tracks(engine);
    expect(tracks[bounce_track].clip_count == 1, "bounce track should contain one clip");
    const EngineClip* clip = &tracks[bounce_track].clips[bounce_clip];
    expect(clip->kind == ENGINE_CLIP_KIND_AUDIO, "inserted bounce should be an audio clip");
    expect(clip->timeline_start_frames == 256, "inserted bounce should align to bounce start");
    expect(state.media_registry.count == 1, "inserted bounce should be registered as library media");

    engine_bounce_buffer_free(&bounce);
    media_registry_shutdown(&state.media_registry);
    engine_destroy(engine);
    (void)unlink(path);
}

int main(void) {
    test_clip_midi_instrument_render();
    test_live_midi_audition_renders_without_clip_notes();
    test_midi_instrument_presets_change_render_shape();
    test_midi_instrument_params_clamp_and_affect_render();
    test_stopped_midi_audition_uses_idle_clock_without_moving_transport();
    test_bounce_region_inserts_library_audio_track();
    fprintf(stderr, "midi_instrument_render_test: success\n");
    return 0;
}
