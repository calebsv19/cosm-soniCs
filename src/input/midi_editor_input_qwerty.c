#include "input/midi_editor_input.h"
#include "input/midi_editor_input_internal.h"

#include "app_state.h"

#include <SDL2/SDL.h>
#include <string.h>

#define MIDI_EDITOR_DEFAULT_VELOCITY 0.80f

float midi_editor_clamp_velocity(float velocity) {
    if (velocity < 0.0f) return 0.0f;
    if (velocity > 1.0f) return 1.0f;
    return velocity;
}

float midi_editor_current_default_velocity(const AppState* state) {
    if (!state || state->midi_editor_ui.default_velocity <= 0.0f) {
        return MIDI_EDITOR_DEFAULT_VELOCITY;
    }
    return midi_editor_clamp_velocity(state->midi_editor_ui.default_velocity);
}

void midi_editor_set_default_velocity(AppState* state, float velocity) {
    if (!state) {
        return;
    }
    state->midi_editor_ui.default_velocity = midi_editor_clamp_velocity(velocity);
}

int midi_editor_clamp_octave_offset(int offset) {
    if (offset < -2) return -2;
    if (offset > 2) return 2;
    return offset;
}

uint8_t midi_editor_apply_octave_offset(int base_note, int octave_offset) {
    int note = base_note + midi_editor_clamp_octave_offset(octave_offset) * 12;
    if (note < ENGINE_MIDI_NOTE_MIN) note = ENGINE_MIDI_NOTE_MIN;
    if (note > ENGINE_MIDI_NOTE_MAX) note = ENGINE_MIDI_NOTE_MAX;
    return (uint8_t)note;
}

bool midi_editor_qwerty_note_for_key(SDL_Keycode key, uint8_t* out_note) {
    uint8_t note = 0;
    switch (key) {
    case SDLK_z: note = 48; break; /* C3 */
    case SDLK_x: note = 50; break;
    case SDLK_c: note = 52; break;
    case SDLK_v: note = 53; break;
    case SDLK_b: note = 55; break;
    case SDLK_n: note = 57; break;
    case SDLK_m: note = 59; break;
    case SDLK_a: note = 60; break; /* C4 */
    case SDLK_w: note = 61; break;
    case SDLK_s: note = 62; break;
    case SDLK_e: note = 63; break;
    case SDLK_d: note = 64; break;
    case SDLK_f: note = 65; break;
    case SDLK_t: note = 66; break;
    case SDLK_g: note = 67; break;
    case SDLK_y: note = 68; break;
    case SDLK_h: note = 69; break;
    case SDLK_u: note = 70; break;
    case SDLK_j: note = 71; break;
    case SDLK_k: note = 72; break;
    default:
        return false;
    }
    if (out_note) {
        *out_note = note;
    }
    return true;
}

int midi_editor_find_active_qwerty_note(const AppState* state, SDL_Keycode key) {
    if (!state) {
        return -1;
    }
    for (int i = 0; i < MIDI_EDITOR_QWERTY_ACTIVE_NOTE_CAPACITY; ++i) {
        if (state->midi_editor_ui.qwerty_active_notes[i].active &&
            state->midi_editor_ui.qwerty_active_notes[i].key == key) {
            return i;
        }
    }
    return -1;
}

int midi_editor_find_free_qwerty_note_slot(const AppState* state) {
    if (!state) {
        return -1;
    }
    for (int i = 0; i < MIDI_EDITOR_QWERTY_ACTIVE_NOTE_CAPACITY; ++i) {
        if (!state->midi_editor_ui.qwerty_active_notes[i].active) {
            return i;
        }
    }
    return -1;
}

void midi_editor_clear_qwerty_active_notes(AppState* state) {
    if (!state) {
        return;
    }
    if (state->engine) {
        engine_midi_audition_all_notes_off(state->engine);
    }
    memset(state->midi_editor_ui.qwerty_active_notes,
           0,
           sizeof(state->midi_editor_ui.qwerty_active_notes));
}

