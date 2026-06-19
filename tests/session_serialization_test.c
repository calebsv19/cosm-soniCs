#include "session.h"
#include "daw/data_paths.h"
#include "engine/engine.h"
#include "ui/library_browser.h"

#include "test_session_engine_stubs.h"

#include <SDL2/SDL.h>

#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

static const char* kTestOutputPath = "build/tests/sample_session.json";

static bool write_replaced_file(const char* source_path,
                                const char* target_path,
                                const char* needle,
                                const char* replacement) {
    FILE* source = fopen(source_path, "rb");
    if (!source) {
        return false;
    }
    if (fseek(source, 0, SEEK_END) != 0) {
        fclose(source);
        return false;
    }
    long size = ftell(source);
    if (size < 0 || fseek(source, 0, SEEK_SET) != 0) {
        fclose(source);
        return false;
    }
    char* data = (char*)malloc((size_t)size + 1u);
    if (!data) {
        fclose(source);
        return false;
    }
    size_t read_count = fread(data, 1, (size_t)size, source);
    fclose(source);
    if (read_count != (size_t)size) {
        free(data);
        return false;
    }
    data[size] = '\0';

    char* match = strstr(data, needle);
    if (!match) {
        free(data);
        return false;
    }
    size_t prefix_len = (size_t)(match - data);
    size_t needle_len = strlen(needle);
    size_t replacement_len = strlen(replacement);
    size_t suffix_len = (size_t)size - prefix_len - needle_len;
    size_t out_len = prefix_len + replacement_len + suffix_len;
    char* out = (char*)malloc(out_len + 1u);
    if (!out) {
        free(data);
        return false;
    }
    memcpy(out, data, prefix_len);
    memcpy(out + prefix_len, replacement, replacement_len);
    memcpy(out + prefix_len + replacement_len, match + needle_len, suffix_len);
    out[out_len] = '\0';

    FILE* target = fopen(target_path, "wb");
    if (!target) {
        free(out);
        free(data);
        return false;
    }
    size_t write_count = fwrite(out, 1, out_len, target);
    fclose(target);
    free(out);
    free(data);
    return write_count == out_len;
}

