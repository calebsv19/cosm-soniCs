#include "app_state.h"
#include "config.h"
#include "engine/engine.h"
#include "input/input_manager.h"
#include "input/midi_editor_input.h"
#include "input/midi_instrument_panel_input.h"
#include "time/tempo.h"
#include "ui/effects_panel.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/midi_editor.h"
#include "ui/midi_instrument_panel.h"
#include "undo/undo_manager.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char* message) {
    fprintf(stderr, "midi_editor_shell_test: %s\n", message);
    exit(1);
}

static void expect(int condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

static bool rect_has_positive_size(const SDL_Rect* rect) {
    return rect && rect->w > 0 && rect->h > 0;
}

static bool rect_contains_rect(const SDL_Rect* outer, const SDL_Rect* inner) {
    if (!outer || !inner) {
        return false;
    }
    return inner->x >= outer->x &&
           inner->y >= outer->y &&
           inner->x + inner->w <= outer->x + outer->w &&
           inner->y + inner->h <= outer->y + outer->h;
}

static bool rects_overlap_strict(const SDL_Rect* a, const SDL_Rect* b) {
    if (!rect_has_positive_size(a) || !rect_has_positive_size(b)) {
        return false;
    }
    return a->x < b->x + b->w &&
           a->x + a->w > b->x &&
           a->y < b->y + b->h &&
           a->y + a->h > b->y;
}

static void state_init(AppState* state, EngineRuntimeConfig* cfg) {
    memset(state, 0, sizeof(*state));
    config_set_defaults(cfg);
    cfg->sample_rate = 48000;
    state->runtime_cfg = *cfg;
    state->engine = engine_create(cfg);
    expect(state->engine != NULL, "engine_create failed");
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

static void state_destroy(AppState* state) {
    undo_manager_free(&state->undo);
    time_signature_map_free(&state->time_signature_map);
    tempo_map_free(&state->tempo_map);
    engine_destroy(state->engine);
    state->engine = NULL;
}

static void validate_layout(const AppState* state, const MidiEditorLayout* layout) {
    const Pane* pane = ui_layout_get_pane(state, 2);
    expect(pane != NULL, "lower pane missing");
    expect(layout->panel_rect.x == pane->rect.x &&
               layout->panel_rect.y == pane->rect.y &&
               layout->panel_rect.w == pane->rect.w &&
               layout->panel_rect.h == pane->rect.h,
           "midi editor panel rect mismatch");
    expect(rect_has_positive_size(&layout->header_rect), "header rect invalid");
    expect(layout->header_rect.h <= 30, "MIDI editor header should stay compact");
    expect(rect_has_positive_size(&layout->title_rect), "title rect invalid");
    expect(rect_has_positive_size(&layout->instrument_button_rect), "instrument button rect invalid");
    expect(rect_has_positive_size(&layout->instrument_panel_button_rect), "instrument panel button rect invalid");
    expect(rect_has_positive_size(&layout->test_button_rect), "test button rect invalid");
    expect(layout->test_button_rect.h <= 24, "MIDI editor buttons should stay compact");
    expect(rect_has_positive_size(&layout->quantize_button_rect), "quantize button rect invalid");
    expect(rect_has_positive_size(&layout->quantize_down_button_rect), "quantize down rect invalid");
    expect(rect_has_positive_size(&layout->quantize_up_button_rect), "quantize up rect invalid");
    expect(rect_has_positive_size(&layout->octave_down_button_rect), "octave down rect invalid");
    expect(rect_has_positive_size(&layout->octave_up_button_rect), "octave up rect invalid");
    expect(rect_has_positive_size(&layout->velocity_down_button_rect), "velocity down rect invalid");
    expect(rect_has_positive_size(&layout->velocity_up_button_rect), "velocity up rect invalid");
    expect(rect_has_positive_size(&layout->summary_rect), "summary rect invalid");
    expect(layout->instrument_param_count == 0, "MIDI editor should not own instrument params");
    expect(!rect_has_positive_size(&layout->param_strip_rect), "MIDI editor param strip should be external");
    expect(rect_has_positive_size(&layout->body_rect), "body rect invalid");
    expect(rect_has_positive_size(&layout->time_ruler_rect), "time ruler rect invalid");
    expect(rect_has_positive_size(&layout->piano_rect), "piano rect invalid");
    expect(rect_has_positive_size(&layout->grid_rect), "grid rect invalid");
    expect(rect_contains_rect(&layout->panel_rect, &layout->header_rect), "header outside panel");
    expect(rect_contains_rect(&layout->header_rect, &layout->instrument_button_rect), "instrument button outside header");
    expect(rect_contains_rect(&layout->header_rect, &layout->instrument_panel_button_rect),
           "instrument panel button outside header");
    expect(rect_contains_rect(&layout->header_rect, &layout->test_button_rect), "test button outside header");
    expect(rect_contains_rect(&layout->header_rect, &layout->quantize_button_rect), "quantize button outside header");
    expect(rect_contains_rect(&layout->header_rect, &layout->quantize_down_button_rect),
           "quantize down outside header");
    expect(rect_contains_rect(&layout->header_rect, &layout->quantize_up_button_rect),
           "quantize up outside header");
    expect(rect_contains_rect(&layout->header_rect, &layout->octave_down_button_rect),
           "octave down outside header");
    expect(rect_contains_rect(&layout->header_rect, &layout->octave_up_button_rect),
           "octave up outside header");
    expect(rect_contains_rect(&layout->header_rect, &layout->velocity_down_button_rect),
           "velocity down outside header");
    expect(rect_contains_rect(&layout->header_rect, &layout->velocity_up_button_rect),
           "velocity up outside header");
    expect(rect_contains_rect(&layout->panel_rect, &layout->body_rect), "body outside panel");
    expect(rect_contains_rect(&layout->body_rect, &layout->time_ruler_rect), "time ruler outside body");
    expect(rect_contains_rect(&layout->body_rect, &layout->piano_rect), "piano outside body");
    expect(rect_contains_rect(&layout->body_rect, &layout->grid_rect), "grid outside body");
    expect(!rects_overlap_strict(&layout->title_rect, &layout->summary_rect), "title overlaps summary");
    expect(!rects_overlap_strict(&layout->summary_rect, &layout->instrument_button_rect),
           "summary overlaps instrument button");
    expect(!rects_overlap_strict(&layout->instrument_button_rect, &layout->test_button_rect),
           "instrument button overlaps test button");
    expect(!rects_overlap_strict(&layout->instrument_button_rect, &layout->instrument_panel_button_rect),
           "instrument button overlaps panel button");
    expect(!rects_overlap_strict(&layout->instrument_panel_button_rect, &layout->velocity_down_button_rect),
           "instrument panel button overlaps velocity down");
    expect(!rects_overlap_strict(&layout->instrument_button_rect, &layout->velocity_down_button_rect),
           "instrument button overlaps velocity down");
    expect(!rects_overlap_strict(&layout->velocity_down_button_rect, &layout->velocity_up_button_rect),
           "velocity buttons overlap");
    expect(!rects_overlap_strict(&layout->velocity_up_button_rect, &layout->octave_down_button_rect),
           "velocity up overlaps octave down");
    expect(!rects_overlap_strict(&layout->octave_down_button_rect, &layout->octave_up_button_rect),
           "octave buttons overlap");
    expect(!rects_overlap_strict(&layout->octave_up_button_rect, &layout->quantize_button_rect),
           "octave up overlaps quantize button");
    expect(!rects_overlap_strict(&layout->quantize_button_rect, &layout->quantize_down_button_rect),
           "quantize button overlaps quantize down");
    expect(!rects_overlap_strict(&layout->quantize_down_button_rect, &layout->quantize_up_button_rect),
           "quantize controls overlap");
    expect(!rects_overlap_strict(&layout->quantize_up_button_rect, &layout->test_button_rect),
           "quantize up overlaps test button");
    expect(!rects_overlap_strict(&layout->time_ruler_rect, &layout->grid_rect), "time ruler overlaps grid");
    expect(!rects_overlap_strict(&layout->time_ruler_rect, &layout->piano_rect), "time ruler overlaps piano");
    expect(!rects_overlap_strict(&layout->piano_rect, &layout->grid_rect), "piano overlaps grid");
    expect(layout->view_end_frame > layout->view_start_frame, "view frame range invalid");
    expect(layout->view_span_frames == layout->view_end_frame - layout->view_start_frame,
           "view span mismatch");
    expect(layout->key_row_count > 0, "key rows missing");
    expect(layout->highest_note >= layout->lowest_note, "note range inverted");
    for (int i = 0; i < layout->key_row_count; ++i) {
        expect(rect_has_positive_size(&layout->key_label_rects[i]), "key label rect invalid");
        expect(rect_has_positive_size(&layout->key_lane_rects[i]), "key lane rect invalid");
        expect(rect_contains_rect(&layout->piano_rect, &layout->key_label_rects[i]), "key label outside piano");
        expect(rect_contains_rect(&layout->grid_rect, &layout->key_lane_rects[i]), "key lane outside grid");
        if (i > 0) {
            expect(layout->key_label_rects[i].y >= layout->key_label_rects[i - 1].y + layout->key_label_rects[i - 1].h,
                   "key label rows overlap");
            expect(layout->key_lane_rects[i].y >= layout->key_lane_rects[i - 1].y + layout->key_lane_rects[i - 1].h,
                   "key lane rows overlap");
        }
    }
}

static void validate_instrument_panel_layout(const AppState* state, const MidiInstrumentPanelLayout* layout) {
    const Pane* pane = ui_layout_get_pane(state, 2);
    expect(pane != NULL, "lower pane missing");
    expect(layout->panel_rect.x == pane->rect.x &&
               layout->panel_rect.y == pane->rect.y &&
               layout->panel_rect.w == pane->rect.w &&
               layout->panel_rect.h == pane->rect.h,
           "instrument panel rect mismatch");
    expect(rect_has_positive_size(&layout->header_rect), "instrument header rect invalid");
    expect(layout->header_rect.h <= 32, "instrument panel header should stay compact");
    expect(rect_has_positive_size(&layout->title_rect), "instrument title rect invalid");
    expect(rect_has_positive_size(&layout->notes_button_rect), "instrument notes button invalid");
    expect(rect_has_positive_size(&layout->preset_button_rect), "instrument preset button invalid");
    expect(rect_has_positive_size(&layout->summary_rect), "instrument summary rect invalid");
    expect(rect_has_positive_size(&layout->param_grid_rect), "instrument param grid invalid");
    expect(rect_has_positive_size(&layout->scope_rect), "instrument preview rect invalid");
    expect(layout->param_grid_rect.h <= 90, "instrument params should stay compact");
    if (layout->panel_rect.h >= 220) {
        expect(layout->scope_rect.h > layout->param_grid_rect.h,
               "instrument preview should get primary space when pane is tall");
    } else {
        expect(layout->scope_rect.h >= 40, "instrument preview should remain usable in compact panes");
    }
    expect(layout->group_tab_count == ENGINE_INSTRUMENT_PARAM_GROUP_COUNT,
           "instrument panel group tabs missing");
    expect(layout->active_group >= 0 && layout->active_group < ENGINE_INSTRUMENT_PARAM_GROUP_COUNT,
           "instrument panel active group invalid");
    expect(layout->instrument_param_count > 0 &&
               layout->instrument_param_count <= ENGINE_INSTRUMENT_PARAM_COUNT,
           "instrument panel params missing");
    expect(rect_contains_rect(&layout->panel_rect, &layout->header_rect), "instrument header outside panel");
    expect(rect_contains_rect(&layout->header_rect, &layout->notes_button_rect),
           "instrument notes button outside header");
    expect(rect_contains_rect(&layout->header_rect, &layout->preset_button_rect),
           "instrument preset button outside header");
    expect(rect_contains_rect(&layout->panel_rect, &layout->param_grid_rect),
           "instrument param grid outside panel");
    expect(rect_contains_rect(&layout->panel_rect, &layout->scope_rect),
           "instrument preview outside panel");
    expect(!rects_overlap_strict(&layout->title_rect, &layout->summary_rect),
           "instrument title overlaps summary");
    expect(!rects_overlap_strict(&layout->summary_rect, &layout->preset_button_rect),
           "instrument summary overlaps preset");
    expect(!rects_overlap_strict(&layout->preset_button_rect, &layout->notes_button_rect),
           "instrument preset overlaps notes");
    expect(!rects_overlap_strict(&layout->param_grid_rect, &layout->scope_rect),
           "instrument params overlap preview");
    for (int i = 0; i < layout->group_tab_count; ++i) {
        expect(rect_has_positive_size(&layout->group_tab_rects[i]), "instrument group tab invalid");
        expect(rect_contains_rect(&layout->panel_rect, &layout->group_tab_rects[i]),
               "instrument group tab outside panel");
    }
    for (int i = 0; i < layout->instrument_param_count; ++i) {
        EngineInstrumentParamSpec spec = {0};
        expect(engine_instrument_param_spec(layout->param_ids[i], &spec),
               "instrument panel param id should have spec");
        expect(spec.group == layout->active_group, "instrument panel param should match active group");
        expect(rect_has_positive_size(&layout->param_widget_rects[i]), "instrument param widget invalid");
        expect(rect_has_positive_size(&layout->param_knob_rects[i]), "instrument param knob invalid");
        expect(rect_contains_rect(&layout->param_grid_rect, &layout->param_widget_rects[i]),
               "instrument param widget outside grid");
        expect(rect_contains_rect(&layout->param_widget_rects[i], &layout->param_knob_rects[i]),
               "instrument param knob outside widget");
        expect(rect_contains_rect(&layout->param_widget_rects[i], &layout->param_label_rects[i]),
               "instrument param label outside widget");
        expect(rect_contains_rect(&layout->param_widget_rects[i], &layout->param_value_rects[i]),
               "instrument param value outside widget");
    }
}

static int find_midi_note_index_by_pitch(const EngineClip* clip, uint8_t pitch) {
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    int note_count = engine_clip_midi_note_count(clip);
    for (int i = 0; notes && i < note_count; ++i) {
        if (notes[i].note == pitch) {
            return i;
        }
    }
    return -1;
}

static int midi_editor_note_center_y_for_test(const MidiEditorLayout* layout, int note) {
    int row = -1;
    expect(midi_editor_pitch_note_to_row(layout, note, &row), "test note should be inside visible pitch range");
    return layout->key_lane_rects[row].y + layout->key_lane_rects[row].h / 2;
}

static void set_midi_editor_note_selection_for_test(AppState* state,
                                                    const MidiEditorSelection* selection,
                                                    int primary_note_index,
                                                    const int* selected_note_indices,
                                                    int selected_note_count) {
    expect(state && selection && selection->clip, "test note selection requires a selected MIDI clip");
    state->midi_editor_ui.selected_track_index = selection->track_index;
    state->midi_editor_ui.selected_clip_index = selection->clip_index;
    state->midi_editor_ui.selected_clip_creation_index = selection->clip->creation_index;
    state->midi_editor_ui.selected_note_index = primary_note_index;
    memset(state->midi_editor_ui.selected_note_indices, 0, sizeof(state->midi_editor_ui.selected_note_indices));
    for (int i = 0; selected_note_indices && i < selected_note_count; ++i) {
        int note_index = selected_note_indices[i];
        expect(note_index >= 0 && note_index < ENGINE_MIDI_NOTE_CAP, "test selected note index out of range");
        state->midi_editor_ui.selected_note_indices[note_index] = true;
    }
}

static void test_midi_selection_routes_editor_shell(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    expect(!midi_editor_should_render(&state), "empty selection should not render MIDI editor");

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine,
                                         0,
                                         (uint64_t)cfg.sample_rate,
                                         (uint64_t)cfg.sample_rate * 2u,
                                         &clip_index),
           "failed to create MIDI clip");
    expect(clip_index >= 0, "invalid MIDI clip index");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){0, (uint64_t)cfg.sample_rate / 2u, 60, 1.0f},
                                     NULL),
           "failed to add MIDI note");

    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.inspector.visible = true;

    MidiEditorSelection selection = {0};
    expect(midi_editor_should_render(&state), "MIDI selection should render editor");
    expect(midi_editor_get_selection(&state, &selection), "MIDI selection lookup failed");
    expect(selection.track_index == 0, "selection track mismatch");
    expect(selection.clip_index == clip_index, "selection clip mismatch");
    expect(selection.clip != NULL && engine_clip_get_kind(selection.clip) == ENGINE_CLIP_KIND_MIDI,
           "selection clip kind mismatch");

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);

    InputManager manager = {0};
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = layout.grid_rect.x + layout.grid_rect.w / 2;
    event.button.y = layout.grid_rect.y + layout.grid_rect.h / 2;
    expect(midi_editor_input_handle_event(&manager, &state, &event),
           "MIDI editor should capture lower-pane mouse input");

    event.button.x = 1;
    event.button.y = 1;
    expect(!midi_editor_input_handle_event(&manager, &state, &event),
           "MIDI editor should not capture outside lower pane");

    state_destroy(&state);
}

