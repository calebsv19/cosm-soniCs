#define _POSIX_C_SOURCE 200809L
#include "app_state.h"
#include "audio/wav_writer.h"
#include "engine/engine_internal.h"
#include "engine/sampler.h"
#include "session.h"
#include "test_assert.h"
#include "ui/render_utils.h"
#include <math.h>
#include <unistd.h>
#define CHECK(v, m) daw_test_expect("fade_processing_test", (v), (m))

// Computes expected gains independently from the shared production helper.
static float reference(int curve, float t) {
    switch (curve) {
    case ENGINE_FADE_CURVE_S_CURVE:
        return t * t * (3 - 2 * t);
    case ENGINE_FADE_CURVE_LOGARITHMIC:
        return powf(t, .35f);
    case ENGINE_FADE_CURVE_EXPONENTIAL:
        return powf(t, 2.2f);
    default:
        return t;
    }
}

// Checks complete rendered samples, including half-open endpoints and unchanged linear fades.
static void check_audio(const float* audio, int length, int fade, int curve) {
    for (int i = 0; i < length; ++i) {
        float gain = 1;
        if (i < fade)
            gain *= reference(curve, (float)i / fade);
        if (i >= length - fade)
            gain *= 1 - reference(curve, (float)(i - (length - fade)) / fade);
        CHECK(fabsf(audio[i * 2] - .25f * gain) < 2e-6f && fabsf(audio[i * 2 + 1] - .25f * gain) < 2e-6f,
              "rendered curve differs from editor-defined gain");
    }
}