static int run_factory_preset_session_matrix_test(void) {
    const char* path = "build/tests/factory_presets_session.json";
    SessionDocument doc;
    session_document_init(&doc);
    doc.engine.sample_rate = 48000;
    doc.engine.block_size = 128;
    doc.track_count = 1;
    doc.tracks = (SessionTrack*)calloc(1, sizeof(SessionTrack));
    if (!doc.tracks) {
        SDL_Log("session_serialization_test: failed to allocate preset matrix track");
        session_document_free(&doc);
        return 20;
    }
    SessionTrack* track = &doc.tracks[0];
    strncpy(track->name, "Factory Presets", sizeof(track->name) - 1);
    track->gain = 1.0f;
    track->midi_instrument_enabled = true;
    track->midi_instrument_preset = ENGINE_INSTRUMENT_PRESET_SOFT_PAD;
    track->midi_instrument_params = engine_instrument_default_params(track->midi_instrument_preset);
    track->midi_instrument_params.level = 0.61f;
    track->midi_instrument_params.tone = 0.37f;
    track->clip_count = ENGINE_INSTRUMENT_PRESET_COUNT;
    track->clips = (SessionClip*)calloc((size_t)track->clip_count, sizeof(SessionClip));
    if (!track->clips) {
        SDL_Log("session_serialization_test: failed to allocate preset matrix clips");
        session_document_free(&doc);
        return 21;
    }

    for (int i = 0; i < track->clip_count; ++i) {
        EngineInstrumentPresetId preset = (EngineInstrumentPresetId)i;
        SessionClip* clip = &track->clips[i];
        clip->kind = ENGINE_CLIP_KIND_MIDI;
        snprintf(clip->name, sizeof(clip->name), "Preset %d", i);
        clip->start_frame = (uint64_t)i * 48000u;
        clip->duration_frames = 48000;
        clip->fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
        clip->fade_out_curve = ENGINE_FADE_CURVE_LINEAR;
        clip->gain = 1.0f;
        clip->instrument_preset = preset;
        clip->instrument_params = engine_instrument_default_params(preset);
        clip->instrument_inherits_track = (i == 0);
        clip->instrument_params.level = 0.50f + 0.03f * (float)i;
        clip->instrument_params.tone = 0.20f + 0.04f * (float)i;
        clip->instrument_params.attack_ms = 2.0f + (float)i;
        clip->instrument_params.release_ms = 40.0f + 3.0f * (float)i;
        clip->midi_note_count = 1;
        clip->midi_notes = (EngineMidiNote*)calloc(1, sizeof(EngineMidiNote));
        if (!clip->midi_notes) {
            SDL_Log("session_serialization_test: failed to allocate preset matrix note");
            session_document_free(&doc);
            return 22;
        }
        clip->midi_notes[0] = (EngineMidiNote){0, 12000, (uint8_t)(48 + i), 0.7f};
    }

    if (!session_document_write_file(&doc, path)) {
        SDL_Log("session_serialization_test: failed to write preset matrix");
        session_document_free(&doc);
        return 23;
    }

    SessionDocument loaded;
    session_document_init(&loaded);
    if (!session_document_read_file(path, &loaded)) {
        SDL_Log("session_serialization_test: failed to read preset matrix");
        session_document_free(&doc);
        session_document_free(&loaded);
        return 24;
    }
    if (loaded.track_count != 1 ||
        loaded.tracks[0].clip_count != ENGINE_INSTRUMENT_PRESET_COUNT) {
        SDL_Log("session_serialization_test: preset matrix count mismatch");
        session_document_free(&doc);
        session_document_free(&loaded);
        return 25;
    }
    if (!loaded.tracks[0].midi_instrument_enabled ||
        loaded.tracks[0].midi_instrument_preset != ENGINE_INSTRUMENT_PRESET_SOFT_PAD ||
        fabsf(loaded.tracks[0].midi_instrument_params.level - 0.61f) > 0.01f ||
        fabsf(loaded.tracks[0].midi_instrument_params.tone - 0.37f) > 0.01f) {
        SDL_Log("session_serialization_test: track MIDI instrument default mismatch");
        session_document_free(&doc);
        session_document_free(&loaded);
        return 26;
    }
    for (int i = 0; i < ENGINE_INSTRUMENT_PRESET_COUNT; ++i) {
        const SessionClip* clip = &loaded.tracks[0].clips[i];
        if (clip->kind != ENGINE_CLIP_KIND_MIDI ||
            clip->instrument_preset != (EngineInstrumentPresetId)i ||
            clip->instrument_inherits_track != (i == 0) ||
            fabsf(clip->instrument_params.level - (0.50f + 0.03f * (float)i)) > 0.01f ||
            fabsf(clip->instrument_params.tone - (0.20f + 0.04f * (float)i)) > 0.01f ||
            fabsf(clip->instrument_params.attack_ms - (2.0f + (float)i)) > 0.01f ||
            fabsf(clip->instrument_params.release_ms - (40.0f + 3.0f * (float)i)) > 0.01f ||
            clip->midi_note_count != 1 ||
            !clip->midi_notes ||
            clip->midi_notes[0].note != (uint8_t)(48 + i)) {
            SDL_Log("session_serialization_test: preset matrix clip %d mismatch", i);
            session_document_free(&doc);
            session_document_free(&loaded);
            return 27;
        }
    }
    session_document_free(&doc);
    session_document_free(&loaded);
    return 0;
}