static void dispatch_mouse_button(InputManager* manager,
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
    expect(midi_editor_input_handle_event(manager, state, &event), "MIDI editor mouse event not consumed");
}

static void dispatch_mouse_motion(InputManager* manager, AppState* state, int x, int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.x = x;
    event.motion.y = y;
    expect(midi_editor_input_handle_event(manager, state, &event), "MIDI editor motion event not consumed");
}

static void dispatch_mouse_wheel(InputManager* manager, AppState* state, int x, int y, int wheel_y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    state->mouse_x = x;
    state->mouse_y = y;
    event.type = SDL_MOUSEWHEEL;
    event.wheel.y = wheel_y;
    expect(midi_editor_input_handle_event(manager, state, &event), "MIDI editor wheel event not consumed");
}

static void dispatch_instrument_mouse_button(InputManager* manager,
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
    expect(midi_instrument_panel_input_handle_event(manager, state, &event),
           "MIDI instrument panel mouse event not consumed");
}

static void dispatch_instrument_mouse_motion(InputManager* manager, AppState* state, int x, int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.x = x;
    event.motion.y = y;
    expect(midi_instrument_panel_input_handle_event(manager, state, &event),
           "MIDI instrument panel motion event not consumed");
}

static void dispatch_instrument_mouse_wheel(InputManager* manager, AppState* state, int x, int y, int wheel_y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    state->mouse_x = x;
    state->mouse_y = y;
    event.type = SDL_MOUSEWHEEL;
    event.wheel.y = wheel_y;
    expect(midi_instrument_panel_input_handle_event(manager, state, &event),
           "MIDI instrument panel wheel event not consumed");
}

static void dispatch_key(InputManager* manager, AppState* state, SDL_Keycode key) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    expect(midi_editor_input_handle_event(manager, state, &event), "MIDI editor key event not consumed");
}

static void dispatch_key_event(InputManager* manager, AppState* state, Uint32 type, SDL_Keycode key) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key.keysym.sym = key;
    event.key.repeat = 0;
    expect(midi_editor_input_handle_event(manager, state, &event), "MIDI editor key event not consumed");
}

static void dispatch_command_key(InputManager* manager, AppState* state, SDL_Keycode key) {
    SDL_Keymod old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | KMOD_CTRL));
    dispatch_key(manager, state, key);
    SDL_SetModState(old_mods);
}

static void dispatch_key_with_mod(InputManager* manager, AppState* state, SDL_Keycode key, SDL_Keymod mod) {
    SDL_Keymod old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | mod));
    dispatch_key(manager, state, key);
    SDL_SetModState(old_mods);
}

static bool handle_key_with_mod(InputManager* manager, AppState* state, SDL_Keycode key, SDL_Keymod mod) {
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

static void dispatch_wheel_with_mod(InputManager* manager,
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
    expect(midi_editor_input_handle_event(manager, state, &event), "MIDI editor wheel event not consumed");
    SDL_SetModState(old_mods);
}

static void test_midi_editor_create_delete_and_undo(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for edit test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.midi_editor_ui.selected_note_index = -1;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);

    InputManager manager = {0};
    int x = layout.grid_rect.x + layout.grid_rect.w / 4;
    int y = layout.key_lane_rects[layout.key_row_count / 2].y + layout.key_lane_rects[layout.key_row_count / 2].h / 2;
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, x, y);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, x, y);

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_note_count(clip) == 1, "click should create one MIDI note");
    expect(state.midi_editor_ui.selected_note_index == 0, "created note should be selected");
    expect(undo_manager_can_undo(&state.undo), "note create should push undo");

    expect(undo_manager_undo(&state.undo, &state), "note create undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[state.selected_clip_index];
    expect(engine_clip_midi_note_count(clip) == 0, "undo should remove created note");
    expect(undo_manager_redo(&state.undo, &state), "note create redo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[state.selected_clip_index];
    expect(engine_clip_midi_note_count(clip) == 1, "redo should restore created note");

    midi_editor_compute_layout(&state, &layout);
    SDL_Rect note_rect = {0, 0, 0, 0};
    expect(midi_editor_note_rect(&layout,
                                 &engine_clip_midi_notes(clip)[0],
                                 clip->duration_frames,
                                 &note_rect),
           "restored note should produce a note rect");
    int slop_x = note_rect.x > layout.grid_rect.x + 3 ? note_rect.x - 2 : note_rect.x + 2;
    int slop_y = note_rect.y + note_rect.h / 2;
    state.midi_editor_ui.selected_note_index = -1;
    dispatch_mouse_motion(&manager, &state, slop_x, slop_y);
    expect(state.midi_editor_ui.hover_note_valid, "hover slop should mark a note as hovered");
    expect(state.midi_editor_ui.hover_note_index == 0, "hover slop should target restored note");
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, slop_x, slop_y);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, slop_x, slop_y);
    expect(state.midi_editor_ui.selected_note_index == 0, "click slop should select restored note");

    state.midi_editor_ui.selected_note_index = 0;
    state.midi_editor_ui.selected_track_index = 0;
    state.midi_editor_ui.selected_clip_index = state.selected_clip_index;
    state.midi_editor_ui.selected_clip_creation_index = clip->creation_index;
    dispatch_key(&manager, &state, SDLK_DELETE);
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[state.selected_clip_index];
    expect(engine_clip_midi_note_count(clip) == 0, "delete should remove selected MIDI note");
    expect(!state.midi_editor_ui.hover_note_valid, "delete should clear hovered MIDI note");
    expect(undo_manager_undo(&state.undo, &state), "note delete undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[state.selected_clip_index];
    expect(engine_clip_midi_note_count(clip) == 1, "undo should restore deleted note");

    state_destroy(&state);
}

static void test_midi_editor_time_ruler_click_seeks_transport(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t clip_start = (uint64_t)cfg.sample_rate;
    uint64_t clip_duration = (uint64_t)cfg.sample_rate * 4u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, clip_start, clip_duration, &clip_index),
           "failed to create MIDI clip for ruler seek test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);

    InputManager manager = {0};
    int seek_x = layout.time_ruler_rect.x + layout.time_ruler_rect.w / 2;
    int seek_y = layout.time_ruler_rect.y + layout.time_ruler_rect.h / 2;
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, seek_x, seek_y);

    uint64_t expected = clip_start + clip_duration / 2u;
    uint64_t actual = engine_get_transport_frame(state.engine);
    uint64_t tolerance = 2u;
    expect(actual >= expected - tolerance && actual <= expected + tolerance,
           "time ruler click should seek to matching MIDI editor frame");

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_note_count(clip) == 0, "time ruler seek should not create notes");

    state_destroy(&state);
}

static void test_midi_editor_viewport_zoom_pan_maps_notes_and_hit_tests(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t clip_duration = (uint64_t)cfg.sample_rate * 8u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, clip_duration, &clip_index),
           "failed to create MIDI clip for viewport test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){clip_duration / 2u,
                                                      (uint64_t)cfg.sample_rate / 2u,
                                                      60,
                                                      1.0f},
                                     NULL),
           "failed to seed viewport note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    uint64_t full_span = layout.view_span_frames;

    InputManager manager = {0};
    int anchor_x = layout.grid_rect.x + layout.grid_rect.w / 2;
    int anchor_y = layout.grid_rect.y + layout.grid_rect.h / 2;
    dispatch_wheel_with_mod(&manager, &state, anchor_x, anchor_y, 1, KMOD_ALT);

    midi_editor_compute_layout(&state, &layout);
    expect(layout.view_span_frames < full_span, "Alt-wheel over MIDI editor should zoom the MIDI viewport");
    expect(state.timeline_window_start_seconds == 0.0f,
           "MIDI editor viewport zoom should not pan the arrangement timeline");

    uint64_t zoom_start = layout.view_start_frame;
    dispatch_wheel_with_mod(&manager, &state, anchor_x, anchor_y, -1, KMOD_NONE);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.view_start_frame > zoom_start, "wheel over MIDI editor should pan the MIDI viewport");

    uint64_t visible_note_start = layout.view_start_frame + layout.view_span_frames / 2u;
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){visible_note_start,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      64,
                                                      0.75f},
                                     NULL),
           "failed to seed visible viewport note");
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    int visible_index = engine_clip_midi_note_count(clip) - 1;
    SDL_Rect note_rect = {0, 0, 0, 0};
    expect(midi_editor_note_rect(&layout, &engine_clip_midi_notes(clip)[visible_index], clip->duration_frames, &note_rect),
           "zoomed MIDI viewport should render visible note");
    uint64_t frame = 0;
    expect(midi_editor_point_to_frame(&layout, clip->duration_frames, note_rect.x, &frame),
           "zoomed MIDI viewport should map x to frame");
    expect(frame >= layout.view_start_frame && frame <= layout.view_end_frame,
           "zoomed x-to-frame mapping should stay inside visible range");

    state.mouse_x = anchor_x;
    state.mouse_y = anchor_y;
    dispatch_key_with_mod(&manager, &state, SDLK_0, KMOD_ALT);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.view_start_frame == 0 && layout.view_span_frames == clip_duration,
           "Alt-0 over MIDI editor should fit the selected region");

    state_destroy(&state);
}

