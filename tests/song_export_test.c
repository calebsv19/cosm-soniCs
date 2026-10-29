#define _XOPEN_SOURCE 700
#include "test_temp_path.h"
#include "app_state.h"
#include "engine/engine_internal.h"
#include "engine/export_stream.h"
#include "ui/layout.h"
#include "ui/song_export_panel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
// Obtains a separately owned exact production guard for each admission.
static DawMixSnapshot* guard(AppState* s) {
    DawMixSnapshot* g = NULL;
    assert(daw_action_get_mix_snapshot(s, &g) == DAW_ACTION_UNCHANGED);
    return g;
}
// Waits for a specific job while checking monotonic provisional progress and independent control
// responsiveness.
static DawSongExportInfo wait_job(AppState* s, const char* id) {
    DawSongExportInfo i;
    uint64_t done = 0;
    for (int n = 0; n < 30000; ++n) {
        assert(daw_song_export_get(s, id, &i));
        assert(i.done >= done);
        done = i.done;
        if (!strcmp(i.state, "completed") || !strcmp(i.state, "failed") ||
            !strcmp(i.state, "cancelled") || !strncmp(i.state, "published_", 10))
            return i;
        SDL_Delay(1);
    }
    assert(!"export timeout");
    return i;
}
// Uses shared native button geometry and matching pointer edges rather than invented test hit
// boxes.
static void click(AppState* s, int button, bool matching) {
    DawSongExportLayout l;
    daw_song_export_panel_layout(s, &l);
    assert(l.editable);
    SDL_Rect r = l.buttons[button];
    SDL_Event e = {.type = SDL_MOUSEBUTTONDOWN};
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = r.x + r.w / 2;
    e.button.y = r.y + r.h / 2;
    assert(daw_song_export_panel_event(s, &e));
    e.type = SDL_MOUSEBUTTONUP;
    if (!matching)
        e.button.x = -100;
    assert(daw_song_export_panel_event(s, &e));
}
// Exercises immutable production audio, encodings/tails, cancellation, failure and native
// lifecycle.
int main(void) {
    AppState* s = calloc(1, sizeof(*s));
    EngineRuntimeConfig c;
    config_set_defaults(&c);
    c.block_size = 128;
    c.default_fade_in_ms = c.default_fade_out_ms = 0;
    s->runtime_cfg = c;
    s->tempo = tempo_state_default(c.sample_rate);
    tempo_map_init(&s->tempo_map, c.sample_rate);
    time_signature_map_init(&s->time_signature_map);
    input_manager_init(&s->input_manager);
    s->engine = engine_create(&c);
    assert(s->engine);
    undo_manager_init(&s->undo);
    daw_data_paths_set_defaults(&s->data_paths);
    DAW_TEST_TEMP_PATH(root, "sonics-export-fixture-XXXXXX");
    assert(mkdtemp(root));
    strcpy(s->data_paths.output_root, root);
    char input[512];
    snprintf(input, 512, "%s/input.wav", root);
    float pcm[4096];
    for (int i = 0; i < 2048; ++i) {
        pcm[2 * i] = .25f;
        pcm[2 * i + 1] = -.125f;
    }
    EngineBounceBuffer b = {pcm, 2048, 2, c.sample_rate};
    assert(engine_bounce_write_wav(&b, input, ENGINE_BOUNCE_WAV_FLOAT32));
    assert(engine_add_clip_to_track(s->engine, 0, input, 1024, NULL));
    DawMixSnapshot* g = guard(s);
    int history = s->undo.undo_count;
    uint64_t head = engine_get_presentation_frame(s->engine);
    DawSongExportInfo first;
    assert(daw_song_export_start(s, g, ENGINE_BOUNCE_WAV_FLOAT32, 128, false, &first) ==
           DAW_ACTION_APPLIED);
    // The captured graph keeps old levels after an immediate native mixer edit.
    assert(engine_track_set_gain(s->engine, 0, .5f));
    DawSongExportInfo done = wait_job(s, first.id);
    assert(!strcmp(done.state, "completed") && done.verified && done.durable && done.end == 3072 &&
           done.tail == 128);
    AudioMediaClip out = {0};
    assert(audio_media_clip_load_wav(done.path, c.sample_rate, &out));
    assert(out.frame_count == 3200 && out.samples[1024 * 2] == .25f &&
           out.samples[1024 * 2 + 1] == -.125f);
    audio_media_clip_free(&out);
    assert(s->undo.undo_count == history && engine_get_presentation_frame(s->engine) == head);
    assert(daw_song_export_start(s, g, ENGINE_BOUNCE_WAV_PCM16, 0, false, &first) ==
           DAW_ACTION_STALE_TARGET);
    daw_mix_snapshot_free(g);
    g = guard(s);
    assert(daw_song_export_start(s, g, ENGINE_BOUNCE_WAV_PCM16, 0, false, &first) ==
           DAW_ACTION_APPLIED);
    done = wait_job(s, first.id);
    assert(done.verified && done.bytes == 44 + 3072 * 4);
    assert(audio_media_clip_load_wav(done.path, c.sample_rate, &out));
    assert(fabs(out.samples[1024 * 2] - .125f) < .0001);
    audio_media_clip_free(&out);
    // Long tail ensures real cancellation reaches queued/rendering work before publication.
    assert(daw_song_export_start(s, g, ENGINE_BOUNCE_WAV_FLOAT32, (uint64_t)c.sample_rate * 60,
                                 false, &first) == DAW_ACTION_APPLIED);
    const char* cancel = daw_song_export_cancel(s, first.id);
    assert(!strcmp(cancel, "cancel_requested") || !strcmp(cancel, "cancelling"));
    done = wait_job(s, first.id);
    assert(!strcmp(done.state, "cancelled") && !done.published && access(done.path, F_OK) != 0);
    assert(!strcmp(daw_song_export_cancel(s, first.id), "already_finished"));
    assert(!strcmp(daw_song_export_cancel(s, "wrong"), "unknown_export"));
    assert(daw_song_export_start(s, g, ENGINE_BOUNCE_WAV_PCM16, (uint64_t)c.sample_rate * 60 + 1,
                                 false, &first) == DAW_ACTION_INVALID);
    ui_init_panes(s);
    s->layout_runtime.mixer_ratio = .55;
    ui_layout_panes(s, 1600, 1000);
    assert(daw_song_export_panel_open(s));
    click(s, 2, false);
    assert(s->export_ui.format == 0);
    click(s, 2, true);
    click(s, 3, true);
    click(s, 4, true);
    assert(s->export_ui.format == 1 && s->export_ui.tail_choice == 1 && s->export_ui.normalize);
    click(s, 0, true);
    assert(daw_song_export_get(s, NULL, &first));
    click(s, 5, true);
    assert(!s->export_ui.open);
    done = wait_job(s, first.id);
    assert(done.verified);
    assert(daw_song_export_panel_open(s) && s->export_ui.format == 1);
    SDL_Event lost = {.type = SDL_WINDOWEVENT};
    lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    daw_song_export_panel_event(s, &lost);
    assert(!s->export_ui.open && s->export_ui.pressed == -1);
    // Cancellation keeps the job displayed at press when another client starts newer work.
    assert(daw_song_export_panel_open(s));
    DawSongExportLayout layout;
    daw_song_export_panel_layout(s, &layout);
    SDL_Rect cancel_rect = layout.buttons[1];
    SDL_Event edge = {.type = SDL_MOUSEBUTTONDOWN};
    edge.button.button = SDL_BUTTON_LEFT;
    edge.button.x = cancel_rect.x + cancel_rect.w / 2;
    edge.button.y = cancel_rect.y + cancel_rect.h / 2;
    assert(daw_song_export_panel_event(s, &edge));
    assert(daw_song_export_start(s, g, ENGINE_BOUNCE_WAV_FLOAT32, (uint64_t)c.sample_rate * 60,
                                 false, &first) == DAW_ACTION_APPLIED);
    edge.type = SDL_MOUSEBUTTONUP;
    assert(daw_song_export_panel_event(s, &edge));
    DawSongExportInfo newer;
    assert(daw_song_export_get(s, first.id, &newer));
    assert(strcmp(newer.state, "cancelling") && strcmp(newer.state, "cancelled"));
    assert(!strcmp(daw_song_export_cancel(s, first.id), "cancel_requested"));
    (void)wait_job(s, first.id);
    click(s, 5, true);
    // Captured streaming cannot replace an existing destination even under a concurrent collision.
    EngineBounceOptions options = engine_bounce_options_default();
    EngineExportSnapshot* captured =
        engine_export_prepare(s->engine, 0, 3072, &options, ENGINE_BOUNCE_WAV_FLOAT32, true);
    assert(captured);
    FILE* sentinel = fopen(input, "rb");
    assert(sentinel);
    fclose(sentinel);
    assert(engine_export_run(captured, input, NULL) == DAW_SAVE_FAILED);
    engine_export_free(captured);
    assert(audio_media_clip_load_wav(input, c.sample_rate, &out) && out.frame_count == 2048 &&
           out.samples[0] == .25f);
    audio_media_clip_free(&out);
    // Session adoption cancels/drains outstanding work before retiring the old sample cache.
    char project[512];
    snprintf(project, sizeof(project), "%s/project.json", root);
    assert(session_save_to_file(s, project));
    assert(daw_song_export_start(s, g, ENGINE_BOUNCE_WAV_FLOAT32, (uint64_t)c.sample_rate * 60,
                                 false, &first) == DAW_ACTION_APPLIED);
    daw_mix_snapshot_free(g);
    assert(session_load_from_file(s, project));
    assert(!s->exports && !s->export_ui.open);
    assert(access(done.path, F_OK) == 0); // Prior verified files survive project replacement.
    daw_song_export_shutdown(s);
    engine_destroy(s->engine);
    tempo_map_free(&s->tempo_map);
    time_signature_map_free(&s->time_signature_map);
    free(s);
    printf("song_export_test: passed (immutable graph, native/MCP shared choices, original levels, "
           "PCM16/float32, tails, monotonic progress, cancellation, no-overwrite, ownership and "
           "history); fixture=%s\n",
           root);
    return 0;
}