static int run_unknown_preset_fallback_test(void) {
    const char* path = "build/tests/sample_session_unknown_preset.json";
    if (!write_replaced_file(kTestOutputPath, path, "\"saw_lead\"", "\"future_missing_preset\"")) {
        SDL_Log("session_serialization_test: failed to write unknown preset fixture");
        return 30;
    }

    SessionDocument loaded;
    session_document_init(&loaded);
    if (!session_document_read_file(path, &loaded)) {
        SDL_Log("session_serialization_test: failed to read unknown preset fixture");
        session_document_free(&loaded);
        return 31;
    }
    if (loaded.track_count != 1 || loaded.tracks[0].clip_count != 2) {
        SDL_Log("session_serialization_test: unknown preset fixture count mismatch");
        session_document_free(&loaded);
        return 32;
    }
    const SessionClip* clip = &loaded.tracks[0].clips[1];
    if (clip->kind != ENGINE_CLIP_KIND_MIDI ||
        clip->instrument_preset != ENGINE_INSTRUMENT_PRESET_PURE_SINE ||
        fabsf(clip->instrument_params.level - 0.82f) > 0.01f ||
        fabsf(clip->instrument_params.tone - 0.73f) > 0.01f ||
        fabsf(clip->instrument_params.attack_ms - 12.0f) > 0.01f ||
        fabsf(clip->instrument_params.release_ms - 120.0f) > 0.01f ||
        clip->midi_note_count != 2 ||
        !clip->midi_notes ||
        clip->midi_notes[0].note != 60 ||
        clip->midi_notes[1].note != 64) {
        SDL_Log("session_serialization_test: unknown preset fallback mismatch");
        session_document_free(&loaded);
        return 33;
    }
    session_document_free(&loaded);
    return 0;
}