static void test_midi_editor_pitch_viewport_state_is_clip_local(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int first_clip_index = -1;
    int second_clip_index = -1;
    uint64_t clip_duration = (uint64_t)cfg.sample_rate * 4u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, clip_duration, &first_clip_index),
           "failed to create first MIDI clip for pitch viewport test");
    expect(engine_add_midi_clip_to_track(state.engine, 0, clip_duration + (uint64_t)cfg.sample_rate, clip_duration, &second_clip_index),
           "failed to create second MIDI clip for pitch viewport test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     first_clip_index,
                                     (EngineMidiNote){0, (uint64_t)cfg.sample_rate / 2u, 96, 0.75f},
                                     NULL),
           "failed to seed high pitch viewport note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     first_clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate,
                                                      (uint64_t)cfg.sample_rate / 2u,
                                                      36,
                                                      0.75f},
                                     NULL),
           "failed to seed low pitch viewport note");

    state.selected_track_index = 0;
    state.selected_clip_index = first_clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = first_clip_index;

    MidiEditorSelection selection = {0};
    expect(midi_editor_get_selection(&state, &selection), "first MIDI clip selection missing");

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    int default_top_note = layout.key_row_count < 12 ? 60 + layout.key_row_count - 1 : 71;
    int default_low_note = default_top_note - layout.key_row_count + 1;
    expect(layout.highest_note == default_top_note && layout.lowest_note == default_low_note,
           "pitch viewport should preserve the default editor range before clip-local state");

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* first_clip = &tracks[0].clips[first_clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(first_clip);
    SDL_Rect note_rect = {0, 0, 0, 0};
    midi_editor_store_pitch_viewport(&state, &selection, 96, layout.key_row_count);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.highest_note == 96, "pitch viewport top note should be restored for the selected clip");
    expect(layout.lowest_note == 96 - layout.key_row_count + 1,
           "pitch viewport low note should follow stored row count");
    expect(midi_editor_note_rect(&layout, &notes[0], first_clip->duration_frames, &note_rect),
           "stored high pitch viewport should make the high note visible");
    expect(!midi_editor_note_rect(&layout, &notes[1], first_clip->duration_frames, &note_rect),
           "stored high pitch viewport should hide the low note");

    int low_note_top = 36 + layout.key_row_count - 1;
    midi_editor_store_pitch_viewport(&state, &selection, low_note_top, layout.key_row_count);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.highest_note == low_note_top && layout.lowest_note == 36,
           "pitch viewport should support lower manually stored note ranges");
    expect(midi_editor_note_rect(&layout, &notes[1], first_clip->duration_frames, &note_rect),
           "stored low pitch viewport should make the low note visible");
    expect(!midi_editor_note_rect(&layout, &notes[0], first_clip->duration_frames, &note_rect),
           "stored low pitch viewport should hide the high note");

    midi_editor_store_pitch_viewport(&state, &selection, -20, layout.key_row_count);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.lowest_note == ENGINE_MIDI_NOTE_MIN,
           "pitch viewport should clamp below-range top notes to the MIDI floor");
    expect(layout.highest_note == layout.key_row_count - 1,
           "pitch viewport clamp should preserve the visible row count at the MIDI floor");

    state.selected_clip_index = second_clip_index;
    state.selection[0].clip_index = second_clip_index;
    midi_editor_compute_layout(&state, &layout);
    default_top_note = layout.key_row_count < 12 ? 60 + layout.key_row_count - 1 : 71;
    default_low_note = default_top_note - layout.key_row_count + 1;
    expect(layout.highest_note == default_top_note && layout.lowest_note == default_low_note,
           "pitch viewport state should not leak across selected MIDI clips");

    state.selected_clip_index = first_clip_index;
    state.selection[0].clip_index = first_clip_index;
    midi_editor_compute_layout(&state, &layout);
    expect(layout.lowest_note == ENGINE_MIDI_NOTE_MIN,
           "pitch viewport state should be restored when returning to its owning clip");

    state_destroy(&state);
}

static void test_midi_editor_pitch_viewport_routes_edit_paths(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t clip_duration = (uint64_t)cfg.sample_rate * 4u;
    uint64_t note_duration = (uint64_t)cfg.sample_rate / 4u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, clip_duration, &clip_index),
           "failed to create MIDI clip for pitch routing test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 4u, note_duration, 96, 0.70f},
                                     NULL),
           "failed to seed first high pitch routing note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 2u, note_duration, 100, 0.80f},
                                     NULL),
           "failed to seed second high pitch routing note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.timeline_snap_enabled = false;

    MidiEditorSelection selection = {0};
    expect(midi_editor_get_selection(&state, &selection), "pitch routing selection missing");

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    midi_editor_store_pitch_viewport(&state, &selection, 108, layout.key_row_count);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.highest_note == 108 && layout.lowest_note <= 96,
           "pitch routing test should use a high visible pitch range");

    InputManager manager = {0};
    int create_x = layout.grid_rect.x + layout.grid_rect.w * 3 / 4;
    int create_y = midi_editor_note_center_y_for_test(&layout, 104);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, create_x, create_y);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, create_x, create_y);

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    expect(find_midi_note_index_by_pitch(clip, 104) >= 0,
           "empty-grid create should use the active high pitch viewport");

    midi_editor_compute_layout(&state, &layout);
    clip = &engine_get_tracks(state.engine)[0].clips[clip_index];
    int note_100_index = find_midi_note_index_by_pitch(clip, 100);
    expect(note_100_index >= 0, "pitch routing note 100 missing after create");
    SDL_Rect note_100_rect = {0, 0, 0, 0};
    expect(midi_editor_note_rect(&layout,
                                 &engine_clip_midi_notes(clip)[note_100_index],
                                 clip->duration_frames,
                                 &note_100_rect),
           "pitch routing note 100 should render in the high viewport");
    dispatch_mouse_motion(&manager,
                          &state,
                          note_100_rect.x + note_100_rect.w / 2,
                          note_100_rect.y + note_100_rect.h / 2);
    expect(state.midi_editor_ui.hover_note_valid, "hover should find high visible notes");
    clip = &engine_get_tracks(state.engine)[0].clips[clip_index];
    expect(engine_clip_midi_notes(clip)[state.midi_editor_ui.hover_note_index].note == 100,
           "hover should target the high visible note");

    state.midi_editor_ui.selected_note_index = -1;
    memset(state.midi_editor_ui.selected_note_indices, 0, sizeof(state.midi_editor_ui.selected_note_indices));
    int note_96_index = find_midi_note_index_by_pitch(clip, 96);
    note_100_index = find_midi_note_index_by_pitch(clip, 100);
    expect(note_96_index >= 0 && note_100_index >= 0, "high notes should exist before marquee");
    SDL_Rect note_96_rect = {0, 0, 0, 0};
    expect(midi_editor_note_rect(&layout,
                                 &engine_clip_midi_notes(clip)[note_96_index],
                                 clip->duration_frames,
                                 &note_96_rect),
           "pitch routing note 96 should render in the high viewport");
    expect(midi_editor_note_rect(&layout,
                                 &engine_clip_midi_notes(clip)[note_100_index],
                                 clip->duration_frames,
                                 &note_100_rect),
           "pitch routing note 100 should still render in the high viewport");
    int start_x = note_96_rect.x < note_100_rect.x ? note_96_rect.x - 6 : note_100_rect.x - 6;
    int end_x = note_96_rect.x + note_96_rect.w > note_100_rect.x + note_100_rect.w
        ? note_96_rect.x + note_96_rect.w + 6
        : note_100_rect.x + note_100_rect.w + 6;
    int start_y = note_100_rect.y - 6;
    int end_y = note_96_rect.y + note_96_rect.h + 6;
    if (start_x < layout.grid_rect.x) start_x = layout.grid_rect.x + 1;
    if (end_x > layout.grid_rect.x + layout.grid_rect.w - 1) end_x = layout.grid_rect.x + layout.grid_rect.w - 1;
    if (start_y < layout.grid_rect.y) start_y = layout.grid_rect.y + 1;
    if (end_y > layout.grid_rect.y + layout.grid_rect.h - 1) end_y = layout.grid_rect.y + layout.grid_rect.h - 1;

    SDL_Keymod old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | KMOD_SHIFT));
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, start_x, start_y);
    dispatch_mouse_motion(&manager, &state, end_x, end_y);
    expect(state.midi_editor_ui.marquee_preview_note_indices[note_96_index],
           "marquee preview should include note 96 in high pitch viewport");
    expect(state.midi_editor_ui.marquee_preview_note_indices[note_100_index],
           "marquee preview should include note 100 in high pitch viewport");
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, end_x, end_y);
    SDL_SetModState(old_mods);
    expect(state.midi_editor_ui.selected_note_indices[note_96_index],
           "marquee commit should select note 96 in high pitch viewport");
    expect(state.midi_editor_ui.selected_note_indices[note_100_index],
           "marquee commit should select note 100 in high pitch viewport");

    int drag_start_x = note_96_rect.x + note_96_rect.w / 2;
    int drag_start_y = note_96_rect.y + note_96_rect.h / 2;
    int drag_end_x = drag_start_x + layout.grid_rect.w / 10;
    int drag_end_y = midi_editor_note_center_y_for_test(&layout, 98);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, drag_start_x, drag_start_y);
    dispatch_mouse_motion(&manager, &state, drag_end_x, drag_end_y);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, drag_end_x, drag_end_y);

    clip = &engine_get_tracks(state.engine)[0].clips[clip_index];
    expect(find_midi_note_index_by_pitch(clip, 98) >= 0,
           "selected group move should route note 96 through the high pitch viewport");
    expect(find_midi_note_index_by_pitch(clip, 102) >= 0,
           "selected group move should preserve selected pitch deltas in the high viewport");
    expect(find_midi_note_index_by_pitch(clip, 104) >= 0,
           "selected group move should not mutate the unselected high note");

    state_destroy(&state);
}

static void test_midi_editor_pitch_scroll_zoom_gestures(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t clip_duration = (uint64_t)cfg.sample_rate * 4u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, clip_duration, &clip_index),
           "failed to create MIDI clip for pitch gesture test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    InputManager manager = {0};
    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    int default_top = layout.highest_note;
    int default_rows = layout.key_row_count;
    uint64_t view_start = layout.view_start_frame;
    uint64_t view_span = layout.view_span_frames;
    int anchor_x = layout.grid_rect.x + layout.grid_rect.w / 2;
    int anchor_y = layout.grid_rect.y + layout.grid_rect.h / 2;

    dispatch_wheel_with_mod(&manager, &state, anchor_x, anchor_y, 4, KMOD_CTRL);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.highest_note == default_top + 4,
           "Ctrl-wheel over MIDI grid should scroll the visible pitch range upward");
    expect(layout.key_row_count == default_rows,
           "pitch scroll should preserve visible pitch row count");
    expect(layout.view_start_frame == view_start && layout.view_span_frames == view_span,
           "pitch scroll should not disturb horizontal MIDI viewport state");

    int create_y = midi_editor_note_center_y_for_test(&layout, layout.highest_note);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, anchor_x, create_y);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, anchor_x, create_y);
    const EngineClip* clip = &engine_get_tracks(state.engine)[0].clips[clip_index];
    expect(find_midi_note_index_by_pitch(clip, (uint8_t)layout.highest_note) >= 0,
           "note creation after pitch scroll should use the scrolled pitch range");

    int zoom_anchor_y = midi_editor_note_center_y_for_test(&layout, 64);
    dispatch_wheel_with_mod(&manager, &state, anchor_x, zoom_anchor_y, 1, (SDL_Keymod)(KMOD_CTRL | KMOD_ALT));
    midi_editor_compute_layout(&state, &layout);
    expect(layout.key_row_count < default_rows,
           "Ctrl-Alt-wheel over MIDI grid should zoom into fewer pitch rows");
    expect(layout.lowest_note <= 64 && layout.highest_note >= 64,
           "pitch zoom should keep the anchor pitch visible");
    expect(layout.view_start_frame == view_start && layout.view_span_frames == view_span,
           "pitch zoom should not disturb horizontal MIDI viewport state");

    int zoomed_rows = layout.key_row_count;
    dispatch_wheel_with_mod(&manager, &state, anchor_x, zoom_anchor_y, -1, (SDL_Keymod)(KMOD_CTRL | KMOD_ALT));
    midi_editor_compute_layout(&state, &layout);
    expect(layout.key_row_count > zoomed_rows,
           "Ctrl-Alt-wheel down should zoom back out to more pitch rows");
    expect(layout.view_start_frame == view_start && layout.view_span_frames == view_span,
           "pitch zoom-out should not disturb horizontal MIDI viewport state");

    state_destroy(&state);
}

