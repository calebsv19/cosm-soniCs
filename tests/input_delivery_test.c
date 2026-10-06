#include "app_state.h"
#include "app/audio_recording.h"
#include "input/input_manager.h"
#include "input/project_modal_input.h"
#include "input/timeline_selection.h"
#include "undo/undo_manager.h"
#include "audio/wav_writer.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Delivers a complete key press without an intervening frame/update or sampled keyboard state.
static void key_press(AppState* state, SDL_Keycode key, SDL_Keymod mods) {
    SDL_Event event = {.type = SDL_KEYDOWN};
    event.key.keysym.sym = key;
    event.key.keysym.mod = mods;
    input_manager_handle_event(&state->input_manager, state, &event);
    event.type = SDL_KEYUP;
    input_manager_handle_event(&state->input_manager, state, &event);
}

// Proves rapid transport edges and modal text focus independently of OS event timing.
int main(void) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_EVENTS) == 0);
    AppState* state = calloc(1, sizeof(*state));
    assert(state);
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    state->engine = engine_create(&cfg);
    assert(state->engine);
    daw_audio_recording_init(&state->audio_recording);
    input_manager_init(&state->input_manager);
    state->transport_ui.play_rect = (SDL_Rect){10, 10, 50, 20};
    state->transport_ui.stop_rect = (SDL_Rect){70, 10, 50, 20};
    SDL_Event click = {.type = SDL_MOUSEBUTTONDOWN};
    click.button.button = SDL_BUTTON_LEFT;
    click.button.x = 20; click.button.y = 20;
    input_manager_handle_event(&state->input_manager, state, &click);
    assert(!engine_transport_requested_playing(state->engine)); // Press only owns the control.
    click.type = SDL_MOUSEBUTTONUP;
    input_manager_handle_event(&state->input_manager, state, &click);
    assert(engine_transport_requested_playing(state->engine));
    key_press(state, SDLK_SPACE, KMOD_NONE);
    assert(!engine_transport_requested_playing(state->engine));
    key_press(state, SDLK_SPACE, KMOD_NONE);
    assert(engine_transport_requested_playing(state->engine));
    SDL_Event repeat = {.type = SDL_KEYDOWN};
    repeat.key.keysym.sym = SDLK_SPACE; repeat.key.repeat = 1;
    input_manager_handle_event(&state->input_manager, state, &repeat);
    assert(engine_transport_requested_playing(state->engine));
    click.type = SDL_MOUSEBUTTONDOWN; click.button.x = 80;
    input_manager_handle_event(&state->input_manager, state, &click);
    assert(engine_transport_requested_playing(state->engine));
    click.type=SDL_MOUSEBUTTONUP;
    input_manager_handle_event(&state->input_manager,state,&click);
    assert(!engine_transport_requested_playing(state->engine));
    // Focused Enter activates on release once; Tab reaches the next registered control.
    repeat.key.keysym.sym = SDLK_RETURN; repeat.key.repeat = 0;
    input_manager_handle_event(&state->input_manager, state, &repeat);
    assert(!engine_transport_requested_playing(state->engine));
    repeat.key.repeat = 1;
    input_manager_handle_event(&state->input_manager, state, &repeat);
    repeat.type = SDL_KEYUP;
    input_manager_handle_event(&state->input_manager, state, &repeat);
    assert(!engine_transport_requested_playing(state->engine)); // STOP still focused.
    key_press(state, SDLK_TAB, KMOD_NONE);
    key_press(state, SDLK_RETURN, KMOD_NONE);
    assert(engine_transport_requested_playing(state->engine));
    engine_transport_stop(state->engine);
    // Product toggles retain their meaning, with no mutation until matching release.
    state->transport_ui.grid_rect = (SDL_Rect){130,10,50,20};
    click.type=SDL_MOUSEBUTTONDOWN;click.button.x=140;
    bool old_grid=state->timeline_show_all_grid_lines;
    input_manager_handle_event(&state->input_manager,state,&click);
    assert(state->timeline_show_all_grid_lines==old_grid);
    click.type=SDL_MOUSEBUTTONUP;
    input_manager_handle_event(&state->input_manager,state,&click);
    assert(state->timeline_show_all_grid_lines!=old_grid);
    state->transport_ui.beat_toggle_rect = (SDL_Rect){190,10,30,20};
    click.type=SDL_MOUSEBUTTONDOWN;click.button.x=200;
    bool old_beats=state->timeline_view_in_beats;
    input_manager_handle_event(&state->input_manager,state,&click);
    assert(state->timeline_view_in_beats==old_beats);
    click.type=SDL_MOUSEBUTTONUP;
    input_manager_handle_event(&state->input_manager,state,&click);
    assert(state->timeline_view_in_beats!=old_beats);
    // A release elsewhere, changed geometry, focus loss or modal takeover cannot play.
    click.type=SDL_MOUSEBUTTONDOWN;click.button.x=20;
    input_manager_handle_event(&state->input_manager,state,&click);
    click.type=SDL_MOUSEBUTTONUP;click.button.x=150;
    input_manager_handle_event(&state->input_manager,state,&click);
    assert(!engine_transport_requested_playing(state->engine));
    click.type=SDL_MOUSEBUTTONDOWN;click.button.x=20;
    input_manager_handle_event(&state->input_manager,state,&click);
    state->transport_ui.play_rect.x=30;click.type=SDL_MOUSEBUTTONUP;click.button.x=40;
    input_manager_handle_event(&state->input_manager,state,&click);
    assert(!engine_transport_requested_playing(state->engine));state->transport_ui.play_rect.x=10;
    click.type=SDL_MOUSEBUTTONDOWN;click.button.x=20;
    input_manager_handle_event(&state->input_manager,state,&click);
    SDL_Event lost={.type=SDL_WINDOWEVENT};lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    input_manager_handle_event(&state->input_manager,state,&lost);
    click.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&state->input_manager,state,&click);
    assert(!engine_transport_requested_playing(state->engine));
    click.type=SDL_MOUSEBUTTONDOWN;input_manager_handle_event(&state->input_manager,state,&click);
    project_modal_input_open_save_prompt(state);
    click.type=SDL_MOUSEBUTTONUP;input_manager_handle_event(&state->input_manager,state,&click);
    assert(!engine_transport_requested_playing(state->engine));
    key_press(state, SDLK_r, KMOD_NONE);
    key_press(state, SDLK_SPACE, KMOD_NONE);
    assert(state->audio_recording.status == DAW_AUDIO_RECORDING_IDLE);
    assert(!state->audio_recording.capture_device_open);
    assert(!engine_transport_requested_playing(state->engine));
    SDL_Event text = {.type = SDL_TEXTINPUT};
    SDL_strlcpy(text.text.text, "Rehearsal", sizeof(text.text.text));
    input_manager_handle_event(&state->input_manager, state, &text);
    assert(!strcmp(state->project_prompt.buffer, "Rehearsal"));
    key_press(state, SDLK_ESCAPE, KMOD_NONE);
    assert(!project_modal_input_active(state));
    state->project_load.active = true;
    key_press(state, SDLK_r, KMOD_NONE);
    assert(!state->audio_recording.capture_device_open);
    state->project_load.count = 1; state->project_load.selected_index = 0;
    SDL_strlcpy(state->project_load.entries[0].path, "/no-such-daw-s5-project.json", sizeof(state->project_load.entries[0].path));
    Engine* original_engine = state->engine;
    key_press(state, SDLK_RETURN, KMOD_NONE);
    assert(state->project_load.active && state->project_load.error[0]);
    assert(state->engine == original_engine && state->project_load.selected_index == 0);
    key_press(state, SDLK_ESCAPE, KMOD_NONE);
    assert(!state->project_load.active);
    char cwd[4096]; assert(getcwd(cwd, sizeof(cwd)));
    char root[] = "/tmp/daw-s5-modal-XXXXXX"; assert(mkdtemp(root)); assert(chdir(root) == 0);
    assert(mkdir("config", 0700) == 0);
    assert(mkdir("config/projects", 0700) == 0);
    FILE* file = fopen("config/projects/fixture.json", "w"); assert(file); fputs("{}", file); fclose(file);
    snprintf(state->data_paths.output_root, sizeof(state->data_paths.output_root), "%s/config", root);
    ProjectInfo entries[8]; int count = 0;
    assert(project_manager_list(state, entries, 8, &count) && count == 1);
    file = fopen("blocked", "w"); assert(file); fclose(file);
    snprintf(state->data_paths.output_root, sizeof(state->data_paths.output_root), "%s/blocked", root);
    project_modal_input_open_save_prompt(state);
    SDL_strlcpy(state->project_prompt.buffer, "KeepThisName", sizeof(state->project_prompt.buffer));
    key_press(state, SDLK_RETURN, KMOD_NONE);
    assert(state->project_prompt.active && state->project_prompt.error[0]);
    assert(!strcmp(state->project_prompt.buffer, "KeepThisName"));
    key_press(state, SDLK_ESCAPE, KMOD_NONE);
    assert(chdir(cwd) == 0);
    state->track_name_editor.editing = true;
    key_press(state, SDLK_SPACE, KMOD_NONE);
    assert(!engine_transport_requested_playing(state->engine));
    state->track_name_editor.editing = false;
    undo_manager_init(&state->undo);
    char audio_path[4096]; snprintf(audio_path, sizeof(audio_path), "%s/shortcut.wav", root);
    float audio[1024] = {0}; assert(wav_write_f32(audio_path, audio, 1024, 1, cfg.sample_rate));
    assert(engine_add_clip_to_track(state->engine, 0, audio_path, 0, NULL));
    timeline_selection_set_single(state, 0, 0);
    key_press(state, SDLK_d, KMOD_GUI);
    assert(state->undo.undo_count == 1 && engine_get_tracks(state->engine)[0].clip_count == 2);
    key_press(state, SDLK_z, KMOD_GUI);
    assert(state->undo.redo_count == 1 && engine_get_tracks(state->engine)[0].clip_count == 1);
    SDL_Event history_repeat = {.type = SDL_KEYDOWN};
    history_repeat.key.keysym.sym = SDLK_z; history_repeat.key.keysym.mod = KMOD_GUI | KMOD_SHIFT; history_repeat.key.repeat = 1;
    input_manager_handle_event(&state->input_manager, state, &history_repeat);
    assert(state->undo.redo_count == 1 && engine_get_tracks(state->engine)[0].clip_count == 1);
    key_press(state, SDLK_z, KMOD_GUI | KMOD_SHIFT);
    assert(state->undo.undo_count == 1 && engine_get_tracks(state->engine)[0].clip_count == 2);
    state->track_name_editor.editing = true;
    key_press(state, SDLK_z, KMOD_GUI);
    assert(engine_get_tracks(state->engine)[0].clip_count == 2);
    state->track_name_editor.editing = false;
    key_press(state, SDLK_BACKSPACE, KMOD_NONE);
    assert(state->undo.undo_count == 2 && engine_get_tracks(state->engine)[0].clip_count == 1);
    history_repeat.key.keysym.sym = SDLK_BACKSPACE; history_repeat.key.keysym.mod = KMOD_NONE;
    input_manager_handle_event(&state->input_manager, state, &history_repeat);
    assert(state->undo.undo_count == 2 && engine_get_tracks(state->engine)[0].clip_count == 1);
    key_press(state, SDLK_z, KMOD_GUI);
    assert(engine_get_tracks(state->engine)[0].clip_count == 2 && state->selection_count == 1);
    // Held duplicate/paste commands do not insert extra clips or advance history.
    key_press(state, SDLK_c, KMOD_GUI);
    int undo_count = state->undo.undo_count;
    history_repeat.key.keysym.mod = KMOD_GUI;
    history_repeat.key.keysym.sym = SDLK_d;
    input_manager_handle_event(&state->input_manager, state, &history_repeat);
    history_repeat.key.keysym.sym = SDLK_v;
    input_manager_handle_event(&state->input_manager, state, &history_repeat);
    assert(engine_get_tracks(state->engine)[0].clip_count == 2 && state->undo.undo_count == undo_count);
    key_press(state, SDLK_v, KMOD_GUI);
    assert(engine_get_tracks(state->engine)[0].clip_count == 3 && state->undo.undo_count == undo_count + 1);
    key_press(state, SDLK_z, KMOD_CTRL);
    assert(engine_get_tracks(state->engine)[0].clip_count == 2 && state->undo.undo_count == undo_count);
    undo_manager_free(&state->undo);
    daw_audio_recording_free(&state->audio_recording);
    engine_destroy(state->engine);
    free(state);
    SDL_Quit();
    puts("input_delivery_test: success (rapid transport/history, edit repeat suppression, modal shortcut isolation)");
    return 0;
}
