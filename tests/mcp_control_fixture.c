#include "app_state.h"
#include "engine/engine_internal.h"
#include "daw/ab_comparison.h"
#include "daw/control_bridge.h"
#include "daw/app_actions.h"
#include "daw/midi_actions.h"
#include "daw/automation_actions.h"
#include "daw/track_controls.h"
#include "daw/track_mix.h"
#include "input/timeline_input.h"
#include "daw/eq_actions.h"
#include "input/effects_panel_eq_detail_input.h"
#include "ui/effects_panel_eq_detail.h"
#include "test_assert.h"
#include <signal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
// Requests fixture shutdown without accessing application objects from a signal handler.
static void stop_fixture(int signal_number) { (void)signal_number; stopped = 1; }

// Renders an isolated phrase to deterministic float and listening WAVs for MCP loop acceptance.
static void render_phrase(AppState* state, const char* directory, const char* label) {
    if (!directory || (strcmp(label, "baseline") && strcmp(label, "edited") && strcmp(label, "undo") &&
        strcmp(label, "redo") && strcmp(label, "motion") && strcmp(label, "rounded_keys") &&
        strcmp(label, "rounded_pluck") && strcmp(label, "rounded_pad") && strcmp(label, "rounded_pad_chord") && strcmp(label, "filtered_keys") &&
        strcmp(label, "filtered_pad") && strcmp(label, "filtered_pad_chord") && strcmp(label, "composition") && strcmp(label, "sweep_bass") && strcmp(label, "filter_pluck") && strcmp(label, "moving_pad") && strcmp(label, "mod_edit") && strcmp(label, "mod_undo") && strcmp(label, "mod_redo") && strcmp(label, "automated") &&
        strcmp(label, "auto_curve") && strcmp(label, "auto_redo") && strcmp(label, "auto_cleared") &&
        strcmp(label, "undone") && strcmp(label, "redone") && strcmp(label, "reopened"))) exit(4);
    EngineBounceBuffer buffer = {0};
    EngineBounceOptions options = engine_bounce_options_default();
    options.normalize_if_clipping = false;
    uint64_t frames = 120000;
    if (!strcmp(label, "composition")) {
        frames = 0; const EngineTrack* tracks = engine_get_tracks(state->engine);
        for (int t=0;t<engine_get_track_count(state->engine);++t) for (int c=0;c<tracks[t].clip_count;++c) {
            uint64_t end=tracks[t].clips[c].timeline_start_frames+tracks[t].clips[c].duration_frames;
            if (end>frames) frames=end;
        }
        if (!frames || frames>480000) exit(4);
    }
    if (!engine_bounce_range_to_buffer_with_options(state->engine, 0, frames, &options, NULL, NULL, &buffer)) exit(4);
    double sum = 0, peak = 0;
    uint64_t count = buffer.frame_count * (uint64_t)buffer.channels;
    for (uint64_t i = 0; i < count; ++i) {
        float value = buffer.data[i];
        if (!isfinite(value)) exit(4);
        sum += (double)value * value;
        if (fabs(value) > peak) peak = fabs(value);
    }
    char path[SESSION_PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s.wav", directory, label) >= (int)sizeof(path) ||
        !engine_bounce_write_wav(&buffer, path, ENGINE_BOUNCE_WAV_FLOAT32)) exit(4);
    if (snprintf(path, sizeof(path), "%s/%s-listen.wav", directory, label) >= (int)sizeof(path) ||
        !engine_bounce_write_wav(&buffer, path, ENGINE_BOUNCE_WAV_PCM16)) exit(4);
    printf("DONE {\"peak\":%.9g,\"rms\":%.9g,\"frames\":%llu,\"rate\":%d,\"channels\":%d,\"notes\":%d}\n",
        peak, sqrt(sum / count), (unsigned long long)buffer.frame_count, buffer.sample_rate, buffer.channels,
        engine_clip_midi_note_count(&engine_get_tracks(state->engine)[0].clips[0]));
    fflush(stdout);
    engine_bounce_buffer_free(&buffer);
}

// Supplies a paused dummy comparison endpoint without running the app or physical output.
static void comparison_silence(float* output,int frames,int channels,void* user){(void)user;memset(output,0,(size_t)frames*channels*sizeof(float));}