static void test_midi_editor_pitch_fit_selected_notes(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t clip_duration = (uint64_t)cfg.sample_rate * 6u;
    uint64_t note_duration = (uint64_t)cfg.sample_rate / 4u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, clip_duration, &clip_index),
           "failed to create MIDI clip for pitch fit test");
    int high_a_index = -1;
    int high_b_index = -1;
    int low_index = -1;
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 4u, note_duration, 96, 0.70f},
                                     &high_a_index),
           "failed to seed first high pitch fit note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 2u, note_duration, 100, 0.80f},
                                     &high_b_index),
           "failed to seed second high pitch fit note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate, note_duration, 36, 0.75f},
                                     &low_index),
           "failed to seed low pitch fit note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    InputManager manager = {0};
    MidiEditorSelection selection = {0};
    expect(midi_editor_get_selection(&state, &selection), "pitch fit selection missing");

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    int anchor_x = layout.grid_rect.x + layout.grid_rect.w / 2;
    int anchor_y = layout.grid_rect.y + layout.grid_rect.h / 2;
    int default_rows = layout.key_row_count;
    uint64_t full_span = layout.view_span_frames;

    dispatch_wheel_with_mod(&manager, &state, anchor_x, anchor_y, 1, KMOD_ALT);
    midi_editor_compute_layout(&state, &layout);
    uint64_t zoom_start = layout.view_start_frame;
    uint64_t zoom_span = layout.view_span_frames;
    expect(zoom_span < full_span, "pitch fit test should start with a zoomed horizontal viewport");

    state.midi_editor_ui.selected_track_index = 0;
    state.midi_editor_ui.selected_clip_index = clip_index;
    state.midi_editor_ui.selected_clip_creation_index = selection.clip->creation_index;
    state.midi_editor_ui.selected_note_index = high_a_index;
    memset(state.midi_editor_ui.selected_note_indices, 0, sizeof(state.midi_editor_ui.selected_note_indices));
    state.midi_editor_ui.selected_note_indices[high_a_index] = true;
    state.midi_editor_ui.selected_note_indices[high_b_index] = true;
    state.mouse_x = anchor_x;
    state.mouse_y = anchor_y;
    dispatch_key_with_mod(&manager, &state, SDLK_0, KMOD_CTRL);

    midi_editor_compute_layout(&state, &layout);
    expect(layout.key_row_count == default_rows,
           "pitch fit should preserve row count when selected notes already fit within it");
    expect(layout.view_start_frame == zoom_start && layout.view_span_frames == zoom_span,
           "pitch fit should not disturb horizontal MIDI viewport state");
    const EngineClip* clip = &engine_get_tracks(state.engine)[0].clips[clip_index];
    SDL_Rect note_rect = {0, 0, 0, 0};
    expect(midi_editor_note_rect(&layout,
                                 &engine_clip_midi_notes(clip)[high_a_index],
                                 clip->duration_frames,
                                 &note_rect),
           "pitch fit should bring the first high selected note into view");
    expect(midi_editor_note_rect(&layout,
                                 &engine_clip_midi_notes(clip)[high_b_index],
                                 clip->duration_frames,
                                 &note_rect),
           "pitch fit should bring the second high selected note into view");

    state.midi_editor_ui.selected_note_index = low_index;
    memset(state.midi_editor_ui.selected_note_indices, 0, sizeof(state.midi_editor_ui.selected_note_indices));
    state.midi_editor_ui.selected_note_indices[low_index] = true;
    dispatch_key_with_mod(&manager, &state, SDLK_0, KMOD_CTRL);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.view_start_frame == zoom_start && layout.view_span_frames == zoom_span,
           "low-note pitch fit should not disturb horizontal MIDI viewport state");
    expect(midi_editor_note_rect(&layout,
                                 &engine_clip_midi_notes(clip)[low_index],
                                 clip->duration_frames,
                                 &note_rect),
           "pitch fit should bring a low selected note into view");

    state_destroy(&state);
}

static void test_midi_editor_pitch_viewport_edge_cases_and_selection_stability(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t clip_duration = (uint64_t)cfg.sample_rate * 8u;
    uint64_t note_duration = (uint64_t)cfg.sample_rate / 4u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, clip_duration, &clip_index),
           "failed to create MIDI clip for pitch edge-case test");
    int low_index = -1;
    int center_index = -1;
    int high_index = -1;
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){0, note_duration, ENGINE_MIDI_NOTE_MIN, 0.70f},
                                     &low_index),
           "failed to seed low pitch edge-case note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate, note_duration, 64, 0.75f},
                                     &center_index),
           "failed to seed center pitch edge-case note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate * 2u,
                                                      note_duration,
                                                      ENGINE_MIDI_NOTE_MAX,
                                                      0.80f},
                                     &high_index),
           "failed to seed high pitch edge-case note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    InputManager manager = {0};
    MidiEditorSelection selection = {0};
    expect(midi_editor_get_selection(&state, &selection), "pitch edge-case selection missing");

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    int default_rows = layout.key_row_count;
    uint64_t full_span = layout.view_span_frames;
    int anchor_x = layout.grid_rect.x + layout.grid_rect.w / 2;
    int anchor_y = layout.grid_rect.y + layout.grid_rect.h / 2;

    dispatch_wheel_with_mod(&manager, &state, anchor_x, anchor_y, 1, KMOD_ALT);
    midi_editor_compute_layout(&state, &layout);
    uint64_t zoom_start = layout.view_start_frame;
    uint64_t zoom_span = layout.view_span_frames;
    expect(zoom_span < full_span, "pitch edge-case test should start with a zoomed horizontal viewport");

    int center_selection[] = {center_index};
    set_midi_editor_note_selection_for_test(&state, &selection, center_index, center_selection, 1);

    dispatch_wheel_with_mod(&manager, &state, anchor_x, anchor_y, 200, KMOD_CTRL);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.highest_note == ENGINE_MIDI_NOTE_MAX,
           "large upward pitch scroll should clamp at the MIDI ceiling");
    expect(layout.key_row_count == default_rows,
           "large pitch scroll should preserve row count at the ceiling");
    expect(layout.view_start_frame == zoom_start && layout.view_span_frames == zoom_span,
           "ceiling pitch scroll should not disturb horizontal viewport state");
    expect(state.midi_editor_ui.selected_note_index == center_index &&
               state.midi_editor_ui.selected_note_indices[center_index],
           "ceiling pitch scroll should preserve selected-note state");

    dispatch_wheel_with_mod(&manager, &state, anchor_x, anchor_y, -300, KMOD_CTRL);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.lowest_note == ENGINE_MIDI_NOTE_MIN,
           "large downward pitch scroll should clamp at the MIDI floor");
    expect(layout.key_row_count == default_rows,
           "large pitch scroll should preserve row count at the floor");
    expect(layout.view_start_frame == zoom_start && layout.view_span_frames == zoom_span,
           "floor pitch scroll should not disturb horizontal viewport state");
    expect(state.midi_editor_ui.selected_note_index == center_index &&
               state.midi_editor_ui.selected_note_indices[center_index],
           "floor pitch scroll should preserve selected-note state");

    int floor_anchor_y = midi_editor_note_center_y_for_test(&layout, ENGINE_MIDI_NOTE_MIN);
    for (int i = 0; i < MIDI_EDITOR_VISIBLE_KEY_ROWS; ++i) {
        dispatch_wheel_with_mod(&manager,
                                &state,
                                anchor_x,
                                floor_anchor_y,
                                1,
                                (SDL_Keymod)(KMOD_CTRL | KMOD_ALT));
        midi_editor_compute_layout(&state, &layout);
        floor_anchor_y = midi_editor_note_center_y_for_test(&layout, ENGINE_MIDI_NOTE_MIN);
    }
    expect(layout.key_row_count == 1 &&
               layout.lowest_note == ENGINE_MIDI_NOTE_MIN &&
               layout.highest_note == ENGINE_MIDI_NOTE_MIN,
           "pitch zoom-in at the MIDI floor should clamp to a single floor row");
    expect(layout.view_start_frame == zoom_start && layout.view_span_frames == zoom_span,
           "floor pitch zoom-in should not disturb horizontal viewport state");
    expect(state.midi_editor_ui.selected_note_index == center_index &&
               state.midi_editor_ui.selected_note_indices[center_index],
           "floor pitch zoom-in should preserve selected-note state");

    for (int i = 0; i < MIDI_EDITOR_VISIBLE_KEY_ROWS; ++i) {
        floor_anchor_y = midi_editor_note_center_y_for_test(&layout, ENGINE_MIDI_NOTE_MIN);
        dispatch_wheel_with_mod(&manager,
                                &state,
                                anchor_x,
                                floor_anchor_y,
                                -1,
                                (SDL_Keymod)(KMOD_CTRL | KMOD_ALT));
        midi_editor_compute_layout(&state, &layout);
    }
    expect(layout.key_row_count == default_rows && layout.lowest_note == ENGINE_MIDI_NOTE_MIN,
           "pitch zoom-out at the MIDI floor should restore full rows without crossing below MIDI note 0");
    expect(layout.highest_note == default_rows - 1,
           "pitch zoom-out at the MIDI floor should clamp the top note to the restored row count");
    expect(layout.view_start_frame == zoom_start && layout.view_span_frames == zoom_span,
           "floor pitch zoom-out should not disturb horizontal viewport state");

    int no_selection_top = layout.highest_note;
    int no_selection_rows = layout.key_row_count;
    set_midi_editor_note_selection_for_test(&state, &selection, -1, NULL, 0);
    state.mouse_x = anchor_x;
    state.mouse_y = anchor_y;
    dispatch_key_with_mod(&manager, &state, SDLK_0, KMOD_CTRL);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.highest_note == no_selection_top && layout.key_row_count == no_selection_rows,
           "pitch fit with no selected notes should leave pitch viewport unchanged");
    expect(layout.view_start_frame == zoom_start && layout.view_span_frames == zoom_span,
           "pitch fit with no selected notes should not disturb horizontal viewport state");

    int wide_selection[] = {low_index, high_index};
    set_midi_editor_note_selection_for_test(&state, &selection, low_index, wide_selection, 2);
    dispatch_key_with_mod(&manager, &state, SDLK_0, KMOD_CTRL);
    midi_editor_compute_layout(&state, &layout);
    expect(layout.key_row_count == default_rows,
           "wide pitch fit should use the maximum available editor row count");
    expect(layout.lowest_note >= ENGINE_MIDI_NOTE_MIN && layout.highest_note <= ENGINE_MIDI_NOTE_MAX,
           "wide pitch fit should stay inside MIDI pitch bounds");
    expect(layout.view_start_frame == zoom_start && layout.view_span_frames == zoom_span,
           "wide pitch fit should not disturb horizontal viewport state");
    expect(state.midi_editor_ui.selected_note_index == low_index &&
               state.midi_editor_ui.selected_note_indices[low_index] &&
               state.midi_editor_ui.selected_note_indices[high_index],
           "wide pitch fit should preserve multi-note selection state");

    state_destroy(&state);
}

static void test_midi_editor_drag_moves_note(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for drag test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 4u,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      60,
                                                      1.0f},
                                     NULL),
           "failed to seed drag note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.timeline_snap_enabled = false;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    SDL_Rect note_rect = {0, 0, 0, 0};
    expect(midi_editor_note_rect(&layout,
                                 &engine_clip_midi_notes(clip)[0],
                                 clip->duration_frames,
                                 &note_rect),
           "seed note should produce a rect");

    InputManager manager = {0};
    int start_x = note_rect.x + note_rect.w / 2;
    int start_y = note_rect.y + note_rect.h / 2;
    int end_x = start_x + layout.grid_rect.w / 8;
    int end_y = layout.key_lane_rects[layout.highest_note - 62].y + layout.key_lane_rects[layout.highest_note - 62].h / 2;
    state.midi_editor_ui.selected_note_index = -1;
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, start_x, start_y);
    dispatch_mouse_motion(&manager, &state, end_x, end_y);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, end_x, end_y);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(state.midi_editor_ui.selected_note_index == 0, "first note click should select note");
    expect(notes && notes[0].note == 60, "first note click-drag should not move unselected note pitch");
    expect(notes[0].start_frame == (uint64_t)cfg.sample_rate / 4u,
           "first note click-drag should not move unselected note start");

    midi_editor_compute_layout(&state, &layout);
    expect(midi_editor_note_rect(&layout, &notes[0], clip->duration_frames, &note_rect),
           "selected note should produce a rect");
    start_x = note_rect.x + note_rect.w / 2;
    start_y = note_rect.y + note_rect.h / 2;
    end_x = start_x + layout.grid_rect.w / 8;
    end_y = layout.key_lane_rects[layout.highest_note - 62].y + layout.key_lane_rects[layout.highest_note - 62].h / 2;
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, start_x, start_y);

    SDL_Event motion;
    memset(&motion, 0, sizeof(motion));
    motion.type = SDL_MOUSEMOTION;
    motion.motion.x = end_x;
    motion.motion.y = end_y;
    expect(midi_editor_input_handle_event(&manager, &state, &motion), "MIDI editor motion not consumed");
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, end_x, end_y);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(engine_clip_midi_note_count(clip) == 1, "drag should keep one note");
    expect(notes && notes[0].note == 62, "drag should move pitch");
    expect(notes[0].start_frame > (uint64_t)cfg.sample_rate / 4u, "drag should move start later");
    expect(undo_manager_undo(&state.undo, &state), "drag undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[state.selected_clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && notes[0].note == 60, "drag undo should restore pitch");

    state_destroy(&state);
}