bool midi_editor_complete_qwerty_note(AppState* state, int active_index) {
    if (!state || active_index < 0 || active_index >= MIDI_EDITOR_QWERTY_ACTIVE_NOTE_CAPACITY) {
        return false;
    }
    MidiEditorQwertyActiveNote active = state->midi_editor_ui.qwerty_active_notes[active_index];
    state->midi_editor_ui.qwerty_active_notes[active_index].active = false;
    if (state->engine) {
        (void)engine_midi_audition_note_off(state->engine, active.note);
    }
    if (!active.active) {
        return false;
    }
    if (!active.record_on_release) {
        return true;
    }

    const EngineClip* clip = midi_editor_clip_from_indices(state, active.track_index, active.clip_index);
    if (!clip || clip->creation_index != active.clip_creation_index) {
        return false;
    }

    uint64_t end_frame = active.start_frame;
    if (!midi_editor_transport_relative_frame(state, clip, true, &end_frame)) {
        end_frame = active.start_frame;
    }
    end_frame = midi_editor_snap_relative_frame(state, clip, end_frame);
    uint64_t clip_frames = clip->duration_frames > 0 ? clip->duration_frames : 1u;
    uint64_t min_duration = midi_editor_min_note_duration(clip_frames);
    if (end_frame <= active.start_frame) {
        end_frame = active.start_frame + min_duration;
    }
    if (end_frame > clip_frames) {
        end_frame = clip_frames;
    }
    uint64_t duration = end_frame > active.start_frame ? end_frame - active.start_frame : min_duration;
    if (active.start_frame + duration > clip_frames) {
        duration = clip_frames > active.start_frame ? clip_frames - active.start_frame : min_duration;
    }
    if (duration < min_duration) {
        duration = min_duration;
    }
    if (active.start_frame + duration > clip_frames) {
        return false;
    }

    MidiEditorSelection selection = {0};
    selection.track_index = active.track_index;
    selection.clip_index = active.clip_index;
    selection.clip = clip;
    EngineMidiNote* before_notes = NULL;
    int before_count = 0;
    if (!midi_editor_snapshot_notes(clip, &before_notes, &before_count)) {
        return false;
    }

    EngineMidiNote note = {active.start_frame, duration, active.note, midi_editor_clamp_velocity(active.velocity)};
    int new_index = -1;
    if (!engine_clip_midi_add_note(state->engine, active.track_index, active.clip_index, note, &new_index)) {
        midi_editor_free_notes(&before_notes, &before_count);
        return false;
    }
    midi_editor_select_note(state, &selection, new_index);
    return midi_editor_push_note_undo(state, &selection, before_notes, before_count);
}

void midi_editor_complete_all_qwerty_notes(AppState* state) {
    if (!state) {
        return;
    }
    for (int i = 0; i < MIDI_EDITOR_QWERTY_ACTIVE_NOTE_CAPACITY; ++i) {
        if (state->midi_editor_ui.qwerty_active_notes[i].active) {
            (void)midi_editor_complete_qwerty_note(state, i);
        }
    }
}

bool midi_editor_begin_qwerty_note(AppState* state,
                                          const MidiEditorSelection* selection,
                                          SDL_Keycode key,
                                          uint8_t note,
                                          bool record_on_release) {
    if (!state || !selection || !selection->clip) {
        return false;
    }
    if (midi_editor_find_active_qwerty_note(state, key) >= 0) {
        return true;
    }
    uint64_t start_frame = 0;
    if (record_on_release &&
        !midi_editor_transport_relative_frame(state, selection->clip, false, &start_frame)) {
        return false;
    }
    if (record_on_release) {
        start_frame = midi_editor_snap_relative_frame(state, selection->clip, start_frame);
    }
    if (record_on_release && start_frame >= selection->clip->duration_frames) {
        return false;
    }

    int slot = midi_editor_find_free_qwerty_note_slot(state);
    if (slot < 0) {
        return false;
    }
    EngineInstrumentPresetId preset = engine_clip_midi_effective_instrument_preset(state->engine,
                                                                                   selection->track_index,
                                                                                   selection->clip_index);
    EngineInstrumentParams params = engine_clip_midi_effective_instrument_params(state->engine,
                                                                                 selection->track_index,
                                                                                 selection->clip_index);
    float velocity = midi_editor_current_default_velocity(state);
    (void)engine_midi_audition_note_on(state->engine, selection->track_index, preset, params, note, velocity);
    state->midi_editor_ui.qwerty_active_notes[slot] = (MidiEditorQwertyActiveNote){
        .active = true,
        .key = key,
        .note = note,
        .velocity = velocity,
        .record_on_release = record_on_release,
        .track_index = selection->track_index,
        .clip_index = selection->clip_index,
        .clip_creation_index = selection->clip->creation_index,
        .start_frame = start_frame
    };
    state->midi_editor_ui.selected_track_index = selection->track_index;
    state->midi_editor_ui.selected_clip_index = selection->clip_index;
    state->midi_editor_ui.selected_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.selected_note_index = -1;
    return true;
}

