#include "config.h"
#include "engine/engine.h"
#include "engine/midi.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void fail(const char* message) {
    fprintf(stderr, "midi_model_test: %s\n", message);
    exit(1);
}

static void expect(int condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

static void write_u16_le(FILE* fp, uint16_t value) {
    unsigned char bytes[2];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    fwrite(bytes, 1, sizeof(bytes), fp);
}

static void write_u32_le(FILE* fp, uint32_t value) {
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24) & 0xFFu);
    fwrite(bytes, 1, sizeof(bytes), fp);
}

static void write_test_wav_or_fail(const char* path, int sample_rate, uint32_t frames) {
    const uint16_t channels = 1;
    const uint16_t bits_per_sample = 16;
    const uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    const uint32_t byte_rate = (uint32_t)sample_rate * (uint32_t)block_align;
    const uint32_t data_size = frames * (uint32_t)block_align;
    const uint32_t riff_size = 36u + data_size;
    FILE* fp = NULL;

    if (mkdir("tmp", 0755) != 0 && errno != EEXIST) {
        fail("failed to create tmp directory");
    }

    fp = fopen(path, "wb");
    if (!fp) {
        fail("failed to create wav fixture");
    }

    fwrite("RIFF", 1, 4, fp);
    write_u32_le(fp, riff_size);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    write_u32_le(fp, 16u);
    write_u16_le(fp, 1u);
    write_u16_le(fp, channels);
    write_u32_le(fp, (uint32_t)sample_rate);
    write_u32_le(fp, byte_rate);
    write_u16_le(fp, block_align);
    write_u16_le(fp, bits_per_sample);
    fwrite("data", 1, 4, fp);
    write_u32_le(fp, data_size);

    for (uint32_t i = 0; i < frames; ++i) {
        write_u16_le(fp, 0u);
    }

    fclose(fp);
}

static void test_note_list_orders_and_validates(void) {
    EngineMidiNoteList list;
    engine_midi_note_list_init(&list);

    int index = -1;
    expect(engine_midi_note_list_insert(&list, (EngineMidiNote){480, 240, 64, 0.75f}, &index),
           "failed to insert first note");
    expect(index == 0, "first note index mismatch");
    expect(engine_midi_note_list_insert(&list, (EngineMidiNote){120, 120, 60, 1.0f}, &index),
           "failed to insert earlier note");
    expect(index == 0, "earlier note should sort first");
    expect(engine_midi_note_list_insert(&list, (EngineMidiNote){120, 60, 55, 0.5f}, &index),
           "failed to insert same-start lower note");
    expect(index == 0, "same-start lower note should sort first by pitch");
    expect(list.note_count == 3, "note count after inserts mismatch");
    expect(engine_midi_note_list_validate(&list), "valid note list rejected");
    expect(list.notes[0].note == 55, "first sorted note pitch mismatch");
    expect(list.notes[1].note == 60, "second sorted note pitch mismatch");
    expect(list.notes[2].note == 64, "third sorted note pitch mismatch");

    expect(!engine_midi_note_list_insert(&list, (EngineMidiNote){0, 0, 60, 1.0f}, NULL),
           "zero-duration note should fail");
    expect(!engine_midi_note_list_insert(&list, (EngineMidiNote){0, 10, 60, 1.25f}, NULL),
           "out-of-range velocity should fail");

    expect(engine_midi_note_list_update(&list, 2, (EngineMidiNote){10, 40, 72, 0.25f}, &index),
           "failed to update note");
    expect(index == 0, "updated early note should sort first");
    expect(list.notes[0].note == 72, "updated note pitch mismatch");
    expect(engine_midi_note_list_remove(&list, 0), "failed to remove updated note");
    expect(list.note_count == 2, "note count after remove mismatch");
    expect(engine_midi_note_list_validate(&list), "valid note list rejected after remove");

    engine_midi_note_list_free(&list);
}