static void test_midi_editor_resize_requires_selected_note(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t start_frame = (uint64_t)cfg.sample_rate / 4u;
    uint64_t duration = (uint64_t)cfg.sample_rate / 4u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for resize guard test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){start_frame, duration, 60, 1.0f},
                                     NULL),
           "failed to seed resize guard note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.timeline_snap_enabled = false;
    state.midi_editor_ui.selected_note_index = -1;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    SDL_Rect note_rect = {0, 0, 0, 0};
    expect(midi_editor_note_rect(&layout,
                                 &engine_clip_midi_notes(clip)[0],
                                 clip->duration_frames,
                                 &note_rect),
           "resize guard note should produce a rect");

    InputManager manager = {0};
    int edge_x = note_rect.x + note_rect.w - 1;
    int edge_y = note_rect.y + note_rect.h / 2;
    int extend_x = edge_x + layout.grid_rect.w / 8;
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, edge_x, edge_y);
    dispatch_mouse_motion(&manager, &state, extend_x, edge_y);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, extend_x, edge_y);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(state.midi_editor_ui.selected_note_index == 0, "first edge click should select note");
    expect(notes && notes[0].duration_frames == duration,
           "first edge click-drag should not resize unselected note");

    midi_editor_compute_layout(&state, &layout);
    expect(midi_editor_note_rect(&layout, &notes[0], clip->duration_frames, &note_rect),
           "selected resize guard note should produce a rect");
    edge_x = note_rect.x + note_rect.w - 1;
    edge_y = note_rect.y + note_rect.h / 2;
    extend_x = edge_x + layout.grid_rect.w / 8;
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, edge_x, edge_y);
    dispatch_mouse_motion(&manager, &state, extend_x, edge_y);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, extend_x, edge_y);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && notes[0].duration_frames > duration,
           "selected edge drag should resize note duration");

    state_destroy(&state);
}

static void test_midi_editor_shift_drag_edits_velocity(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for velocity drag test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 4u,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      60,
                                                      0.40f},
                                     NULL),
           "failed to seed velocity note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.timeline_snap_enabled = false;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    SDL_Rect note_rect = {0, 0, 0, 0};
    expect(midi_editor_note_rect(&layout,
                                 &engine_clip_midi_notes(clip)[0],
                                 clip->duration_frames,
                                 &note_rect),
           "velocity note should produce a rect");

    InputManager manager = {0};
    int start_x = note_rect.x + note_rect.w / 2;
    int start_y = note_rect.y + note_rect.h / 2;
    SDL_Keymod old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | KMOD_SHIFT));
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, start_x, start_y);
    dispatch_mouse_motion(&manager, &state, start_x, start_y - 72);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, start_x, start_y - 72);
    SDL_SetModState(old_mods);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(notes && notes[0].note == 60, "velocity drag should not move pitch");
    expect(notes[0].start_frame == (uint64_t)cfg.sample_rate / 4u,
           "velocity drag should not move timing");
    expect(notes[0].velocity > 0.90f, "shift-drag up should raise note velocity");
    expect(state.midi_editor_ui.default_velocity > 0.90f,
           "shift-drag should seed the default velocity from the edited note");
    expect(undo_manager_undo(&state.undo, &state), "velocity drag undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[state.selected_clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && notes[0].velocity == 0.40f, "velocity drag undo should restore velocity");

    state_destroy(&state);
}

static void test_midi_editor_qwerty_records_timed_note(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t clip_start = (uint64_t)cfg.sample_rate;
    uint64_t clip_duration = (uint64_t)cfg.sample_rate * 2u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, clip_start, clip_duration, &clip_index),
           "failed to create MIDI clip for qwerty record test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.timeline_snap_enabled = false;

    InputManager manager = {0};
    dispatch_key_event(&manager, &state, SDL_KEYDOWN, SDLK_r);
    expect(state.midi_editor_ui.qwerty_record_armed, "R should arm QWERTY recording");

    expect(engine_transport_seek(state.engine, clip_start + (uint64_t)cfg.sample_rate / 4u),
           "failed to seek to note start");
    dispatch_key_event(&manager, &state, SDL_KEYDOWN, SDLK_a);

    expect(engine_transport_seek(state.engine, clip_start + (uint64_t)cfg.sample_rate * 3u / 4u),
           "failed to seek to note end");
    dispatch_key_event(&manager, &state, SDL_KEYUP, SDLK_a);

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(engine_clip_midi_note_count(clip) == 1, "QWERTY key pair should create one note");
    expect(notes != NULL, "QWERTY note list missing");
    expect(notes[0].note == 60, "A key should record C4");
    expect(notes[0].start_frame == (uint64_t)cfg.sample_rate / 4u, "QWERTY note start mismatch");
    expect(notes[0].duration_frames == (uint64_t)cfg.sample_rate / 2u, "QWERTY note duration mismatch");
    expect(notes[0].velocity > 0.79f && notes[0].velocity < 0.81f,
           "QWERTY note should use default velocity");
    expect(undo_manager_can_undo(&state.undo), "QWERTY note should push undo");
    expect(undo_manager_undo(&state.undo, &state), "QWERTY note undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[state.selected_clip_index];
    expect(engine_clip_midi_note_count(clip) == 0, "QWERTY undo should remove note");

    dispatch_key_event(&manager, &state, SDL_KEYDOWN, SDLK_r);
    expect(!state.midi_editor_ui.qwerty_record_armed, "second R should disarm QWERTY recording");

    state_destroy(&state);
}

static void test_midi_editor_qwerty_octave_and_velocity_controls(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t clip_start = (uint64_t)cfg.sample_rate;
    uint64_t clip_duration = (uint64_t)cfg.sample_rate * 2u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, clip_start, clip_duration, &clip_index),
           "failed to create MIDI clip for qwerty controls test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.timeline_snap_enabled = false;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);

    InputManager manager = {0};
    dispatch_mouse_button(&manager,
                          &state,
                          SDL_MOUSEBUTTONDOWN,
                          layout.octave_up_button_rect.x + layout.octave_up_button_rect.w / 2,
                          layout.octave_up_button_rect.y + layout.octave_up_button_rect.h / 2);
    expect(state.midi_editor_ui.qwerty_octave_offset == 1,
           "Oct+ button should raise QWERTY octave");
    dispatch_mouse_button(&manager,
                          &state,
                          SDL_MOUSEBUTTONDOWN,
                          layout.velocity_down_button_rect.x + layout.velocity_down_button_rect.w / 2,
                          layout.velocity_down_button_rect.y + layout.velocity_down_button_rect.h / 2);
    expect(state.midi_editor_ui.default_velocity > 0.74f &&
               state.midi_editor_ui.default_velocity < 0.76f,
           "Vel- button should lower default velocity");

    dispatch_key_event(&manager, &state, SDL_KEYDOWN, SDLK_r);
    expect(engine_transport_seek(state.engine, clip_start + (uint64_t)cfg.sample_rate / 4u),
           "failed to seek to octave note start");
    dispatch_key_event(&manager, &state, SDL_KEYDOWN, SDLK_a);
    expect(engine_transport_seek(state.engine, clip_start + (uint64_t)cfg.sample_rate / 2u),
           "failed to seek to octave note end");
    dispatch_key_event(&manager, &state, SDL_KEYUP, SDLK_a);

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(engine_clip_midi_note_count(clip) == 1, "QWERTY controls should create one note");
    expect(notes && notes[0].note == 72, "Oct+ should record A key as C5");
    expect(notes[0].velocity > 0.74f && notes[0].velocity < 0.76f,
           "QWERTY controls should record lowered default velocity");

    state_destroy(&state);
}

static void test_midi_editor_quantize_button_snaps_selected_note(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for quantize command test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){7000u, 13000u, 60, 0.80f},
                                     NULL),
           "failed to seed off-grid quantize note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    state.midi_editor_ui.selected_track_index = 0;
    state.midi_editor_ui.selected_clip_index = clip_index;
    state.midi_editor_ui.selected_clip_creation_index = clip->creation_index;
    state.midi_editor_ui.selected_note_index = 0;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);

    InputManager manager = {0};
    dispatch_mouse_button(&manager,
                          &state,
                          SDL_MOUSEBUTTONDOWN,
                          layout.quantize_button_rect.x + layout.quantize_button_rect.w / 2,
                          layout.quantize_button_rect.y + layout.quantize_button_rect.h / 2);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(notes && engine_clip_midi_note_count(clip) == 1, "quantize should keep one note");
    expect(notes[0].start_frame == 6000u, "quantize should snap start to the 1/16 grid");
    expect(notes[0].duration_frames == 12000u, "quantize should snap note end to the 1/16 grid");
    expect(undo_manager_can_undo(&state.undo), "quantize command should push undo");
    expect(undo_manager_undo(&state.undo, &state), "quantize undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && notes[0].start_frame == 7000u && notes[0].duration_frames == 13000u,
           "quantize undo should restore off-grid timing");

    state_destroy(&state);
}

static void test_midi_editor_shift_click_toggles_multi_selection(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for multi-select test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 4u,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      60,
                                                      0.50f},
                                     NULL),
           "failed to seed first multi-select note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 2u,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      64,
                                                      0.60f},
                                     NULL),
           "failed to seed second multi-select note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.midi_editor_ui.selected_note_index = -1;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    SDL_Rect first = {0, 0, 0, 0};
    SDL_Rect second = {0, 0, 0, 0};
    expect(notes && midi_editor_note_rect(&layout, &notes[0], clip->duration_frames, &first),
           "first multi-select note should produce a rect");
    expect(midi_editor_note_rect(&layout, &notes[1], clip->duration_frames, &second),
           "second multi-select note should produce a rect");

    InputManager manager = {0};
    SDL_Keymod old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | KMOD_SHIFT));
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, first.x + first.w / 2, first.y + first.h / 2);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, first.x + first.w / 2, first.y + first.h / 2);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, second.x + second.w / 2, second.y + second.h / 2);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, second.x + second.w / 2, second.y + second.h / 2);
    SDL_SetModState(old_mods);

    expect(state.midi_editor_ui.selected_note_indices[0], "shift-click should select first note");
    expect(state.midi_editor_ui.selected_note_indices[1], "shift-click should add second note");
    expect(state.midi_editor_ui.selected_note_index == 1, "last shift-clicked note should be focused");

    SDL_SetModState((SDL_Keymod)(old_mods | KMOD_SHIFT));
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, first.x + first.w / 2, first.y + first.h / 2);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, first.x + first.w / 2, first.y + first.h / 2);
    SDL_SetModState(old_mods);

    expect(!state.midi_editor_ui.selected_note_indices[0], "second shift-click should deselect first note");
    expect(state.midi_editor_ui.selected_note_indices[1], "deselecting first note should keep second selected");
    expect(state.midi_editor_ui.selected_note_index == 1, "remaining note should stay focused");

    state_destroy(&state);
}

static void test_midi_editor_shift_marquee_selects_notes(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for marquee test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 4u,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      60,
                                                      0.50f},
                                     NULL),
           "failed to seed first marquee note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 2u,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      64,
                                                      0.60f},
                                     NULL),
           "failed to seed second marquee note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.midi_editor_ui.selected_note_index = -1;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    SDL_Rect first = {0, 0, 0, 0};
    SDL_Rect second = {0, 0, 0, 0};
    expect(notes && midi_editor_note_rect(&layout, &notes[0], clip->duration_frames, &first),
           "first marquee note should produce a rect");
    expect(midi_editor_note_rect(&layout, &notes[1], clip->duration_frames, &second),
           "second marquee note should produce a rect");

    int start_x = first.x - 10;
    int start_y = first.y < second.y ? first.y - 6 : second.y - 6;
    if (start_x < layout.grid_rect.x) start_x = layout.grid_rect.x + 1;
    if (start_y < layout.grid_rect.y) start_y = layout.grid_rect.y + 1;
    int end_x = second.x + second.w + 10;
    int end_y = first.y > second.y ? first.y + first.h + 6 : second.y + second.h + 6;
    if (end_x > layout.grid_rect.x + layout.grid_rect.w - 1) {
        end_x = layout.grid_rect.x + layout.grid_rect.w - 1;
    }
    if (end_y > layout.grid_rect.y + layout.grid_rect.h - 1) {
        end_y = layout.grid_rect.y + layout.grid_rect.h - 1;
    }

    InputManager manager = {0};
    SDL_Keymod old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | KMOD_SHIFT));
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, start_x, start_y);
    dispatch_mouse_motion(&manager, &state, end_x, end_y);
    expect(state.midi_editor_ui.marquee_active, "shift-drag empty grid should start marquee");
    expect(state.midi_editor_ui.marquee_preview_note_indices[0], "marquee preview should include first note");
    expect(state.midi_editor_ui.marquee_preview_note_indices[1], "marquee preview should include second note");
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, end_x, end_y);
    SDL_SetModState(old_mods);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_note_count(clip) == 2, "marquee selection should not create notes");
    expect(!state.midi_editor_ui.marquee_active, "mouse up should clear marquee state");
    expect(state.midi_editor_ui.selected_note_indices[0], "marquee commit should select first note");
    expect(state.midi_editor_ui.selected_note_indices[1], "marquee commit should select second note");

    state_destroy(&state);
}