// Exercises publication, sampler clones, block partitioning, export, trim, and serialized curve identity.
int main(void) {
    char path[] = "/tmp/daw-fade-audio-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0, "fixture");
    close(fd);
    char project[1024], backup[1030];
    snprintf(project, sizeof(project), "%s.json", path);
    snprintf(backup, sizeof(backup), "%s.bak", project);
    float samples[512];
    for (int i = 0; i < 512; ++i)
        samples[i] = .25f;
    CHECK(wav_write_f32(path, samples, 256, 2, 48000), "write constant audio");
    AppState state = {0};
    config_set_defaults(&state.runtime_cfg);
    state.engine = engine_create(&state.runtime_cfg);
    CHECK(state.engine != NULL, "engine");
    Engine* engine = state.engine;
    undo_manager_init(&state.undo);
    CHECK(engine_add_clip_to_track(engine, 0, path, 0, NULL), "create clip");
    CHECK(engine_clip_set_region(engine, 0, 0, 16, 128), "trim offset");
    CHECK(engine_clip_set_fades(engine, 0, 0, 32, 32), "fade lengths");
    for (EngineFadeCurve curve = 0; curve < ENGINE_FADE_CURVE_COUNT; ++curve) {
        UndoCommand command = {.type = UNDO_CMD_CLIP_TRANSFORM};
        CHECK(undo_clip_state_from_engine_clip(&engine->tracks[0].clips[0], 0,
                                               &command.data.clip_transform.before),
              "history before");
        CHECK(engine_clip_set_fade_curves(engine, 0, 0, curve, curve), "publish shape");
        CHECK(undo_clip_state_from_engine_clip(&engine->tracks[0].clips[0], 0,
                                               &command.data.clip_transform.after),
              "history after");
        CHECK(undo_manager_push(&state.undo, &command), "shape history");
        undo_clip_state_clear(&command.data.clip_transform.before);
        undo_clip_state_clear(&command.data.clip_transform.after);
        float audio[256] = {0}, scratch[256] = {0};
        engine_mix_tracks(engine, 0, 128, audio, scratch, 2);
        check_audio(audio, 128, 32, curve);
        EngineSamplerSource* clone = engine_sampler_source_clone(engine->tracks[0].clips[0].sampler);
        CHECK(clone != NULL, "clone");
        memset(audio, 0, sizeof(audio));
        for (int i = 0; i < 128; i += 8)
            engine_sampler_source_render(clone, audio + i * 2, 8, (uint64_t)i);
        check_audio(audio, 128, 32, curve);
        engine_sampler_source_set_clip(clone, engine_sampler_get_media(clone), 0, 16, 32, 32, 32);
        memset(audio, 0, sizeof(audio));
        engine_sampler_source_render(clone, audio, 32, 0);
        check_audio(audio, 32, 32, curve);
        engine_sampler_source_destroy(clone);
        EngineBounceBuffer bounce = {0};
        CHECK(engine_bounce_range_to_buffer(engine, 0, 128, NULL, NULL, &bounce), "bounce");
        check_audio(bounce.data, 128, 32, curve);
        engine_bounce_buffer_free(&bounce);
        for (int i = 0; i <= 32; ++i)
            CHECK(fabsf(ui_fade_curve_eval(curve, (float)i / 32) - reference(curve, (float)i / 32)) < 1e-6f,
                  "preview formula");
        CHECK(undo_manager_undo(&state.undo, &state), "undo shape");
        engine_mix_tracks(engine, 0, 128, audio, scratch, 2);
        check_audio(audio, 128, 32, curve ? (int)curve - 1 : 0);
        CHECK(undo_manager_redo(&state.undo, &state), "redo shape");
        engine_mix_tracks(engine, 0, 128, audio, scratch, 2);
        check_audio(audio, 128, 32, curve);
        SessionDocument document;
        session_document_init(&document);
        CHECK(session_document_capture(&state, &document), "capture curve settings");
        CHECK(document.tracks[0].clips[0].fade_in_curve == curve &&
                  document.tracks[0].clips[0].fade_out_curve == curve,
              "saved curve identity");
        CHECK(session_document_write_file(&document, project), "serialize curves");
        SessionDocument restored;
        session_document_init(&restored);
        CHECK(session_document_read_file(project, &restored), "read saved curves");
        CHECK(restored.tracks[0].clips[0].fade_in_curve == curve &&
                  restored.tracks[0].clips[0].fade_out_curve == curve,
              "roundtrip changed curves");
        session_document_free(&restored);
        session_document_free(&document);
    }
    int segment = -1;
    CHECK(engine_add_clip_segment(engine, 0, &engine->tracks[0].clips[0], 16, 64, 256, &segment),
          "existing segment copy");
    float segment_audio[128] = {0}, segment_scratch[128] = {0};
    engine_mix_tracks(engine, 256, 64, segment_audio, segment_scratch, 2);
    check_audio(segment_audio, 64, 32, ENGINE_FADE_CURVE_EXPONENTIAL);
    CHECK(engine_remove_clip(engine, 0, segment), "remove segment fixture");
    CHECK(engine_clip_set_fades(engine, 0, 0, 0, 0), "zero fades");
    float audio[256] = {0}, scratch[256] = {0};
    engine_mix_tracks(engine, 0, 128, audio, scratch, 2);
    for (int i = 0; i < 256; ++i)
        CHECK(audio[i] == .25f, "zero fades altered source");
    CHECK(engine_clip_set_region(engine, 0, 0, 16, 1) &&
              engine_clip_set_fades(engine, 0, 0, UINT64_MAX, UINT64_MAX),
          "one-frame bounds");
    CHECK(engine->tracks[0].clips[0].fade_in_frames == 1 && engine->tracks[0].clips[0].fade_out_frames == 0,
          "existing overlap clamp changed");
    engine_mix_tracks(engine, 0, 1, audio, scratch, 2);
    CHECK(audio[0] == 0, "one-frame fade endpoint");
    undo_manager_free(&state.undo);
    engine_destroy(engine);
    CHECK(unlink(path) == 0 && unlink(project) == 0 && unlink(backup) == 0, "cleanup");
    puts("fade_processing_test: success (four curves, render/clone/block/export/preview parity, capture, "
         "zero/short bounds)");
    return 0;
}