// Runs the production bridge and engine on their creating thread without user data or devices.
int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) return 2;
    /* Refuse unsafe endpoints before expensive sampled-instrument preparation. */
    DawControlBridge* bridge = daw_control_bridge_start(argv[1], !strcmp(argv[2], "write") || !strcmp(argv[2], "empty"));
    if (!bridge) return 3;
    AppState* state = calloc(1, sizeof(*state));
    if (!state) return 2;
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    if (!strcmp(argv[2], "high-rate")) config.sample_rate = 192000;
    state->engine = engine_create(&config);
    if (!state->engine) return 2;
    daw_data_paths_set_defaults(&state->data_paths);if(argc==4)SDL_strlcpy(state->data_paths.output_root,argv[3],sizeof(state->data_paths.output_root));
    undo_manager_init(&state->undo);
    state->tempo = tempo_state_default(config.sample_rate);
    tempo_map_init(&state->tempo_map, config.sample_rate);
    time_signature_map_init(&state->time_signature_map);
    int clip = -1;
    bool empty = strcmp(argv[2], "empty") == 0;
    bool mix_project = strcmp(argv[2], "mix-project") == 0;
    if (mix_project && argc != 4) return 2;
    if (empty) while (engine_get_track_count(state->engine))
        if (!engine_remove_track(state->engine, engine_get_track_count(state->engine)-1)) return 2;
    if (!empty && !mix_project && !engine_add_midi_clip_to_track(state->engine, 0, 0, 120000, &clip)) return 2;
    EngineMidiNote note = {.start_frame = 0, .duration_frames = 24000, .note = 60, .velocity = .75f};
    if (!empty && !mix_project && !engine_clip_midi_add_note(state->engine, 0, clip, note, NULL)) return 2;
    // Load only a caller-staged private fixture; no project/runtime files are written.
    if (mix_project) { SessionDocument doc; session_document_init(&doc);
        bool loaded = session_document_read_file(argv[3], &doc) && session_apply_document(state, &doc);
        session_document_free(&doc); if (!loaded) return 2; }
    if(!strcmp(argv[2],"mix-fx")){
        FxInstId comp=engine_fx_track_add(state->engine,0,20),limiter=engine_fx_track_add(state->engine,0,21);
        if(!comp || !limiter || !engine_fx_track_set_param(state->engine,0,comp,0,-36) || !engine_fx_track_set_param(state->engine,0,limiter,1,3))return 2;
    }
    signal(SIGTERM, stop_fixture);
    signal(SIGINT, stop_fixture);
    puts("READY"); fflush(stdout);
    struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
    int changes = 0;
    while (!stopped) {
        if (daw_control_bridge_poll(bridge, state)) ++changes;
        if (poll(&input, 1, 2) > 0 && input.revents & POLLIN) {
            char command[32];
            if (!fgets(command, sizeof(command), stdin)) break;
            if(!strcmp(command,"ab_open_device\n")){AudioDeviceSpec spec={config.sample_rate,128,2};if(!audio_device_open(&state->engine->device,&spec,comparison_silence,NULL))return 4;}
            if(!strcmp(command,"live_tick\n")){float mixed[256],scratch[256];engine_source_plan_apply(state->engine);for(uint64_t frame=0;frame<24000;frame+=128)engine_mix_tracks(state->engine,frame,128,mixed,scratch,2);}
            if(!strcmp(command,"ab_tick\n")){float output[128*8];audio_device_preview_render(&state->engine->device,output,128,state->engine->device.spec.channels);}
            if (strncmp(command, "render ", 7) == 0) {
                command[strcspn(command, "\n")] = '\0';
                render_phrase(state, argc == 4 ? argv[3] : NULL, command + 7);
                continue;
            }
            if (strcmp(command, "reopen\n") == 0) {
                if (argc != 4) return 4;
                char path[SESSION_PATH_MAX]; snprintf(path, sizeof(path), "%s/midi-project.json", argv[3]);
                SessionDocument project; session_document_init(&project);
                if (!session_document_capture(state, &project) || !session_document_write_file(&project, path)) return 4;
                session_document_free(&project); session_document_init(&project);
                if (!session_document_read_file(path, &project) || !session_apply_document(state, &project)) return 4;
                session_document_free(&project);
            }
            if (strcmp(command, "midi_edit\n") == 0) {
                EngineMidiNote other = {24000, 1000, 61, .5f};
                if (!engine_clip_midi_add_note(state->engine, 0, clip, other, NULL)) return 4;
            }
            if (strcmp(command, "tempo_edit\n") == 0) {
                if (!tempo_map_upsert_event(&state->tempo_map, 1, 180)) return 4;
            }
            if (strcmp(command, "pages\n") == 0) {
                EngineMidiNote notes[ENGINE_MIDI_NOTE_CAP];
                for (int i = 0; i < ENGINE_MIDI_NOTE_CAP; ++i) notes[i] = (EngineMidiNote){(uint64_t)i*10, 5, (uint8_t)(48+i%24), .5f};
                if (!engine_clip_midi_set_notes(state->engine, 0, clip, notes, ENGINE_MIDI_NOTE_CAP)) return 4;
                for (int i = 1; i < DAW_MIDI_TEMPO_EVENT_CAP; ++i)
                    if (!tempo_map_upsert_event(&state->tempo_map, i, 120+i%10)) return 4;
                undo_manager_clear(&state->undo);
            }
            if (strcmp(command, "phrase\n") == 0) {
                EngineMidiNote notes[8];
                const uint8_t pitches[] = {60, 64, 67, 72, 69, 67, 64, 60};
                for (int i = 0; i < 8; ++i) notes[i] = (EngineMidiNote){
                    .start_frame = (uint64_t)i * 12000, .duration_frames = 8640,
                    .note = pitches[i], .velocity = i % 2 ? .65f : .8f};
                if (!engine_clip_midi_set_notes(state->engine, 0, clip, notes, 8)) return 4;
                undo_manager_clear(&state->undo);
            }
            if (strcmp(command, "chord\n") == 0) {
                const EngineMidiNote notes[] = {{0, 84000, 48, .65f}, {0, 84000, 55, .55f},
                    {0, 84000, 60, .5f}, {0, 84000, 64, .5f}};
                if (!engine_clip_midi_set_notes(state->engine, 0, clip, notes, 4)) return 4;
                undo_manager_clear(&state->undo);
            }
            if (strcmp(command, "load_project\n") == 0) {
                char path[SESSION_PATH_MAX];
                snprintf(path, sizeof(path), "%s/baseline-input.json", argv[3]);
                SessionDocument project; session_document_init(&project);
                if (!session_document_read_file(path, &project) || !session_apply_document(state, &project)) return 4;
                session_document_free(&project);
                engine_transport_stop(state->engine);
            }
            if (strcmp(command, "automation_dense\n") == 0) {
                EngineAutomationPoint points[160];
                for (int i = 0; i < 160; ++i) points[i] = (EngineAutomationPoint){(uint64_t)i * 600, .001f * i, ENGINE_AUTOMATION_CURVE_LINEAR};
                EngineAutomationLane lane = {.target = ENGINE_AUTOMATION_TARGET_VOLUME, .points = points, .point_count = 160};
                if (!daw_automation_replace_lanes(state, 0, clip, &lane, 1)) return 4;
            }
            if (strcmp(command, "audio_region\n") == 0) {
                char path[SESSION_PATH_MAX];
                snprintf(path, sizeof(path), "%s/baseline.wav", argv[3]);
                int track = engine_add_track(state->engine);
                if (track < 0 || !engine_add_clip_to_track(state->engine, track, path, 0, NULL)) return 4;
            }
            if(strcmp(command,"native_eq_reset\n")==0){
                state->effects_panel.target=FX_PANEL_TARGET_TRACK;state->effects_panel.target_track_index=0;state->effects_panel.eq_detail.view_mode=EQ_DETAIL_VIEW_TRACK;
                EffectsPanelLayout layout={.detail_rect={0,0,800,400}};SDL_Rect graph=effects_panel_eq_detail_compute_graph_rect(&layout.detail_rect);
                SDL_Event event={.type=SDL_MOUSEBUTTONDOWN};event.button.button=SDL_BUTTON_LEFT;event.button.clicks=2;event.button.x=graph.x+graph.w/2;event.button.y=graph.y+graph.h/2;
                if(!effects_panel_eq_detail_handle_mouse_down(state,&layout,&event))return 4;
            }
            if(strcmp(command,"native_eq_fine\n")==0){
                UndoCommand cmd={.type=UNDO_CMD_EQ_CURVE};cmd.data.eq_curve_edit.track_index=0;
                if(!undo_command_bind_track(state,&cmd) || !undo_manager_begin_drag(&state->undo,&cmd))return 4;
                state->effects_panel.eq_detail.history_serial=state->undo.drag_serial;state->effects_panel.eq_detail.pending_apply=true;state->effects_panel.eq_detail.dragging=true;
                state->effects_panel.eq_curve.bands[1].gain_db+=.0005f;
                SDL_Event up={.type=SDL_MOUSEBUTTONUP};up.button.button=SDL_BUTTON_LEFT;if(!effects_panel_eq_detail_handle_mouse_up(state,&up))return 4;
            }
            if(strcmp(command,"native_eq_change\n")==0){EngineEqCurve c;if(!engine_get_eq_curve(state->engine,0,&c))return 4;c.bands[1].gain_db=-3;if(!engine_set_track_eq_curve(state->engine,0,&c))return 4;}
            if (strcmp(command,"native_gain\n")==0 || strcmp(command,"native_pan\n")==0) {
                const EngineTrack* row=&engine_get_tracks(state->engine)[0];bool pan=!strcmp(command,"native_pan\n");
                DawTrackMixEdit edit={engine_instance_id(state->engine),row->runtime_id,pan,pan ? row->pan : row->gain,pan ? -.25f : .75f};
                if(!daw_track_mix_write(state,&edit,edit.before,edit.after))return 4;
            }
            if(strcmp(command,"native_name\n")==0) {
                track_name_editor_start(state,0);strcpy(state->track_name_editor.buffer,"Native Violin");track_name_editor_stop(state,true);
            }
            if(strcmp(command,"native_mute_solo\n")==0) {
                if(!engine_track_set_muted(state->engine,0,true) || !engine_track_set_solo(state->engine,0,true))return 4;
            }
            if (strcmp(command, "changes\n") == 0) {
                printf("DONE %d\n", changes); fflush(stdout);
                continue;
            }
            if (strcmp(command, "replace\n") == 0) {
                Engine* replacement = engine_create(&config);
                if (!replacement) return 4;
                engine_destroy(state->engine);
                state->engine = replacement;
                undo_manager_clear(&state->undo);
            } else if (strcmp(command, "ui_edit\n") == 0) {
                DawActionTarget target;
                daw_instrument_target_capture(state, 0, clip, &target);
                DawInstrumentRequest patch = {.target = target, .set_preset = true, .preset = ENGINE_INSTRUMENT_PRESET_PLUCK};
                if (daw_action_set_instrument(state, &patch).status != DAW_ACTION_APPLIED) return 4;
            } else if (strcmp(command, "play_view\n") == 0) {
                if (!engine_transport_play(state->engine)) return 4;
            } else if (strcmp(command, "stop_view\n") == 0) {
                engine_transport_stop(state->engine);
            } else if (strcmp(command, "begin_gesture\n") == 0) {
                DawActionTarget target;
                daw_instrument_target_capture(state, 0, clip, &target);
                DawInstrumentGesture gesture = {0};
                daw_action_begin_instrument(state, target, &gesture);
            } else if (strcmp(command, "end_gesture\n") == 0) {
                undo_manager_cancel_drag(&state->undo);
            } else if (strcmp(command, "quit\n") == 0) break;
            printf("DONE %d %d\n", state->undo.undo_count, state->undo.redo_count); fflush(stdout);
        }
    }
    daw_control_bridge_stop(bridge);
    daw_song_export_shutdown(state);
    daw_ab_release(state);
    undo_manager_free(&state->undo);
    engine_destroy(state->engine);
    tempo_map_free(&state->tempo_map);
    time_signature_map_free(&state->time_signature_map);
    free(state);
    return 0;
}