static void test_midi_editor_multiselect_quantize_and_delete(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for batch command test");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){7000u, 13000u, 60, 0.50f}, NULL),
           "failed to seed first batch note");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){19000u, 13000u, 64, 0.60f}, NULL),
           "failed to seed second batch note");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){31000u, 8000u, 67, 0.70f}, NULL),
           "failed to seed third batch note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    state.midi_editor_ui.selected_track_index = 0;
    state.midi_editor_ui.selected_clip_index = clip_index;
    state.midi_editor_ui.selected_clip_creation_index = clip->creation_index;
    state.midi_editor_ui.selected_note_index = 0;
    state.midi_editor_ui.selected_note_indices[0] = true;
    state.midi_editor_ui.selected_note_indices[1] = true;

    InputManager manager = {0};
    dispatch_key(&manager, &state, SDLK_q);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(notes && engine_clip_midi_note_count(clip) == 3, "batch quantize should keep all notes");
    expect(notes[0].start_frame == 6000u && notes[0].duration_frames == 12000u,
           "batch quantize should snap first selected note");
    expect(notes[1].start_frame == 18000u && notes[1].duration_frames == 12000u,
           "batch quantize should snap second selected note");
    expect(notes[2].start_frame == 31000u && notes[2].duration_frames == 8000u,
           "batch quantize should not mutate unselected note");

    dispatch_key(&manager, &state, SDLK_DELETE);
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && engine_clip_midi_note_count(clip) == 1, "batch delete should remove selected notes");
    expect(notes[0].note == 67 && notes[0].start_frame == 31000u,
           "batch delete should keep the unselected note");

    state_destroy(&state);
}

static void test_midi_editor_copy_paste_selected_notes(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 3u, &clip_index),
           "failed to create MIDI clip for copy/paste test");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){24000u, 6000u, 60, 0.50f}, NULL),
           "failed to seed first copy note");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){36000u, 6000u, 64, 0.60f}, NULL),
           "failed to seed second copy note");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){54000u, 6000u, 67, 0.70f}, NULL),
           "failed to seed unselected copy note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    state.midi_editor_ui.selected_track_index = 0;
    state.midi_editor_ui.selected_clip_index = clip_index;
    state.midi_editor_ui.selected_clip_creation_index = clip->creation_index;
    state.midi_editor_ui.selected_note_index = 0;
    state.midi_editor_ui.selected_note_indices[0] = true;
    state.midi_editor_ui.selected_note_indices[1] = true;

    InputManager manager = {0};
    dispatch_command_key(&manager, &state, SDLK_c);
    expect(state.midi_editor_ui.clipboard_note_count == 2, "copy should store two selected notes");
    dispatch_command_key(&manager, &state, SDLK_v);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(notes && engine_clip_midi_note_count(clip) == 5, "paste should append copied notes");
    expect(notes[0].start_frame == 0u && notes[0].note == 60, "paste should place pattern at playhead-relative start");
    expect(notes[1].start_frame == 12000u && notes[1].note == 64, "paste should preserve copied spacing");
    expect(notes[0].velocity == 0.50f && notes[1].velocity == 0.60f, "paste should preserve velocities");
    expect(state.midi_editor_ui.selected_note_indices[0] && state.midi_editor_ui.selected_note_indices[1],
           "paste should select pasted notes");
    expect(!state.midi_editor_ui.selected_note_indices[2],
           "paste should not keep original notes selected");
    expect(undo_manager_can_undo(&state.undo), "paste should push undo");
    expect(undo_manager_undo(&state.undo, &state), "paste undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_note_count(clip) == 3, "paste undo should restore original note count");

    state_destroy(&state);
}

static void test_midi_editor_clipboard_shortcut_falls_through_without_note_selection(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 3u, &clip_index),
           "failed to create MIDI clip for clipboard fallthrough test");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){24000u, 6000u, 60, 0.50f}, NULL),
           "failed to seed clipboard fallthrough note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    state.midi_editor_ui.selected_track_index = 0;
    state.midi_editor_ui.selected_clip_index = clip_index;
    state.midi_editor_ui.selected_clip_creation_index = clip->creation_index;
    state.midi_editor_ui.selected_note_index = -1;
    memset(state.midi_editor_ui.selected_note_indices, 0, sizeof(state.midi_editor_ui.selected_note_indices));

    InputManager manager = {0};
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_c;

    SDL_Keymod old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | KMOD_CTRL));
    bool consumed = midi_editor_input_handle_event(&manager, &state, &event);
    SDL_SetModState(old_mods);

    expect(!consumed, "MIDI editor should let region copy shortcut fall through when no notes are selected");
    expect(state.midi_editor_ui.clipboard_note_count == 0,
           "fallthrough note copy should not leave a MIDI note clipboard payload");

    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_v;
    old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | KMOD_CTRL));
    consumed = midi_editor_input_handle_event(&manager, &state, &event);
    SDL_SetModState(old_mods);

    expect(!consumed, "MIDI editor should let region paste shortcut fall through without a note clipboard");

    state_destroy(&state);
}

static void test_midi_editor_duplicate_selected_notes(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 3u, &clip_index),
           "failed to create MIDI clip for duplicate test");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){6000u, 6000u, 60, 0.50f}, NULL),
           "failed to seed first duplicate note");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){18000u, 6000u, 64, 0.60f}, NULL),
           "failed to seed second duplicate note");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){72000u, 6000u, 67, 0.70f}, NULL),
           "failed to seed unselected duplicate note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    state.midi_editor_ui.selected_track_index = 0;
    state.midi_editor_ui.selected_clip_index = clip_index;
    state.midi_editor_ui.selected_clip_creation_index = clip->creation_index;
    state.midi_editor_ui.selected_note_index = 0;
    state.midi_editor_ui.selected_note_indices[0] = true;
    state.midi_editor_ui.selected_note_indices[1] = true;

    InputManager manager = {0};
    dispatch_command_key(&manager, &state, SDLK_d);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(notes && engine_clip_midi_note_count(clip) == 5, "duplicate should append selected group");
    expect(notes[2].start_frame == 24000u && notes[2].note == 60, "duplicate should start after selected span");
    expect(notes[3].start_frame == 36000u && notes[3].note == 64, "duplicate should preserve selected spacing");
    expect(notes[4].start_frame == 72000u && notes[4].note == 67, "duplicate should not move unselected note");
    expect(state.midi_editor_ui.selected_note_indices[2] && state.midi_editor_ui.selected_note_indices[3],
           "duplicate should select duplicated notes");
    expect(!state.midi_editor_ui.selected_note_indices[0] && !state.midi_editor_ui.selected_note_indices[1],
           "duplicate should replace selection with duplicated notes");
    expect(undo_manager_can_undo(&state.undo), "duplicate should push undo");
    expect(undo_manager_undo(&state.undo, &state), "duplicate undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_note_count(clip) == 3, "duplicate undo should restore original note count");

    state_destroy(&state);
}

static void test_midi_editor_note_command_shortcuts_edit_selection(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 3u, &clip_index),
           "failed to create MIDI clip for note command test");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){12000u, 12000u, 60, 0.50f}, NULL),
           "failed to seed first command note");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){24000u, 12000u, 64, 0.60f}, NULL),
           "failed to seed second command note");
    expect(engine_clip_midi_add_note(state.engine, 0, clip_index, (EngineMidiNote){72000u, 6000u, 67, 0.70f}, NULL),
           "failed to seed unselected command note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    MidiEditorSelection selection = {
        .track_index = 0,
        .clip_index = clip_index,
        .clip = clip
    };
    int selected[] = {0, 1};
    set_midi_editor_note_selection_for_test(&state, &selection, 0, selected, 2);

    InputManager manager = {0};
    state.midi_editor_ui.qwerty_test_enabled = true;
    expect(handle_key_with_mod(&manager, &state, SDLK_UP, KMOD_ALT),
           "Alt+Up should transpose selected notes during QWERTY test mode");

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(notes && notes[0].note == 61 && notes[1].note == 65,
           "Alt+Up should transpose selected notes by semitone");
    expect(notes[2].note == 67, "transpose should not mutate unselected notes");
    expect(state.midi_editor_ui.selected_note_indices[0] && state.midi_editor_ui.selected_note_indices[1],
           "transpose should preserve selected note group");
    expect(undo_manager_undo(&state.undo, &state), "transpose undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && notes[0].note == 60 && notes[1].note == 64,
           "transpose undo should restore pitches");
    expect(undo_manager_redo(&state.undo, &state), "transpose redo failed");

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    selection.clip = clip;
    set_midi_editor_note_selection_for_test(&state, &selection, 0, selected, 2);
    expect(handle_key_with_mod(&manager, &state, SDLK_DOWN, (SDL_Keymod)(KMOD_ALT | KMOD_SHIFT)),
           "Alt+Shift+Down should transpose selected notes by octave");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && notes[0].note == 49 && notes[1].note == 53,
           "Alt+Shift+Down should transpose selected notes by octave");

    selection.clip = clip;
    set_midi_editor_note_selection_for_test(&state, &selection, 0, selected, 2);
    expect(handle_key_with_mod(&manager, &state, SDLK_RIGHT, KMOD_CTRL),
           "Command/Ctrl+Right should nudge selected notes");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && notes[0].start_frame == 18000u && notes[1].start_frame == 30000u,
           "nudge should move selected notes by the active grid");
    expect(notes[2].start_frame == 72000u, "nudge should not move unselected notes");
    expect(undo_manager_undo(&state.undo, &state), "nudge undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && notes[0].start_frame == 12000u && notes[1].start_frame == 24000u,
           "nudge undo should restore starts");

    selection.clip = clip;
    set_midi_editor_note_selection_for_test(&state, &selection, 0, selected, 2);
    expect(handle_key_with_mod(&manager, &state, SDLK_RIGHT, (SDL_Keymod)(KMOD_CTRL | KMOD_SHIFT)),
           "Command/Ctrl+Shift+Right should lengthen selected notes");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && notes[0].duration_frames == 18000u && notes[1].duration_frames == 18000u,
           "resize should lengthen selected notes by the active grid");
    expect(notes[2].duration_frames == 6000u, "resize should not mutate unselected notes");

    selection.clip = clip;
    set_midi_editor_note_selection_for_test(&state, &selection, -1, NULL, 0);
    expect(!handle_key_with_mod(&manager, &state, SDLK_RIGHT, KMOD_CTRL),
           "note command should fall through when no MIDI notes are selected");

    state_destroy(&state);
}

static void test_midi_editor_multiselect_velocity_drag_updates_group(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for batch velocity test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 4u,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      60,
                                                      0.30f},
                                     NULL),
           "failed to seed first velocity group note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 2u,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      64,
                                                      0.40f},
                                     NULL),
           "failed to seed second velocity group note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate * 3u / 4u,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      67,
                                                      0.20f},
                                     NULL),
           "failed to seed unselected velocity group note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.timeline_snap_enabled = false;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    state.midi_editor_ui.selected_track_index = 0;
    state.midi_editor_ui.selected_clip_index = clip_index;
    state.midi_editor_ui.selected_clip_creation_index = clip->creation_index;
    state.midi_editor_ui.selected_note_index = 0;
    state.midi_editor_ui.selected_note_indices[0] = true;
    state.midi_editor_ui.selected_note_indices[1] = true;
    SDL_Rect first = {0, 0, 0, 0};
    expect(notes && midi_editor_note_rect(&layout, &notes[0], clip->duration_frames, &first),
           "first velocity group note should produce a rect");

    InputManager manager = {0};
    int start_x = first.x + first.w / 2;
    int start_y = first.y + first.h / 2;
    SDL_Keymod old_mods = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(old_mods | KMOD_SHIFT));
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, start_x, start_y);
    dispatch_mouse_motion(&manager, &state, start_x, start_y - 60);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, start_x, start_y - 60);
    SDL_SetModState(old_mods);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && engine_clip_midi_note_count(clip) == 3, "velocity group drag should keep all notes");
    expect(notes[0].velocity > 0.79f, "velocity group drag should raise first selected note");
    expect(notes[1].velocity > 0.89f, "velocity group drag should raise second selected note");
    expect(notes[2].velocity > 0.19f && notes[2].velocity < 0.21f,
           "velocity group drag should not change unselected note");

    state_destroy(&state);
}

static void test_midi_editor_selected_group_click_collapses_on_release(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for group click collapse test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 4u,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      60,
                                                      0.40f},
                                     NULL),
           "failed to seed first group click note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 2u,
                                                      (uint64_t)cfg.sample_rate / 4u,
                                                      64,
                                                      0.50f},
                                     NULL),
           "failed to seed second group click note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    state.midi_editor_ui.selected_track_index = 0;
    state.midi_editor_ui.selected_clip_index = clip_index;
    state.midi_editor_ui.selected_clip_creation_index = clip->creation_index;
    state.midi_editor_ui.selected_note_index = 1;
    state.midi_editor_ui.selected_note_indices[0] = true;
    state.midi_editor_ui.selected_note_indices[1] = true;

    SDL_Rect second = {0, 0, 0, 0};
    expect(notes && midi_editor_note_rect(&layout, &notes[1], clip->duration_frames, &second),
           "second group click note should produce a rect");

    InputManager manager = {0};
    int x = second.x + second.w / 2;
    int y = second.y + second.h / 2;
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, x, y);
    expect(state.midi_editor_ui.selected_note_indices[0] && state.midi_editor_ui.selected_note_indices[1],
           "mouse down on selected group note should preserve group while gesture is pending");
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, x, y);

    expect(!state.midi_editor_ui.selected_note_indices[0],
           "click-release on selected group member should clear other notes");
    expect(state.midi_editor_ui.selected_note_indices[1],
           "click-release on selected group member should focus clicked note");
    expect(state.midi_editor_ui.selected_note_index == 1,
           "clicked group member should become focused note");

    state_destroy(&state);
}

