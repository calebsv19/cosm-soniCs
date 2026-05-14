#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ENGINE_MIDI_NOTE_MIN 0
#define ENGINE_MIDI_NOTE_MAX 127
#define ENGINE_MIDI_NOTE_CAP 4096

// Stores one MIDI note event using frame-relative timing and normalized velocity.
typedef struct {
    uint64_t start_frame;
    uint64_t duration_frames;
    uint8_t note;
    float velocity;
} EngineMidiNote;

// Owns the mutable note list for a MIDI clip.
typedef struct {
    EngineMidiNote* notes;
    int note_count;
    int note_capacity;
} EngineMidiNoteList;

// Resets a MIDI note list to an empty owned state.
void engine_midi_note_list_init(EngineMidiNoteList* list);
// Releases memory owned by a MIDI note list.
void engine_midi_note_list_free(EngineMidiNoteList* list);
// Returns whether a note event is valid for insertion into a MIDI clip.
bool engine_midi_note_is_valid(const EngineMidiNote* note);
// Replaces all notes in the list with a sorted copy of the provided notes.
bool engine_midi_note_list_set(EngineMidiNoteList* list, const EngineMidiNote* notes, int count);
// Inserts a note into sorted order and optionally returns its final index.
bool engine_midi_note_list_insert(EngineMidiNoteList* list, EngineMidiNote note, int* out_index);
// Updates an existing note and optionally returns its final index after sorting.
bool engine_midi_note_list_update(EngineMidiNoteList* list, int note_index, EngineMidiNote note, int* out_index);
// Removes one note by index.
bool engine_midi_note_list_remove(EngineMidiNoteList* list, int note_index);
// Returns true when every note in the list is valid and sorted.
bool engine_midi_note_list_validate(const EngineMidiNoteList* list);