int main(void) {
    if (mkdir("build", 0755) != 0 && errno != EEXIST) {
        SDL_Log("session_serialization_test: failed to create build directory");
        return 1;
    }
    if (mkdir("build/tests", 0755) != 0 && errno != EEXIST) {
        SDL_Log("session_serialization_test: failed to create build/tests directory");
        return 1;
    }

    SessionDocument doc;
    session_document_init(&doc);

    doc.engine.sample_rate = 48000;
    doc.engine.block_size = 128;
    doc.engine.default_fade_in_ms = 5.0f;
    doc.engine.default_fade_out_ms = 15.0f;
    doc.engine.fade_preset_count = 3;
    doc.engine.fade_preset_ms[0] = 0.0f;
    doc.engine.fade_preset_ms[1] = 12.5f;
    doc.engine.fade_preset_ms[2] = 55.0f;
    for (int i = doc.engine.fade_preset_count; i < CONFIG_FADE_PRESET_MAX; ++i) {
        doc.engine.fade_preset_ms[i] = 0.0f;
    }
    doc.engine.enable_engine_logs = true;
    doc.engine.enable_cache_logs = false;
    doc.engine.enable_timing_logs = true;

    doc.loop.enabled = false;
    doc.loop.start_frame = 0;
    doc.loop.end_frame = 0;

    doc.timeline.visible_seconds = 8.0f;
    doc.timeline.vertical_scale = 1.0f;
    doc.timeline.show_all_grid_lines = false;
    doc.timeline.snap_enabled = true;
    doc.timeline.automation_mode = true;
    doc.timeline.automation_labels_enabled = true;
    doc.timeline.tempo_overlay_enabled = true;
    doc.timeline.playhead_frame = 0;
    doc.active_track_index = 0;
    doc.selected_track_index = 0;
    doc.selected_clip_index = 1;
    doc.selection_count = 2;
    doc.selection[0].track_index = 0;
    doc.selection[0].clip_index = 0;
    doc.selection[1].track_index = 0;
    doc.selection[1].clip_index = 1;
    doc.midi_editor.panel_mode = 1;
    doc.midi_editor.instrument_active_group = ENGINE_INSTRUMENT_PARAM_GROUP_MOD;
    doc.midi_editor.quantize_division = 32;
    doc.midi_editor.default_velocity = 0.67f;
    doc.midi_editor.qwerty_octave_offset = -1;
    doc.midi_editor.viewport_track_index = 0;
    doc.midi_editor.viewport_clip_index = 1;
    doc.midi_editor.viewport_start_frame = 24000;
    doc.midi_editor.viewport_span_frames = 72000;
    doc.midi_editor.pitch_viewport_track_index = 0;
    doc.midi_editor.pitch_viewport_clip_index = 1;
    doc.midi_editor.pitch_viewport_top_note = 84;
    doc.midi_editor.pitch_viewport_row_count = 18;

    doc.layout.transport_ratio = 0.3f;
    doc.layout.library_ratio = 0.25f;
    doc.layout.mixer_ratio = 0.45f;

    strncpy(doc.library.directory, "/tmp/daw_media", sizeof(doc.library.directory) - 1);
    doc.library.directory[sizeof(doc.library.directory) - 1] = '\0';
    doc.library.selected_index = 0;
    doc.library.panel_mode = LIBRARY_PANEL_MODE_IN_PROJECT;
    strncpy(doc.data_paths.input_root, "/tmp/daw_media", sizeof(doc.data_paths.input_root) - 1);
    doc.data_paths.input_root[sizeof(doc.data_paths.input_root) - 1] = '\0';
    strncpy(doc.data_paths.output_root, "build/tests/session-output", sizeof(doc.data_paths.output_root) - 1);
    doc.data_paths.output_root[sizeof(doc.data_paths.output_root) - 1] = '\0';
    strncpy(doc.data_paths.library_copy_root, "build/tests/library-copy", sizeof(doc.data_paths.library_copy_root) - 1);
    doc.data_paths.library_copy_root[sizeof(doc.data_paths.library_copy_root) - 1] = '\0';

    doc.transport_playing = false;
    doc.transport_frame = 0;

    doc.track_count = 1;
    doc.tracks = (SessionTrack*)calloc(1, sizeof(SessionTrack));
    if (!doc.tracks) {
        SDL_Log("session_serialization_test: failed to allocate track array");
        session_document_free(&doc);
        return 1;
    }

    SessionTrack* track = &doc.tracks[0];
    strncpy(track->name, "Test Track", sizeof(track->name) - 1);
    track->name[sizeof(track->name) - 1] = '\0';
    track->gain = 0.8f;
    track->muted = false;
    track->solo = false;
    track->midi_instrument_enabled = true;
    track->midi_instrument_preset = ENGINE_INSTRUMENT_PRESET_SOFT_PAD;
    track->midi_instrument_params = engine_instrument_default_params(track->midi_instrument_preset);
    track->midi_instrument_params.level = 0.66f;
    track->midi_instrument_automation_lane_count = 1;
    track->midi_instrument_automation_lanes = (SessionAutomationLane*)calloc(1, sizeof(SessionAutomationLane));
    if (!track->midi_instrument_automation_lanes) {
        SDL_Log("session_serialization_test: failed to allocate track MIDI automation lanes");
        session_document_free(&doc);
        return 2;
    }
    track->midi_instrument_automation_lanes[0].target = ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL;
    track->midi_instrument_automation_lanes[0].point_count = 2;
    track->midi_instrument_automation_lanes[0].points =
        (SessionAutomationPoint*)calloc(2, sizeof(SessionAutomationPoint));
    if (!track->midi_instrument_automation_lanes[0].points) {
        SDL_Log("session_serialization_test: failed to allocate track MIDI automation points");
        session_document_free(&doc);
        return 2;
    }
    track->midi_instrument_automation_lanes[0].points[0].frame = 0;
    track->midi_instrument_automation_lanes[0].points[0].value = -0.4f;
    track->midi_instrument_automation_lanes[0].points[1].frame = 192000;
    track->midi_instrument_automation_lanes[0].points[1].value = 0.35f;
    track->clip_count = 2;
    track->clips = (SessionClip*)calloc(2, sizeof(SessionClip));
    if (!track->clips) {
        SDL_Log("session_serialization_test: failed to allocate clip array");
        session_document_free(&doc);
        return 2;
    }

    SessionClip* clip = &track->clips[0];
    strncpy(clip->name, "Test Clip", sizeof(clip->name) - 1);
    clip->name[sizeof(clip->name) - 1] = '\0';
    strncpy(clip->media_path, "assets/audio/test.wav", sizeof(clip->media_path) - 1);
    clip->media_path[sizeof(clip->media_path) - 1] = '\0';
    clip->start_frame = 0;
    clip->duration_frames = 48000;
    clip->offset_frames = 0;
    clip->fade_in_frames = 1200;
    clip->fade_out_frames = 2400;
    clip->automation_lane_count = 1;
    clip->automation_lanes = (SessionAutomationLane*)calloc(1, sizeof(SessionAutomationLane));
    if (!clip->automation_lanes) {
        SDL_Log("session_serialization_test: failed to allocate automation lanes");
        session_document_free(&doc);
        return 2;
    }
    clip->automation_lanes[0].target = ENGINE_AUTOMATION_TARGET_VOLUME;
    clip->automation_lanes[0].point_count = 1;
    clip->automation_lanes[0].points = (SessionAutomationPoint*)calloc(1, sizeof(SessionAutomationPoint));
    if (!clip->automation_lanes[0].points) {
        SDL_Log("session_serialization_test: failed to allocate automation points");
        session_document_free(&doc);
        return 2;
    }
    clip->automation_lanes[0].points[0].frame = 24000;
    clip->automation_lanes[0].points[0].value = 0.5f;
    clip->gain = 1.0f;
    clip->selected = false;

    SessionClip* midi_clip = &track->clips[1];
    midi_clip->kind = ENGINE_CLIP_KIND_MIDI;
    strncpy(midi_clip->name, "Test MIDI Region", sizeof(midi_clip->name) - 1);
    midi_clip->name[sizeof(midi_clip->name) - 1] = '\0';
    midi_clip->start_frame = 96000;
    midi_clip->duration_frames = 192000;
    midi_clip->offset_frames = 0;
    midi_clip->fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
    midi_clip->fade_out_curve = ENGINE_FADE_CURVE_LINEAR;
    midi_clip->gain = 0.75f;
    midi_clip->instrument_preset = ENGINE_INSTRUMENT_PRESET_SAW_LEAD;
    midi_clip->instrument_params = engine_instrument_default_params(midi_clip->instrument_preset);
    midi_clip->instrument_params.level = 0.82f;
    midi_clip->instrument_params.tone = 0.73f;
    midi_clip->instrument_params.attack_ms = 12.0f;
    midi_clip->instrument_params.release_ms = 120.0f;
    midi_clip->instrument_params.decay_ms = 180.0f;
    midi_clip->instrument_params.sustain = 0.64f;
    midi_clip->instrument_params.osc_mix = 0.55f;
    midi_clip->instrument_params.osc2_detune = 9.0f;
    midi_clip->instrument_params.sub_mix = 0.20f;
    midi_clip->instrument_params.drive = 0.33f;
    midi_clip->instrument_params.vibrato_rate = 5.5f;
    midi_clip->instrument_params.vibrato_depth = 11.0f;
    midi_clip->midi_note_count = 2;
    midi_clip->midi_notes = (EngineMidiNote*)calloc(2, sizeof(EngineMidiNote));
    if (!midi_clip->midi_notes) {
        SDL_Log("session_serialization_test: failed to allocate MIDI notes");
        session_document_free(&doc);
        return 2;
    }
    midi_clip->midi_notes[0] = (EngineMidiNote){0, 24000, 60, 0.9f};
    midi_clip->midi_notes[1] = (EngineMidiNote){48000, 12000, 64, 0.6f};
    midi_clip->automation_lane_count = 1;
    midi_clip->automation_lanes = (SessionAutomationLane*)calloc(1, sizeof(SessionAutomationLane));
    if (!midi_clip->automation_lanes) {
        SDL_Log("session_serialization_test: failed to allocate MIDI automation lanes");
        session_document_free(&doc);
        return 2;
    }
    midi_clip->automation_lanes[0].target = ENGINE_AUTOMATION_TARGET_INSTRUMENT_TONE;
    midi_clip->automation_lanes[0].point_count = 2;
    midi_clip->automation_lanes[0].points = (SessionAutomationPoint*)calloc(2, sizeof(SessionAutomationPoint));
    if (!midi_clip->automation_lanes[0].points) {
        SDL_Log("session_serialization_test: failed to allocate MIDI automation points");
        session_document_free(&doc);
        return 2;
    }
    midi_clip->automation_lanes[0].points[0].frame = 0;
    midi_clip->automation_lanes[0].points[0].value = -0.25f;
    midi_clip->automation_lanes[0].points[1].frame = 96000;
    midi_clip->automation_lanes[0].points[1].value = 0.5f;

    SDL_Log("session_serialization_test: writing %s", kTestOutputPath);
    bool ok = session_document_write_file(&doc, kTestOutputPath);
    if (!ok) {
        session_document_free(&doc);
        SDL_Log("session_serialization_test: failed to write session file");
        return 3;
    }

    SessionDocument loaded;
    session_document_init(&loaded);
    if (!session_document_read_file(kTestOutputPath, &loaded)) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: failed to read session file");
        return 4;
    }

    if (loaded.track_count != 1 || loaded.tracks[0].clip_count != 2) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: deserialised counts mismatch");
        return 5;
    }
    if (fabsf(loaded.engine.default_fade_in_ms - doc.engine.default_fade_in_ms) > 0.01f ||
        fabsf(loaded.engine.default_fade_out_ms - doc.engine.default_fade_out_ms) > 0.01f ||
        loaded.engine.fade_preset_count != doc.engine.fade_preset_count) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: engine fade config mismatch");
        return 6;
    }
    for (int i = 0; i < loaded.engine.fade_preset_count; ++i) {
        if (fabsf(loaded.engine.fade_preset_ms[i] - doc.engine.fade_preset_ms[i]) > 0.01f) {
            session_document_free(&doc);
            session_document_free(&loaded);
            SDL_Log("session_serialization_test: fade preset %d mismatch", i);
            return 7;
        }
    }
    if (loaded.engine.enable_engine_logs != doc.engine.enable_engine_logs ||
        loaded.engine.enable_cache_logs != doc.engine.enable_cache_logs ||
        loaded.engine.enable_timing_logs != doc.engine.enable_timing_logs) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: engine logging flags mismatch");
        return 8;
    }
    if (loaded.active_track_index != 0 ||
        loaded.selected_track_index != 0 ||
        loaded.selected_clip_index != 1 ||
        loaded.midi_editor.panel_mode != 1 ||
        loaded.midi_editor.instrument_active_group != ENGINE_INSTRUMENT_PARAM_GROUP_MOD ||
        loaded.midi_editor.quantize_division != 32 ||
        fabsf(loaded.midi_editor.default_velocity - 0.67f) > 0.01f ||
        loaded.midi_editor.qwerty_octave_offset != -1 ||
        loaded.midi_editor.viewport_track_index != 0 ||
        loaded.midi_editor.viewport_clip_index != 1 ||
        loaded.midi_editor.viewport_start_frame != 24000 ||
        loaded.midi_editor.viewport_span_frames != 72000 ||
        loaded.midi_editor.pitch_viewport_track_index != 0 ||
        loaded.midi_editor.pitch_viewport_clip_index != 1 ||
        loaded.midi_editor.pitch_viewport_top_note != 84 ||
        loaded.midi_editor.pitch_viewport_row_count != 18) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: MIDI editor view state mismatch");
        return 8;
    }
    if (!loaded.timeline.snap_enabled ||
        !loaded.timeline.automation_mode ||
        !loaded.timeline.automation_labels_enabled ||
        !loaded.timeline.tempo_overlay_enabled ||
        loaded.selection_count != 2 ||
        loaded.selection[0].track_index != 0 ||
        loaded.selection[0].clip_index != 0 ||
        loaded.selection[1].track_index != 0 ||
        loaded.selection[1].clip_index != 1 ||
        strcmp(loaded.library.directory, "/tmp/daw_media") != 0 ||
        loaded.library.panel_mode != LIBRARY_PANEL_MODE_IN_PROJECT ||
        strcmp(loaded.data_paths.input_root, "/tmp/daw_media") != 0 ||
        strcmp(loaded.data_paths.output_root, "build/tests/session-output") != 0 ||
        strcmp(loaded.data_paths.library_copy_root, "build/tests/library-copy") != 0) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: session UI/path state mismatch");
        return 8;
    }
    SessionTrack* lt = &loaded.tracks[0];
    SessionClip* lc = &lt->clips[0];
    if (!lt->midi_instrument_enabled ||
        lt->midi_instrument_preset != ENGINE_INSTRUMENT_PRESET_SOFT_PAD ||
        fabsf(lt->midi_instrument_params.level - 0.66f) > 0.01f ||
        lt->midi_instrument_automation_lane_count != 1 ||
        !lt->midi_instrument_automation_lanes ||
        lt->midi_instrument_automation_lanes[0].target != ENGINE_AUTOMATION_TARGET_INSTRUMENT_LEVEL ||
        lt->midi_instrument_automation_lanes[0].point_count != 2 ||
        !lt->midi_instrument_automation_lanes[0].points ||
        lt->midi_instrument_automation_lanes[0].points[1].frame != 192000 ||
        fabsf(lt->midi_instrument_automation_lanes[0].points[1].value - 0.35f) > 0.01f) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: track MIDI automation mismatch");
        return 9;
    }
    if (lc->kind != ENGINE_CLIP_KIND_AUDIO ||
        strcmp(lt->name, "Test Track") != 0 || strcmp(lc->name, "Test Clip") != 0 ||
        strcmp(lc->media_path, "assets/audio/test.wav") != 0 || lc->duration_frames != 48000 ||
        lc->fade_in_frames != 1200 || lc->fade_out_frames != 2400) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: deserialised content mismatch");
        return 9;
    }
    if (lc->automation_lane_count != 1 ||
        lc->automation_lanes[0].target != ENGINE_AUTOMATION_TARGET_VOLUME ||
        lc->automation_lanes[0].point_count != 1 ||
        lc->automation_lanes[0].points[0].frame != 24000) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: automation lanes mismatch");
        return 9;
    }
    SessionClip* lm = &lt->clips[1];
    if (lm->kind != ENGINE_CLIP_KIND_MIDI ||
        strcmp(lm->name, "Test MIDI Region") != 0 ||
        lm->media_path[0] != '\0' ||
        lm->media_id[0] != '\0' ||
        lm->start_frame != 96000 ||
        lm->duration_frames != 192000 ||
        lm->instrument_preset != ENGINE_INSTRUMENT_PRESET_SAW_LEAD ||
        fabsf(lm->instrument_params.level - 0.82f) > 0.01f ||
        fabsf(lm->instrument_params.tone - 0.73f) > 0.01f ||
        fabsf(lm->instrument_params.attack_ms - 12.0f) > 0.01f ||
        fabsf(lm->instrument_params.release_ms - 120.0f) > 0.01f ||
        fabsf(lm->instrument_params.decay_ms - 180.0f) > 0.01f ||
        fabsf(lm->instrument_params.sustain - 0.64f) > 0.01f ||
        fabsf(lm->instrument_params.osc_mix - 0.55f) > 0.01f ||
        fabsf(lm->instrument_params.osc2_detune - 9.0f) > 0.01f ||
        fabsf(lm->instrument_params.sub_mix - 0.20f) > 0.01f ||
        fabsf(lm->instrument_params.drive - 0.33f) > 0.01f ||
        fabsf(lm->instrument_params.vibrato_rate - 5.5f) > 0.01f ||
        fabsf(lm->instrument_params.vibrato_depth - 11.0f) > 0.01f ||
        lm->midi_note_count != 2 ||
        !lm->midi_notes ||
        lm->midi_notes[0].note != 60 ||
        lm->midi_notes[1].note != 64 ||
        fabsf(lm->midi_notes[0].velocity - 0.9f) > 0.01f ||
        lm->automation_lane_count != 1 ||
        !lm->automation_lanes ||
        lm->automation_lanes[0].target != ENGINE_AUTOMATION_TARGET_INSTRUMENT_TONE ||
        lm->automation_lanes[0].point_count != 2 ||
        !lm->automation_lanes[0].points ||
        lm->automation_lanes[0].points[1].frame != 96000 ||
        fabsf(lm->automation_lanes[0].points[1].value - 0.5f) > 0.01f) {
        session_document_free(&doc);
        session_document_free(&loaded);
        SDL_Log("session_serialization_test: MIDI clip mismatch");
        return 9;
    }

    session_document_free(&doc);
    session_document_free(&loaded);

    FILE* file = fopen(kTestOutputPath, "rb");
    if (!file) {
        SDL_Log("session_serialization_test: output file missing");
        return 10;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        SDL_Log("session_serialization_test: failed to seek output file");
        fclose(file);
        return 11;
    }
    long size = ftell(file);
    fclose(file);
    if (size <= 0) {
        SDL_Log("session_serialization_test: output file empty");
        return 12;
    }

    int factory_result = run_factory_preset_session_matrix_test();
    if (factory_result != 0) {
        return factory_result;
    }
    int fallback_result = run_unknown_preset_fallback_test();
    if (fallback_result != 0) {
        return fallback_result;
    }

    SDL_Log("session_serialization_test: success (%ld bytes)", size);
    return 0;
}