static void test_midi_editor_selected_group_drag_moves_group_and_keeps_selection(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t duration = (uint64_t)cfg.sample_rate / 4u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 3u, &clip_index),
           "failed to create MIDI clip for group drag test");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 4u, duration, 60, 0.40f},
                                     NULL),
           "failed to seed first group drag note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate / 2u, duration, 64, 0.50f},
                                     NULL),
           "failed to seed second group drag note");
    expect(engine_clip_midi_add_note(state.engine,
                                     0,
                                     clip_index,
                                     (EngineMidiNote){(uint64_t)cfg.sample_rate * 5u / 4u, duration, 67, 0.60f},
                                     NULL),
           "failed to seed unselected group drag note");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.timeline_snap_enabled = false;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    state.midi_editor_ui.selected_track_index = 0;
    state.midi_editor_ui.selected_clip_index = clip_index;
    state.midi_editor_ui.selected_clip_creation_index = clip->creation_index;
    state.midi_editor_ui.selected_note_index = 0;
    state.midi_editor_ui.selected_note_indices[0] = true;
    state.midi_editor_ui.selected_note_indices[1] = true;

    SDL_Rect first = {0, 0, 0, 0};
    expect(notes && midi_editor_note_rect(&layout, &notes[0], clip->duration_frames, &first),
           "first group drag note should produce a rect");
    uint64_t first_start = notes[0].start_frame;
    uint64_t second_start = notes[1].start_frame;
    uint64_t third_start = notes[2].start_frame;

    InputManager manager = {0};
    int start_x = first.x + first.w / 2;
    int start_y = first.y + first.h / 2;
    int end_x = start_x + layout.grid_rect.w / 8;
    int end_y = layout.key_lane_rects[layout.highest_note - 62].y + layout.key_lane_rects[layout.highest_note - 62].h / 2;
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, start_x, start_y);
    expect(state.midi_editor_ui.selected_note_indices[0] && state.midi_editor_ui.selected_note_indices[1],
           "mouse down on selected group should keep group selected before drag threshold");
    dispatch_mouse_motion(&manager, &state, end_x, end_y);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, end_x, end_y);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    notes = engine_clip_midi_notes(clip);
    expect(notes && engine_clip_midi_note_count(clip) == 3, "group move should keep all notes");
    expect(notes[0].start_frame > first_start, "group move should move first selected note later");
    expect(notes[1].start_frame > second_start, "group move should move second selected note later");
    expect(notes[0].note == 62 && notes[1].note == 66, "group move should preserve pitch delta across selected notes");
    expect(notes[2].start_frame == third_start && notes[2].note == 67,
           "group move should not mutate unselected note");
    expect(state.midi_editor_ui.selected_note_indices[0] && state.midi_editor_ui.selected_note_indices[1],
           "group move should keep moved notes selected after mouse up");
    expect(!state.midi_editor_ui.selected_note_indices[2],
           "group move should not select unselected note after mouse up");

    state_destroy(&state);
}

static void test_midi_editor_snap_uses_editor_quantize_grid(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for quantize snap test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.timeline_snap_enabled = true;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);

    InputManager manager = {0};
    dispatch_mouse_button(&manager,
                          &state,
                          SDL_MOUSEBUTTONDOWN,
                          layout.quantize_down_button_rect.x + layout.quantize_down_button_rect.w / 2,
                          layout.quantize_down_button_rect.y + layout.quantize_down_button_rect.h / 2);
    expect(state.midi_editor_ui.quantize_division == 8, "Q- should move default grid from 1/16 to 1/8");

    midi_editor_compute_layout(&state, &layout);
    int x = layout.grid_rect.x + (int)((0.16 / 2.0) * (double)layout.grid_rect.w);
    int y = layout.key_lane_rects[layout.key_row_count / 2].y + layout.key_lane_rects[layout.key_row_count / 2].h / 2;
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONDOWN, x, y);
    dispatch_mouse_button(&manager, &state, SDL_MOUSEBUTTONUP, x, y);

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(notes && engine_clip_midi_note_count(clip) == 1, "snap create should add one note");
    expect(notes[0].start_frame == 12000u, "snap create should use the selected 1/8 grid");
    expect(notes[0].duration_frames == 12000u, "snap create duration should use the selected 1/8 grid");

    state_destroy(&state);
}

static void test_midi_editor_qwerty_record_snaps_to_quantize_grid(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    uint64_t clip_start = (uint64_t)cfg.sample_rate;
    uint64_t clip_duration = (uint64_t)cfg.sample_rate * 2u;
    expect(engine_add_midi_clip_to_track(state.engine, 0, clip_start, clip_duration, &clip_index),
           "failed to create MIDI clip for qwerty quantize test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.timeline_snap_enabled = true;

    InputManager manager = {0};
    dispatch_key_event(&manager, &state, SDL_KEYDOWN, SDLK_r);
    expect(engine_transport_seek(state.engine, clip_start + 7000u),
           "failed to seek to off-grid qwerty start");
    dispatch_key_event(&manager, &state, SDL_KEYDOWN, SDLK_a);
    expect(engine_transport_seek(state.engine, clip_start + 20000u),
           "failed to seek to off-grid qwerty end");
    dispatch_key_event(&manager, &state, SDL_KEYUP, SDLK_a);

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    const EngineMidiNote* notes = engine_clip_midi_notes(clip);
    expect(notes && engine_clip_midi_note_count(clip) == 1, "quantized QWERTY should create one note");
    expect(notes[0].start_frame == 6000u, "quantized QWERTY should snap start");
    expect(notes[0].duration_frames == 12000u, "quantized QWERTY should snap duration via note end");

    state_destroy(&state);
}

static void test_midi_editor_qwerty_test_auditions_without_recording(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for qwerty test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);

    InputManager manager = {0};
    dispatch_mouse_button(&manager,
                          &state,
                          SDL_MOUSEBUTTONDOWN,
                          layout.test_button_rect.x + layout.test_button_rect.w / 2,
                          layout.test_button_rect.y + layout.test_button_rect.h / 2);
    expect(state.midi_editor_ui.qwerty_test_enabled, "Test button should enable QWERTY test mode");

    dispatch_key_event(&manager, &state, SDL_KEYDOWN, SDLK_a);
    expect(state.midi_editor_ui.qwerty_active_notes[0].active, "test key should create active audition state");
    expect(state.midi_editor_ui.qwerty_active_notes[0].note == 60, "A key should audition C4");
    expect(!state.midi_editor_ui.qwerty_active_notes[0].record_on_release,
           "test key should not record on release");

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_note_count(clip) == 0, "test keydown should not create a MIDI note");

    dispatch_key_event(&manager, &state, SDL_KEYUP, SDLK_a);
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_note_count(clip) == 0, "test keyup should not create a MIDI note");
    expect(!state.midi_editor_ui.qwerty_active_notes[0].active, "test keyup should clear active audition state");

    state_destroy(&state);
}

static void test_midi_editor_instrument_dropdown_sets_region_preset(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for instrument dropdown test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    MidiEditorLayout layout = {0};
    midi_editor_compute_layout(&state, &layout);
    validate_layout(&state, &layout);

    InputManager manager = {0};
    dispatch_mouse_button(&manager,
                          &state,
                          SDL_MOUSEBUTTONDOWN,
                          layout.instrument_button_rect.x + layout.instrument_button_rect.w / 2,
                          layout.instrument_button_rect.y + layout.instrument_button_rect.h / 2);
    expect(state.midi_editor_ui.instrument_menu_open, "instrument button should open preset menu");
    expect(!state.midi_editor_ui.qwerty_test_enabled,
           "instrument affordance should not toggle QWERTY test mode");

    midi_editor_compute_layout(&state, &layout);
    expect(layout.instrument_menu_item_count >= ENGINE_INSTRUMENT_PRESET_COUNT,
           "instrument menu should expose all fixed presets");
    expect(layout.instrument_browser.total_row_count == ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT,
           "collapsed instrument preset browser should show category headers only");
    expect(layout.instrument_browser.row_count > 0 &&
               layout.instrument_browser.rows[0].type == MIDI_PRESET_BROWSER_ROW_CATEGORY,
           "instrument preset browser should start with a category header");
    expect(!rect_has_positive_size(&layout.instrument_menu_item_rects[ENGINE_INSTRUMENT_PRESET_SAW_LEAD]),
           "collapsed instrument preset browser should hide preset rows");
    dispatch_mouse_button(&manager,
                          &state,
                          SDL_MOUSEBUTTONDOWN,
                          layout.instrument_browser.rows[0].rect.x + layout.instrument_browser.rows[0].rect.w / 2,
                          layout.instrument_browser.rows[0].rect.y + layout.instrument_browser.rows[0].rect.h / 2);
    expect(state.midi_editor_ui.instrument_menu_open,
           "clicking a preset category header should keep the menu open");
    expect(state.midi_editor_ui.instrument_menu_expanded_category == ENGINE_INSTRUMENT_PRESET_CATEGORY_BASIC,
           "clicking Basic category should expand it");
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_instrument_preset(clip) == ENGINE_INSTRUMENT_PRESET_PURE_SINE,
           "clicking a preset category header should not select a preset");
    midi_editor_compute_layout(&state, &layout);
    expect(layout.instrument_browser.total_row_count >
               layout.instrument_browser.visible_capacity,
           "expanded Basic category should make the browser scrollable");
    int scroll_before = state.midi_editor_ui.instrument_menu_scroll_row;
    dispatch_mouse_wheel(&manager,
                         &state,
                         layout.instrument_menu_rect.x + layout.instrument_menu_rect.w / 2,
                         layout.instrument_menu_rect.y + layout.instrument_menu_rect.h / 2,
                         -1);
    expect(state.midi_editor_ui.instrument_menu_scroll_row > scroll_before,
           "expanded preset browser should scroll");

    state.midi_editor_ui.instrument_menu_scroll_row = 0;
    midi_editor_compute_layout(&state, &layout);
    SDL_Rect lead_category = {0};
    for (int i = 0; i < layout.instrument_browser.row_count; ++i) {
        if (layout.instrument_browser.rows[i].type == MIDI_PRESET_BROWSER_ROW_CATEGORY &&
            layout.instrument_browser.rows[i].category == ENGINE_INSTRUMENT_PRESET_CATEGORY_LEAD) {
            lead_category = layout.instrument_browser.rows[i].rect;
            break;
        }
    }
    expect(rect_has_positive_size(&lead_category),
           "Lead category should be visible after Basic expansion");
    dispatch_mouse_button(&manager,
                          &state,
                          SDL_MOUSEBUTTONDOWN,
                          lead_category.x + lead_category.w / 2,
                          lead_category.y + lead_category.h / 2);
    expect(state.midi_editor_ui.instrument_menu_open,
           "clicking Lead category should keep the menu open");
    expect(state.midi_editor_ui.instrument_menu_expanded_category == ENGINE_INSTRUMENT_PRESET_CATEGORY_LEAD,
           "clicking Lead category should expand it");
    midi_editor_compute_layout(&state, &layout);

    SDL_Rect saw_item = layout.instrument_menu_item_rects[ENGINE_INSTRUMENT_PRESET_SAW_LEAD];
    expect(rect_has_positive_size(&saw_item),
           "instrument preset browser should expose Saw Lead after expanding Lead");
    dispatch_mouse_button(&manager,
                          &state,
                          SDL_MOUSEBUTTONDOWN,
                          saw_item.x + saw_item.w / 2,
                          saw_item.y + saw_item.h / 2);
    expect(!state.midi_editor_ui.instrument_menu_open, "selecting a preset should close the menu");

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_note_count(clip) == 0,
           "instrument preset selection should not mutate MIDI notes");
    expect(engine_clip_midi_instrument_preset(clip) == ENGINE_INSTRUMENT_PRESET_SAW_LEAD,
           "instrument dropdown should set the selected MIDI region preset");
    expect(undo_manager_undo(&state.undo, &state), "instrument dropdown undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_instrument_preset(clip) == ENGINE_INSTRUMENT_PRESET_PURE_SINE,
           "instrument dropdown undo should restore previous preset");
    expect(undo_manager_redo(&state.undo, &state), "instrument dropdown redo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_instrument_preset(clip) == ENGINE_INSTRUMENT_PRESET_SAW_LEAD,
           "instrument dropdown redo should restore selected preset");

    state_destroy(&state);
}

