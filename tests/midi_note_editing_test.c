#include "test_midi_editor_harness.h"
#include "input/midi_editor_input_internal.h"
#include <math.h>
#include <stdio.h>

#define expect(c, m) daw_test_expect("midi_note_editing_test", (c), (m))

// Routes a real MIDI pointer event through the note editor owner.
static void pointer(AppState* s, Uint32 type, int x, int y) {
    InputManager manager = {0};
    if (type == SDL_MOUSEMOTION) daw_midi_editor_test_dispatch_mouse_motion("midi_note_editing_test", &manager, s, x, y);
    else daw_midi_editor_test_dispatch_mouse_button("midi_note_editing_test", &manager, s, type, x, y);
}

// Seeds an off-grid three-note region with two selected notes and one untouched neighbor.
static void fixture(AppState* s, EngineRuntimeConfig* cfg) {
    daw_midi_editor_test_state_init("midi_note_editing_test", s, cfg);
    int clip = -1;
    expect(engine_add_midi_clip_to_track(s->engine, 0, 0, 96000, &clip), "create region");
    EngineMidiNote notes[] = {{7000, 12000, 60, .4f}, {30000, 12000, 64, .98f}, {70000, 4000, 67, .2f}};
    expect(engine_clip_midi_set_notes(s->engine, 0, clip, notes, 3), "seed region");
    s->selected_track_index = 0;
    s->selected_clip_index = clip;
    s->selection_count = 1;
    s->selection[0].track_index = 0;
    s->selection[0].clip_index = clip;
    MidiEditorSelection selection = {0};
    expect(midi_editor_get_selection(s, &selection), "region selection");
    midi_editor_select_note(s, &selection, 0);
    s->midi_editor_ui.selected_note_indices[1] = true;
    s->timeline_snap_enabled = true;
}

// Returns freshly published notes after a command or undo.
static const EngineMidiNote* notes(AppState* s) {
    return engine_clip_midi_notes(&engine_get_tracks(s->engine)[0].clips[s->selected_clip_index]);
}

// Checks selected-only button edits, saturation, keyboard parity, history and insertion fallback.
static void test_velocity(void) {
    AppState s; EngineRuntimeConfig cfg; fixture(&s, &cfg);
    MidiEditorLayout l = {0}; midi_editor_compute_layout(&s, &l);
    int x = l.velocity_up_button_rect.x + l.velocity_up_button_rect.w / 2;
    int y = l.velocity_up_button_rect.y + l.velocity_up_button_rect.h / 2;
    InputManager manager = {0};
    input_manager_init(&manager);
    s.mouse_x = x; s.mouse_y = y;
    SDL_Event event = {0}; event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT; event.button.x = x; event.button.y = y;
    input_manager_handle_event(&manager, &s, &event);
    expect(notes(&s)[0].velocity == .4f, "velocity button activated before matched release");
    event.type = SDL_MOUSEBUTTONUP;
    input_manager_handle_event(&manager, &s, &event);
    expect(fabsf(notes(&s)[0].velocity - .45f) < .00001f && notes(&s)[1].velocity == 1.f, "selected velocity increment and clamp");
    expect(notes(&s)[2].velocity == .2f, "unselected velocity changed");
    expect(s.undo.undo_count == 1, "velocity command needs exactly one undo entry");
    expect(undo_manager_undo(&s.undo, &s), "velocity undo");
    expect(fabsf(notes(&s)[0].velocity - .4f) < .00001f && fabsf(notes(&s)[1].velocity - .98f) < .00001f, "velocity undo values");
    expect(undo_manager_redo(&s.undo, &s), "velocity redo");
    InputManager m = {0};
    daw_midi_editor_test_dispatch_key("midi_note_editing_test", &m, &s, SDLK_MINUS);
    expect(fabsf(notes(&s)[0].velocity - .4f) < .00001f && fabsf(notes(&s)[1].velocity - .95f) < .00001f, "minus keyboard parity");
    expect(s.midi_editor_ui.selected_note_indices[0] && s.midi_editor_ui.selected_note_indices[1], "velocity lost selection");
    midi_editor_clear_note_selection(&s);
    float before = midi_editor_current_default_velocity(&s);
    expect(midi_editor_adjust_velocity(&s, -.05f), "default velocity fallback");
    expect(fabsf(midi_editor_current_default_velocity(&s) - (before - .05f)) < .00001f, "no-selection default velocity");
    daw_midi_editor_test_state_destroy(&s);
}