static void test_engine_midi_clip_model(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);

    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed");

    int midi_index = -1;
    uint64_t midi_duration = (uint64_t)cfg.sample_rate * 4u;
    expect(engine_add_midi_clip_to_track(engine, 0, (uint64_t)cfg.sample_rate, midi_duration, &midi_index),
           "failed to add midi clip");
    expect(midi_index >= 0, "invalid midi clip index");

    const EngineTrack* tracks = engine_get_tracks(engine);
    expect(tracks != NULL, "engine_get_tracks failed");
    const EngineClip* clip = &tracks[0].clips[midi_index];
    expect(engine_clip_get_kind(clip) == ENGINE_CLIP_KIND_MIDI, "clip kind should be MIDI");
    expect(clip->sampler == NULL, "model-only MIDI clip should not own sampler");
    expect(clip->media == NULL, "model-only MIDI clip should not own media");
    expect(engine_clip_get_total_frames(engine, 0, midi_index) == midi_duration, "MIDI total frames mismatch");

    int note_index = -1;
    expect(engine_clip_midi_add_note(engine, 0, midi_index, (EngineMidiNote){960, 240, 67, 0.8f}, &note_index),
           "failed to add later MIDI note");
    expect(engine_clip_midi_add_note(engine, 0, midi_index, (EngineMidiNote){120, 120, 60, 1.0f}, &note_index),
           "failed to add earlier MIDI note");
    tracks = engine_get_tracks(engine);
    clip = &tracks[0].clips[midi_index];
    expect(engine_clip_midi_note_count(clip) == 2, "MIDI note count mismatch");
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(notes != NULL, "MIDI notes pointer missing");
    expect(notes[0].note == 60 && notes[1].note == 67, "MIDI notes not sorted");
    expect(!engine_clip_midi_add_note(engine,
                                      0,
                                      midi_index,
                                      (EngineMidiNote){midi_duration - 10u, 20u, 64, 1.0f},
                                      NULL),
           "note beyond clip duration should fail");
    expect(!engine_clip_midi_add_note(engine,
                                      0,
                                      midi_index,
                                      (EngineMidiNote){UINT64_MAX - 4u, 8u, 64, 1.0f},
                                      NULL),
           "overflowing note end should fail");
    expect(engine_clip_midi_update_note(engine, 0, midi_index, 1, (EngineMidiNote){60, 90, 72, 0.4f}, &note_index),
           "failed to update MIDI note");
    tracks = engine_get_tracks(engine);
    clip = &tracks[0].clips[midi_index];
    notes = engine_clip_midi_notes(clip);
    expect(note_index == 0, "updated MIDI note should sort first");
    expect(notes[0].note == 72, "updated MIDI note pitch mismatch");
    expect(engine_clip_midi_remove_note(engine, 0, midi_index, 0), "failed to remove MIDI note");
    tracks = engine_get_tracks(engine);
    clip = &tracks[0].clips[midi_index];
    expect(engine_clip_midi_note_count(clip) == 1, "MIDI remove note count mismatch");
    EngineMidiNote replacement_notes[] = {
        {240, 120, 62, 0.7f},
        {0, 120, 60, 1.0f}
    };
    expect(engine_clip_midi_set_notes(engine, 0, midi_index, replacement_notes, 2),
           "failed to replace MIDI notes");
    tracks = engine_get_tracks(engine);
    clip = &tracks[0].clips[midi_index];
    notes = engine_clip_midi_notes(clip);
    expect(engine_clip_midi_note_count(clip) == 2, "MIDI set note count mismatch");
    expect(notes && notes[0].note == 60 && notes[1].note == 62, "MIDI set should sort replacement notes");
    expect(!engine_clip_midi_set_notes(engine,
                                       0,
                                       midi_index,
                                       (EngineMidiNote[]){{midi_duration - 5u, 10u, 65, 1.0f}},
                                       1),
           "set notes beyond clip duration should fail");
    expect(!engine_clip_set_region(engine, 0, midi_index, 0, 100),
           "shrinking MIDI clip past existing notes should fail");
    expect(engine_clip_set_region(engine, 0, midi_index, 0, midi_duration / 2u),
           "valid MIDI clip region resize should succeed");

    engine_destroy(engine);
}

static void test_audio_clip_kind_defaults(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    const char* path = "tmp/midi_model_audio_default.wav";
    write_test_wav_or_fail(path, cfg.sample_rate, (uint32_t)cfg.sample_rate);

    Engine* engine = engine_create(&cfg);
    expect(engine != NULL, "engine_create failed for audio default test");

    int audio_index = -1;
    expect(engine_add_clip_to_track(engine, 0, path, 0, &audio_index), "failed to add audio clip");
    const EngineTrack* tracks = engine_get_tracks(engine);
    expect(tracks != NULL, "missing tracks after audio add");
    const EngineClip* clip = &tracks[0].clips[audio_index];
    expect(engine_clip_get_kind(clip) == ENGINE_CLIP_KIND_AUDIO, "audio clip kind should default to audio");
    expect(clip->sampler != NULL, "audio clip should keep sampler");
    expect(engine_clip_midi_note_count(clip) == 0, "audio clip should not expose MIDI notes");
    expect(!engine_clip_midi_add_note(engine, 0, audio_index, (EngineMidiNote){0, 120, 60, 1.0f}, NULL),
           "adding MIDI note to audio clip should fail");

    engine_destroy(engine);
    (void)unlink(path);
}

int main(void) {
    test_note_list_orders_and_validates();
    test_engine_midi_clip_model();
    test_audio_clip_kind_defaults();
    fprintf(stderr, "midi_model_test: success\n");
    return 0;
}