bool midi_editor_toggle_qwerty_record(AppState* state) {
    if (!state) {
        return false;
    }
    if (state->midi_editor_ui.qwerty_record_armed) {
        midi_editor_complete_all_qwerty_notes(state);
        state->midi_editor_ui.qwerty_record_armed = false;
        midi_editor_clear_qwerty_active_notes(state);
    } else {
        state->midi_editor_ui.qwerty_record_armed = true;
        midi_editor_clear_qwerty_active_notes(state);
    }
    return true;
}

bool midi_editor_toggle_qwerty_test(AppState* state) {
    if (!state) {
        return false;
    }
    if (state->midi_editor_ui.qwerty_test_enabled) {
        state->midi_editor_ui.qwerty_test_enabled = false;
        midi_editor_clear_qwerty_active_notes(state);
    } else {
        state->midi_editor_ui.qwerty_test_enabled = true;
        midi_editor_clear_qwerty_active_notes(state);
    }
    state->midi_editor_ui.instrument_menu_open = false;
    return true;
}

bool midi_editor_adjust_octave(AppState* state, int delta) {
    if (!state || delta == 0) {
        return false;
    }
    state->midi_editor_ui.qwerty_octave_offset =
        midi_editor_clamp_octave_offset(state->midi_editor_ui.qwerty_octave_offset + delta);
    midi_editor_clear_qwerty_active_notes(state);
    return true;
}

bool midi_editor_adjust_default_velocity(AppState* state, float delta) {
    if (!state || delta == 0.0f) {
        return false;
    }
    midi_editor_set_default_velocity(state, midi_editor_current_default_velocity(state) + delta);
    return true;
}

bool midi_editor_handle_qwerty_event(AppState* state, const SDL_Event* event) {
    if (!state || !event || (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP)) {
        return false;
    }
    SDL_Keymod mods = SDL_GetModState();
    bool ctrl_or_cmd = (mods & (KMOD_CTRL | KMOD_GUI)) != 0;
    bool alt_held = (mods & KMOD_ALT) != 0;
    if (ctrl_or_cmd || alt_held) {
        return false;
    }

    SDL_Keycode key = event->key.keysym.sym;
    if (event->type == SDL_KEYDOWN && key == SDLK_r && event->key.repeat == 0) {
        state->midi_editor_ui.instrument_menu_open = false;
        return midi_editor_toggle_qwerty_record(state);
    }
    if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
        if (key == SDLK_LEFTBRACKET) {
            return midi_editor_adjust_octave(state, -1);
        }
        if (key == SDLK_RIGHTBRACKET) {
            return midi_editor_adjust_octave(state, 1);
        }
        if (key == SDLK_MINUS) {
            return midi_editor_adjust_default_velocity(state, -0.05f);
        }
        if (key == SDLK_EQUALS) {
            return midi_editor_adjust_default_velocity(state, 0.05f);
        }
        if (key == SDLK_q) {
            return midi_editor_quantize_selected_note(state);
        }
    }

    uint8_t base_note = 0;
    if (!midi_editor_qwerty_note_for_key(key, &base_note)) {
        return false;
    }
    uint8_t note = midi_editor_apply_octave_offset((int)base_note, state->midi_editor_ui.qwerty_octave_offset);
    bool record_on_release = state->midi_editor_ui.qwerty_record_armed;
    bool test_only = state->midi_editor_ui.qwerty_test_enabled && !record_on_release;
    if (!record_on_release && !test_only) {
        return false;
    }
    if (event->type == SDL_KEYDOWN) {
        if (event->key.repeat != 0) {
            return true;
        }
        MidiEditorSelection selection = {0};
        if (!midi_editor_get_fresh_selection(state, &selection, NULL)) {
            return true;
        }
        (void)midi_editor_begin_qwerty_note(state, &selection, key, note, record_on_release);
        return true;
    }

    int active_index = midi_editor_find_active_qwerty_note(state, key);
    if (active_index >= 0) {
        (void)midi_editor_complete_qwerty_note(state, active_index);
    }
    return true;
}

bool midi_editor_input_qwerty_capturing(const AppState* state) {
    return state &&
           state->midi_editor_ui.panel_mode != MIDI_REGION_PANEL_INSTRUMENT &&
           (state->midi_editor_ui.qwerty_record_armed ||
                     state->midi_editor_ui.qwerty_test_enabled);
}