// Checks grab-offset movement, live snap bypass and returning a selected group to its baseline.
static void test_movement(void) {
    AppState s; EngineRuntimeConfig cfg; fixture(&s, &cfg);
    MidiEditorLayout l = {0}; midi_editor_compute_layout(&s, &l);
    const EngineClip* c = &engine_get_tracks(s.engine)[0].clips[s.selected_clip_index];
    SDL_Rect rect = {0}; expect(midi_editor_note_rect(&l, &notes(&s)[0], c->duration_frames, &rect), "move rect");
    int x = rect.x + rect.w / 2, y = rect.y + rect.h / 2;
    pointer(&s, SDL_MOUSEBUTTONDOWN, x, y);
    SDL_SetModState(KMOD_ALT);
    pointer(&s, SDL_MOUSEMOTION, x + 10, y);
    uint64_t press = 0, moved = 0;
    midi_editor_point_to_frame(&l, 96000, x, &press);
    midi_editor_point_to_frame(&l, 96000, x + 10, &moved);
    expect(notes(&s)[0].start_frame == 7000 + moved - press, "free move lost grab offset");
    expect(notes(&s)[1].start_frame - notes(&s)[0].start_frame == 23000, "group spacing changed");
    expect(notes(&s)[2].start_frame == 70000, "group moved unselected note");
    pointer(&s, SDL_MOUSEMOTION, x, y);
    expect(notes(&s)[0].start_frame == 7000 && notes(&s)[1].start_frame == 30000, "group failed to return to baseline");
    SDL_SetModState(KMOD_NONE);
    pointer(&s, SDL_MOUSEMOTION, x + 10, y);
    expect(notes(&s)[0].start_frame == midi_editor_snap_relative_frame(&s, c, 7000 + moved - press), "movement grid alignment");
    pointer(&s, SDL_MOUSEBUTTONUP, x + 10, y);
    expect(undo_manager_undo(&s.undo, &s), "group movement undo");
    expect(notes(&s)[0].start_frame == 7000 && notes(&s)[1].start_frame == 30000, "group movement undo values");
    daw_midi_editor_test_state_destroy(&s);
}

// Checks distinct short-note hit zones, hover ownership and relative left-edge resizing.
static void test_edges(void) {
    AppState s; EngineRuntimeConfig cfg; fixture(&s, &cfg);
    MidiEditorSelection selection = {0}; MidiEditorLayout l = {0};
    midi_editor_get_selection(&s, &selection); midi_editor_compute_layout(&s, &l);
    SDL_Rect rect = {0}; midi_editor_note_rect(&l, &notes(&s)[0], 96000, &rect);
    int y = rect.y + rect.h / 2;
    MidiEditorNoteHit hit = {0};
    expect(midi_editor_hit_test_note(&l, selection.clip, rect.x, y, &hit) && hit.part == MIDI_EDITOR_NOTE_HIT_LEFT_EDGE, "left hit");
    expect(midi_editor_hit_test_note(&l, selection.clip, rect.x + rect.w / 2, y, &hit) && hit.part == MIDI_EDITOR_NOTE_HIT_BODY, "body hit");
    expect(midi_editor_hit_test_note(&l, selection.clip, rect.x + rect.w - 1, y, &hit) && hit.part == MIDI_EDITOR_NOTE_HIT_RIGHT_EDGE, "right hit");
    pointer(&s, SDL_MOUSEMOTION, rect.x, y);
    expect(s.midi_editor_ui.hover_note_part == MIDI_EDITOR_NOTE_HIT_LEFT_EDGE, "hover resize target");
    SDL_SetModState(KMOD_ALT);
    pointer(&s, SDL_MOUSEBUTTONDOWN, rect.x + 2, y);
    pointer(&s, SDL_MOUSEMOTION, rect.x + 12, y);
    expect(notes(&s)[0].start_frame > 7000 && notes(&s)[0].start_frame + notes(&s)[0].duration_frames == 19000, "left resize preserves end");
    pointer(&s, SDL_MOUSEBUTTONUP, rect.x + 12, y);
    SDL_SetModState(KMOD_NONE);
    expect(undo_manager_undo(&s.undo, &s), "left resize undo");
    expect(notes(&s)[0].start_frame == 7000 && notes(&s)[0].duration_frames == 12000, "left resize undo values");
    EngineMidiNote short_note = {7000, 600, 60, .4f};
    expect(engine_clip_midi_set_notes(s.engine, 0, s.selected_clip_index, &short_note, 1), "short note");
    midi_editor_get_selection(&s, &selection);
    midi_editor_note_rect(&l, &notes(&s)[0], 96000, &rect);
    expect(midi_editor_hit_test_note(&l, selection.clip, rect.x + rect.w / 2, y, &hit) && hit.part == MIDI_EDITOR_NOTE_HIT_BODY, "short note has no move target");
    daw_midi_editor_test_state_destroy(&s);
}

// Runs the dedicated MIDI note interaction regression suite.
int main(void) {
    SDL_SetModState(KMOD_NONE);
    test_velocity(); test_movement(); test_edges();
    puts("midi_note_editing_test: success");
    return 0;
}
