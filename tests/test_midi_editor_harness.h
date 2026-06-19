#ifndef DAW_TEST_MIDI_EDITOR_HARNESS_H
#define DAW_TEST_MIDI_EDITOR_HARNESS_H

#include "app_state.h"
#include "config.h"
#include "engine/engine.h"
#include "input/input_manager.h"
#include "input/midi_editor_input.h"
#include "input/midi_instrument_panel_input.h"
#include "time/tempo.h"
#include "ui/effects_panel.h"
#include "ui/layout.h"
#include "undo/undo_manager.h"

#include "test_assert.h"

#include <SDL2/SDL.h>

#include <string.h>

static inline void daw_midi_editor_test_state_init(const char* test_name,
                                                   AppState* state,
                                                   EngineRuntimeConfig* cfg) {
    memset(state, 0, sizeof(*state));
    config_set_defaults(cfg);
    cfg->sample_rate = 48000;
    state->runtime_cfg = *cfg;
    state->engine = engine_create(cfg);
    daw_test_expect(test_name, state->engine != NULL, "engine_create failed");
    undo_manager_init(&state->undo);
    state->tempo = tempo_state_default(cfg->sample_rate);
    tempo_map_init(&state->tempo_map, cfg->sample_rate);
    time_signature_map_init(&state->time_signature_map);
    ui_init_panes(state);
    effects_panel_init(state);
    ui_layout_panes(state, 1280, 800);
    state->selected_track_index = -1;
    state->selected_clip_index = -1;
    state->active_track_index = 0;
}

static inline void daw_midi_editor_test_state_destroy(AppState* state) {
    undo_manager_free(&state->undo);
    time_signature_map_free(&state->time_signature_map);
    tempo_map_free(&state->tempo_map);
    engine_destroy(state->engine);
    state->engine = NULL;
}

static inline void daw_midi_editor_test_dispatch_mouse_button(const char* test_name,
                                                              InputManager* manager,
                                                              AppState* state,
                                                              Uint32 type,
                                                              int x,
                                                              int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    daw_test_expect(test_name,
                    midi_editor_input_handle_event(manager, state, &event),
                    "MIDI editor mouse event not consumed");
}

static inline void daw_midi_editor_test_dispatch_mouse_motion(const char* test_name,
                                                              InputManager* manager,
                                                              AppState* state,
                                                              int x,
                                                              int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.x = x;
    event.motion.y = y;
    daw_test_expect(test_name,
                    midi_editor_input_handle_event(manager, state, &event),
                    "MIDI editor motion event not consumed");
}

static inline void daw_midi_editor_test_dispatch_mouse_wheel(const char* test_name,
                                                             InputManager* manager,
                                                             AppState* state,
                                                             int x,
                                                             int y,
                                                             int wheel_y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    state->mouse_x = x;
    state->mouse_y = y;
    event.type = SDL_MOUSEWHEEL;
    event.wheel.y = wheel_y;
    daw_test_expect(test_name,
                    midi_editor_input_handle_event(manager, state, &event),
                    "MIDI editor wheel event not consumed");
}

static inline void daw_midi_editor_test_dispatch_instrument_mouse_button(const char* test_name,
                                                                         InputManager* manager,
                                                                         AppState* state,
                                                                         Uint32 type,
                                                                         int x,
                                                                         int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    daw_test_expect(test_name,
                    midi_instrument_panel_input_handle_event(manager, state, &event),
                    "MIDI instrument panel mouse event not consumed");
}

static inline void daw_midi_editor_test_dispatch_instrument_mouse_motion(const char* test_name,
                                                                         InputManager* manager,
                                                                         AppState* state,
                                                                         int x,
                                                                         int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.x = x;
    event.motion.y = y;
    daw_test_expect(test_name,
                    midi_instrument_panel_input_handle_event(manager, state, &event),
                    "MIDI instrument panel motion event not consumed");
}

static inline void daw_midi_editor_test_dispatch_key(const char* test_name,
                                                     InputManager* manager,
                                                     AppState* state,
                                                     SDL_Keycode key) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    daw_test_expect(test_name,
                    midi_editor_input_handle_event(manager, state, &event),
                    "MIDI editor key event not consumed");
}

static inline void daw_midi_editor_test_dispatch_key_event(const char* test_name,
                                                           InputManager* manager,
                                                           AppState* state,
                                                           Uint32 type,
                                                           SDL_Keycode key) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key.keysym.sym = key;
    event.key.repeat = 0;
    daw_test_expect(test_name,
                    midi_editor_input_handle_event(manager, state, &event),
                    "MIDI editor key event not consumed");
}

static inline void daw_midi_editor_test_dispatch_key_with_mod(const char* test_name,
                                                              InputManager* manager,
                                                              AppState* state,
                                                              SDL_Keycode key,
                                                              SDL_Keymod mod) {
    SDL_Keymod old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | mod));
    daw_midi_editor_test_dispatch_key(test_name, manager, state, key);
    SDL_SetModState(old_mods);
}

static inline void daw_midi_editor_test_dispatch_command_key(const char* test_name,
                                                             InputManager* manager,
                                                             AppState* state,
                                                             SDL_Keycode key) {
    daw_midi_editor_test_dispatch_key_with_mod(test_name, manager, state, key, KMOD_CTRL);
}

static inline bool daw_midi_editor_test_handle_key_with_mod(InputManager* manager,
                                                            AppState* state,
                                                            SDL_Keycode key,
                                                            SDL_Keymod mod) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    event.key.keysym.mod = mod;
    event.key.repeat = 0;
    SDL_Keymod old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | mod));
    bool consumed = midi_editor_input_handle_event(manager, state, &event);
    SDL_SetModState(old_mods);
    return consumed;
}

static inline void daw_midi_editor_test_dispatch_wheel_with_mod(const char* test_name,
                                                                InputManager* manager,
                                                                AppState* state,
                                                                int mouse_x,
                                                                int mouse_y,
                                                                int wheel_y,
                                                                SDL_Keymod mod) {
    SDL_Keymod old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | mod));
    state->mouse_x = mouse_x;
    state->mouse_y = mouse_y;
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEWHEEL;
    event.wheel.y = wheel_y;
    daw_test_expect(test_name,
                    midi_editor_input_handle_event(manager, state, &event),
                    "MIDI editor wheel event not consumed");
    SDL_SetModState(old_mods);
}

#endif
