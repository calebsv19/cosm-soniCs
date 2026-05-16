#pragma once

#include "engine/engine.h"
#include "ui/midi_preset_browser.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

struct AppState;

#define MIDI_EDITOR_VISIBLE_KEY_ROWS 24

typedef struct {
    int track_index;
    int clip_index;
    const EngineTrack* track;
    const EngineClip* clip;
} MidiEditorSelection;

typedef struct {
    SDL_Rect panel_rect;
    SDL_Rect header_rect;
    SDL_Rect title_rect;
    SDL_Rect instrument_button_rect;
    SDL_Rect instrument_panel_button_rect;
    SDL_Rect instrument_menu_rect;
    SDL_Rect instrument_menu_item_rects[ENGINE_INSTRUMENT_PRESET_COUNT];
    MidiPresetBrowserLayout instrument_browser;
    SDL_Rect test_button_rect;
    SDL_Rect quantize_button_rect;
    SDL_Rect quantize_down_button_rect;
    SDL_Rect quantize_up_button_rect;
    SDL_Rect octave_down_button_rect;
    SDL_Rect octave_up_button_rect;
    SDL_Rect velocity_down_button_rect;
    SDL_Rect velocity_up_button_rect;
    SDL_Rect summary_rect;
    SDL_Rect param_strip_rect;
    SDL_Rect param_widget_rects[ENGINE_INSTRUMENT_PARAM_COUNT];
    SDL_Rect param_label_rects[ENGINE_INSTRUMENT_PARAM_COUNT];
    SDL_Rect param_slider_rects[ENGINE_INSTRUMENT_PARAM_COUNT];
    SDL_Rect param_value_rects[ENGINE_INSTRUMENT_PARAM_COUNT];
    SDL_Rect body_rect;
    SDL_Rect time_ruler_rect;
    SDL_Rect piano_rect;
    SDL_Rect grid_rect;
    SDL_Rect footer_rect;
    SDL_Rect key_label_rects[MIDI_EDITOR_VISIBLE_KEY_ROWS];
    SDL_Rect key_lane_rects[MIDI_EDITOR_VISIBLE_KEY_ROWS];
    int key_row_count;
    int instrument_menu_item_count;
    int instrument_param_count;
    int highest_note;
    int lowest_note;
    uint64_t view_start_frame;
    uint64_t view_end_frame;
    uint64_t view_span_frames;
} MidiEditorLayout;

typedef enum {
    MIDI_EDITOR_NOTE_HIT_NONE = 0,
    MIDI_EDITOR_NOTE_HIT_BODY,
    MIDI_EDITOR_NOTE_HIT_LEFT_EDGE,
    MIDI_EDITOR_NOTE_HIT_RIGHT_EDGE
} MidiEditorNoteHitPart;

typedef struct {
    int note_index;
    SDL_Rect rect;
    MidiEditorNoteHitPart part;
} MidiEditorNoteHit;

bool midi_editor_get_selection(const struct AppState* state, MidiEditorSelection* out_selection);
bool midi_editor_should_render(const struct AppState* state);
void midi_editor_compute_layout(const struct AppState* state, MidiEditorLayout* layout);
void midi_editor_store_pitch_viewport(struct AppState* state,
                                      const MidiEditorSelection* selection,
                                      int top_note,
                                      int row_count);
void midi_editor_fit_pitch_viewport(struct AppState* state,
                                    const MidiEditorSelection* selection,
                                    int row_count);
bool midi_editor_resolve_pitch_viewport(const struct AppState* state,
                                        const MidiEditorSelection* selection,
                                        int row_capacity,
                                        int* out_top_note,
                                        int* out_row_count);
bool midi_editor_pitch_note_to_row(const MidiEditorLayout* layout, int note, int* out_row);
bool midi_editor_pitch_row_to_note(const MidiEditorLayout* layout, int row, uint8_t* out_note);
bool midi_editor_note_rect(const MidiEditorLayout* layout,
                           const EngineMidiNote* note,
                           uint64_t clip_frames,
                           SDL_Rect* out_rect);
bool midi_editor_hit_test_note(const MidiEditorLayout* layout,
                               const EngineClip* clip,
                               int x,
                               int y,
                               MidiEditorNoteHit* out_hit);
bool midi_editor_point_to_frame(const MidiEditorLayout* layout,
                                uint64_t clip_frames,
                                int x,
                                uint64_t* out_frame);
bool midi_editor_point_to_note_frame(const MidiEditorLayout* layout,
                                     uint64_t clip_frames,
                                     int x,
                                     int y,
                                     uint8_t* out_note,
                                     uint64_t* out_frame);
void midi_editor_render(SDL_Renderer* renderer, const struct AppState* state, const MidiEditorLayout* layout);