static void test_midi_editor_instrument_panel_button_swaps_subview(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for instrument panel swap test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    MidiEditorLayout editor_layout = {0};
    midi_editor_compute_layout(&state, &editor_layout);
    validate_layout(&state, &editor_layout);
    expect(!midi_instrument_panel_should_render(&state), "instrument panel should start closed");

    InputManager manager = {0};
    dispatch_mouse_button(&manager,
                          &state,
                          SDL_MOUSEBUTTONDOWN,
                          editor_layout.instrument_panel_button_rect.x + editor_layout.instrument_panel_button_rect.w / 2,
                          editor_layout.instrument_panel_button_rect.y + editor_layout.instrument_panel_button_rect.h / 2);
    expect(state.midi_editor_ui.panel_mode == MIDI_REGION_PANEL_INSTRUMENT,
           "instrument edit button should switch to instrument panel");
    expect(midi_instrument_panel_should_render(&state), "instrument panel should render after edit button");
    expect(!midi_editor_input_qwerty_capturing(&state),
           "instrument panel should not leave QWERTY capture active");

    MidiInstrumentPanelLayout panel_layout = {0};
    midi_instrument_panel_compute_layout(&state, &panel_layout);
    validate_instrument_panel_layout(&state, &panel_layout);
    dispatch_instrument_mouse_button(&manager,
                                     &state,
                                     SDL_MOUSEBUTTONDOWN,
                                     panel_layout.notes_button_rect.x + panel_layout.notes_button_rect.w / 2,
                                     panel_layout.notes_button_rect.y + panel_layout.notes_button_rect.h / 2);
    expect(state.midi_editor_ui.panel_mode == MIDI_REGION_PANEL_EDITOR,
           "notes button should return to MIDI editor panel");

    state_destroy(&state);
}

static void test_midi_instrument_panel_grouped_preset_browser_sets_region_preset(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for instrument panel preset browser test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.midi_editor_ui.panel_mode = MIDI_REGION_PANEL_INSTRUMENT;

    MidiInstrumentPanelLayout layout = {0};
    midi_instrument_panel_compute_layout(&state, &layout);
    validate_instrument_panel_layout(&state, &layout);

    InputManager manager = {0};
    dispatch_instrument_mouse_button(&manager,
                                     &state,
                                     SDL_MOUSEBUTTONDOWN,
                                     layout.preset_button_rect.x + layout.preset_button_rect.w / 2,
                                     layout.preset_button_rect.y + layout.preset_button_rect.h / 2);
    expect(state.midi_editor_ui.instrument_menu_open,
           "instrument panel preset button should open grouped preset browser");

    midi_instrument_panel_compute_layout(&state, &layout);
    expect(layout.preset_browser.total_row_count == ENGINE_INSTRUMENT_PRESET_CATEGORY_COUNT,
           "instrument panel collapsed preset browser should show category headers only");
    expect(layout.preset_browser.row_count > 0 &&
               layout.preset_browser.rows[0].type == MIDI_PRESET_BROWSER_ROW_CATEGORY,
           "instrument panel preset browser should include category rows");

    SDL_Rect keys_category = {0};
    for (int i = 0; i < layout.preset_browser.row_count; ++i) {
        if (layout.preset_browser.rows[i].type == MIDI_PRESET_BROWSER_ROW_CATEGORY &&
            layout.preset_browser.rows[i].category == ENGINE_INSTRUMENT_PRESET_CATEGORY_KEYS) {
            keys_category = layout.preset_browser.rows[i].rect;
            break;
        }
    }
    expect(rect_has_positive_size(&keys_category),
           "instrument panel Keys category should be visible while collapsed");
    dispatch_instrument_mouse_button(&manager,
                                     &state,
                                     SDL_MOUSEBUTTONDOWN,
                                     keys_category.x + keys_category.w / 2,
                                     keys_category.y + keys_category.h / 2);
    expect(state.midi_editor_ui.instrument_menu_open,
           "instrument panel category header click should keep menu open");
    expect(state.midi_editor_ui.instrument_menu_expanded_category == ENGINE_INSTRUMENT_PRESET_CATEGORY_KEYS,
           "instrument panel Keys category should expand after click");
    midi_instrument_panel_compute_layout(&state, &layout);

    SDL_Rect warm_keys_item = layout.preset_menu_item_rects[ENGINE_INSTRUMENT_PRESET_WARM_KEYS];
    expect(rect_has_positive_size(&warm_keys_item),
           "instrument panel preset browser should expose Warm Keys after expanding Keys");

    dispatch_instrument_mouse_button(&manager,
                                     &state,
                                     SDL_MOUSEBUTTONDOWN,
                                     warm_keys_item.x + warm_keys_item.w / 2,
                                     warm_keys_item.y + warm_keys_item.h / 2);
    expect(!state.midi_editor_ui.instrument_menu_open,
           "instrument panel preset selection should close browser");

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_instrument_preset(clip) == ENGINE_INSTRUMENT_PRESET_WARM_KEYS,
           "instrument panel preset browser should set selected region preset");
    expect(undo_manager_undo(&state.undo, &state), "instrument panel preset browser undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    expect(engine_clip_midi_instrument_preset(clip) == ENGINE_INSTRUMENT_PRESET_PURE_SINE,
           "instrument panel preset browser undo should restore previous preset");

    state_destroy(&state);
}

static void test_midi_instrument_panel_param_knob_drag_sets_region_param(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for instrument param test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;

    state.midi_editor_ui.panel_mode = MIDI_REGION_PANEL_INSTRUMENT;
    MidiInstrumentPanelLayout layout = {0};
    midi_instrument_panel_compute_layout(&state, &layout);
    validate_instrument_panel_layout(&state, &layout);
    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    EngineInstrumentParams before_params = engine_clip_midi_instrument_params(clip);

    InputManager manager = {0};
    SDL_Rect level_knob = layout.param_knob_rects[ENGINE_INSTRUMENT_PARAM_LEVEL];
    dispatch_instrument_mouse_button(&manager,
                                     &state,
                                     SDL_MOUSEBUTTONDOWN,
                                     level_knob.x + level_knob.w / 2,
                                     level_knob.y + level_knob.h / 2);
    expect(state.midi_editor_ui.instrument_param_drag_active,
           "instrument param mouse down should start knob drag");
    dispatch_instrument_mouse_motion(&manager,
                                     &state,
                                     level_knob.x + level_knob.w / 2,
                                     level_knob.y + level_knob.h / 2 + 180);
    dispatch_instrument_mouse_button(&manager,
                                     &state,
                                     SDL_MOUSEBUTTONUP,
                                     level_knob.x + level_knob.w / 2,
                                     level_knob.y + level_knob.h / 2 + 180);
    expect(!state.midi_editor_ui.instrument_param_drag_active,
           "instrument param mouse up should stop knob drag");

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    EngineInstrumentParams params = engine_clip_midi_instrument_params(clip);
    expect(params.level < 0.05f, "instrument panel level knob should update region params");
    expect(engine_clip_midi_note_count(clip) == 0,
           "instrument panel param knob should not mutate MIDI notes");
    expect(undo_manager_undo(&state.undo, &state), "instrument panel param undo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    params = engine_clip_midi_instrument_params(clip);
    expect(params.level == before_params.level,
           "instrument panel param undo should restore previous value");
    expect(undo_manager_redo(&state.undo, &state), "instrument panel param redo failed");
    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    params = engine_clip_midi_instrument_params(clip);
    expect(params.level < 0.05f,
           "instrument panel param redo should restore dragged value");

    state_destroy(&state);
}

static void test_midi_instrument_panel_group_tab_routes_param_drag(void) {
    AppState state;
    EngineRuntimeConfig cfg;
    state_init(&state, &cfg);

    int clip_index = -1;
    expect(engine_add_midi_clip_to_track(state.engine, 0, 0, (uint64_t)cfg.sample_rate * 2u, &clip_index),
           "failed to create MIDI clip for instrument group test");
    expect(engine_clip_midi_set_instrument_preset(state.engine,
                                                  0,
                                                  clip_index,
                                                  ENGINE_INSTRUMENT_PRESET_SYNTH_LAB),
           "failed to set synth lab preset for instrument group test");
    state.selected_track_index = 0;
    state.selected_clip_index = clip_index;
    state.selection_count = 1;
    state.selection[0].track_index = 0;
    state.selection[0].clip_index = clip_index;
    state.midi_editor_ui.panel_mode = MIDI_REGION_PANEL_INSTRUMENT;

    MidiInstrumentPanelLayout layout = {0};
    midi_instrument_panel_compute_layout(&state, &layout);
    validate_instrument_panel_layout(&state, &layout);

    InputManager manager = {0};
    SDL_Rect osc_tab = layout.group_tab_rects[ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR];
    dispatch_instrument_mouse_button(&manager,
                                     &state,
                                     SDL_MOUSEBUTTONDOWN,
                                     osc_tab.x + osc_tab.w / 2,
                                     osc_tab.y + osc_tab.h / 2);
    midi_instrument_panel_compute_layout(&state, &layout);
    validate_instrument_panel_layout(&state, &layout);
    expect(layout.active_group == ENGINE_INSTRUMENT_PARAM_GROUP_OSCILLATOR,
           "instrument group tab should switch active group");
    expect(layout.instrument_param_count >= 3, "oscillator group should expose synth params");
    expect(layout.param_ids[0] == ENGINE_INSTRUMENT_PARAM_OSC_MIX,
           "oscillator group should route first knob to osc mix");

    const EngineTrack* tracks = engine_get_tracks(state.engine);
    const EngineClip* clip = &tracks[0].clips[clip_index];
    float before = engine_clip_midi_instrument_params(clip).osc_mix;
    SDL_Rect osc_knob = layout.param_knob_rects[0];
    dispatch_instrument_mouse_button(&manager,
                                     &state,
                                     SDL_MOUSEBUTTONDOWN,
                                     osc_knob.x + osc_knob.w / 2,
                                     osc_knob.y + osc_knob.h / 2);
    expect(state.midi_editor_ui.instrument_param_drag_active,
           "oscillator param mouse down should start knob drag");
    expect(state.midi_editor_ui.instrument_param_drag_index == ENGINE_INSTRUMENT_PARAM_OSC_MIX,
           "oscillator knob drag should store actual param id");
    dispatch_instrument_mouse_motion(&manager,
                                     &state,
                                     osc_knob.x + osc_knob.w / 2,
                                     osc_knob.y + osc_knob.h / 2 + 120);
    dispatch_instrument_mouse_button(&manager,
                                     &state,
                                     SDL_MOUSEBUTTONUP,
                                     osc_knob.x + osc_knob.w / 2,
                                     osc_knob.y + osc_knob.h / 2 + 120);

    tracks = engine_get_tracks(state.engine);
    clip = &tracks[0].clips[clip_index];
    EngineInstrumentParams params = engine_clip_midi_instrument_params(clip);
    expect(params.osc_mix < before, "oscillator group knob should update osc mix");
    expect(engine_clip_midi_note_count(clip) == 0,
           "instrument group param knob should not mutate MIDI notes");

    state_destroy(&state);
}

int main(void) {
    if (TTF_Init() != 0) {
        fail("TTF_Init failed");
    }
    expect(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf", 9),
           "ui_font_set failed");
    test_midi_selection_routes_editor_shell();
    test_midi_editor_create_delete_and_undo();
    test_midi_editor_time_ruler_click_seeks_transport();
    test_midi_editor_viewport_zoom_pan_maps_notes_and_hit_tests();
    test_midi_editor_pitch_viewport_state_is_clip_local();
    test_midi_editor_pitch_viewport_routes_edit_paths();
    test_midi_editor_pitch_scroll_zoom_gestures();
    test_midi_editor_pitch_fit_selected_notes();
    test_midi_editor_pitch_viewport_edge_cases_and_selection_stability();
    test_midi_editor_drag_moves_note();
    test_midi_editor_resize_requires_selected_note();
    test_midi_editor_shift_drag_edits_velocity();
    test_midi_editor_qwerty_records_timed_note();
    test_midi_editor_qwerty_octave_and_velocity_controls();
    test_midi_editor_quantize_button_snaps_selected_note();
    test_midi_editor_shift_click_toggles_multi_selection();
    test_midi_editor_shift_marquee_selects_notes();
    test_midi_editor_multiselect_quantize_and_delete();
    test_midi_editor_copy_paste_selected_notes();
    test_midi_editor_clipboard_shortcut_falls_through_without_note_selection();
    test_midi_editor_duplicate_selected_notes();
    test_midi_editor_note_command_shortcuts_edit_selection();
    test_midi_editor_multiselect_velocity_drag_updates_group();
    test_midi_editor_selected_group_click_collapses_on_release();
    test_midi_editor_selected_group_drag_moves_group_and_keeps_selection();
    test_midi_editor_snap_uses_editor_quantize_grid();
    test_midi_editor_qwerty_record_snaps_to_quantize_grid();
    test_midi_editor_qwerty_test_auditions_without_recording();
    test_midi_editor_instrument_dropdown_sets_region_preset();
    test_midi_editor_instrument_panel_button_swaps_subview();
    test_midi_instrument_panel_grouped_preset_browser_sets_region_preset();
    test_midi_instrument_panel_param_knob_drag_sets_region_param();
    test_midi_instrument_panel_group_tab_routes_param_drag();
    ui_font_shutdown();
    TTF_Quit();
    puts("midi_editor_shell_test: success");
    return 0;
}
