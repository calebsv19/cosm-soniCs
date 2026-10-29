#include "app_state.h"
#include "daw/control_bridge.h"
#include "daw/app_actions.h"
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
        strcmp(label, "rounded_pluck") && strcmp(label, "rounded_pad") && strcmp(label, "rounded_pad_chord"))) exit(4);
    EngineBounceBuffer buffer = {0};
    EngineBounceOptions options = engine_bounce_options_default();
    options.normalize_if_clipping = false;
    if (!engine_bounce_range_to_buffer_with_options(state->engine, 0, 120000, &options, NULL, NULL, &buffer)) exit(4);
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

// Runs the production bridge and engine on their creating thread without user data or devices.
int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) return 2;
    AppState* state = calloc(1, sizeof(*state));
    if (!state) return 2;
    EngineRuntimeConfig config;
    config_set_defaults(&config);
    state->engine = engine_create(&config);
    if (!state->engine) return 2;
    undo_manager_init(&state->undo);
    int clip;
    if (!engine_add_midi_clip_to_track(state->engine, 0, 0, 120000, &clip)) return 2;
    EngineMidiNote note = {.start_frame = 0, .duration_frames = 24000, .note = 60, .velocity = .75f};
    if (!engine_clip_midi_add_note(state->engine, 0, clip, note, NULL)) return 2;
    DawControlBridge* bridge = daw_control_bridge_start(argv[1], strcmp(argv[2], "write") == 0);
    if (!bridge) return 3;
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
            if (strncmp(command, "render ", 7) == 0) {
                command[strcspn(command, "\n")] = '\0';
                render_phrase(state, argc == 4 ? argv[3] : NULL, command + 7);
                continue;
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
    undo_manager_free(&state->undo);
    engine_destroy(state->engine);
    free(state);
    return 0;
}
