#include "engine/midi.h"

#include <stdlib.h>
#include <string.h>

static int engine_midi_note_compare(const void* a, const void* b) {
    const EngineMidiNote* na = (const EngineMidiNote*)a;
    const EngineMidiNote* nb = (const EngineMidiNote*)b;
    if (na->start_frame < nb->start_frame) {
        return -1;
    }
    if (na->start_frame > nb->start_frame) {
        return 1;
    }
    if (na->note < nb->note) {
        return -1;
    }
    if (na->note > nb->note) {
        return 1;
    }
    if (na->duration_frames < nb->duration_frames) {
        return -1;
    }
    if (na->duration_frames > nb->duration_frames) {
        return 1;
    }
    return 0;
}

static void engine_midi_note_list_sort(EngineMidiNoteList* list) {
    if (!list || list->note_count <= 1 || !list->notes) {
        return;
    }
    qsort(list->notes, (size_t)list->note_count, sizeof(EngineMidiNote), engine_midi_note_compare);
}

static bool engine_midi_note_list_ensure_capacity(EngineMidiNoteList* list, int needed) {
    if (!list || needed < 0 || needed > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    if (list->note_capacity >= needed) {
        return true;
    }
    int new_capacity = list->note_capacity == 0 ? 8 : list->note_capacity * 2;
    if (new_capacity < needed) {
        new_capacity = needed;
    }
    if (new_capacity > ENGINE_MIDI_NOTE_CAP) {
        new_capacity = ENGINE_MIDI_NOTE_CAP;
    }
    EngineMidiNote* resized = (EngineMidiNote*)realloc(list->notes, sizeof(EngineMidiNote) * (size_t)new_capacity);
    if (!resized) {
        return false;
    }
    list->notes = resized;
    list->note_capacity = new_capacity;
    return true;
}

void engine_midi_note_list_init(EngineMidiNoteList* list) {
    if (!list) {
        return;
    }
    list->notes = NULL;
    list->note_count = 0;
    list->note_capacity = 0;
}

void engine_midi_note_list_free(EngineMidiNoteList* list) {
    if (!list) {
        return;
    }
    free(list->notes);
    list->notes = NULL;
    list->note_count = 0;
    list->note_capacity = 0;
}

bool engine_midi_note_is_valid(const EngineMidiNote* note) {
    if (!note) {
        return false;
    }
    if (note->duration_frames == 0) {
        return false;
    }
    if (note->note > ENGINE_MIDI_NOTE_MAX) {
        return false;
    }
    if (note->velocity < 0.0f || note->velocity > 1.0f) {
        return false;
    }
    return true;
}

bool engine_midi_note_list_set(EngineMidiNoteList* list, const EngineMidiNote* notes, int count) {
    if (!list || count < 0 || count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    if (count > 0 && !notes) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (!engine_midi_note_is_valid(&notes[i])) {
            return false;
        }
    }
    if (count == 0) {
        list->note_count = 0;
        return true;
    }
    if (!engine_midi_note_list_ensure_capacity(list, count)) {
        return false;
    }
    memcpy(list->notes, notes, sizeof(EngineMidiNote) * (size_t)count);
    list->note_count = count;
    engine_midi_note_list_sort(list);
    return true;
}

bool engine_midi_note_list_insert(EngineMidiNoteList* list, EngineMidiNote note, int* out_index) {
    if (!list || !engine_midi_note_is_valid(&note)) {
        return false;
    }
    if (!engine_midi_note_list_ensure_capacity(list, list->note_count + 1)) {
        return false;
    }
    list->notes[list->note_count++] = note;
    engine_midi_note_list_sort(list);
    if (out_index) {
        *out_index = -1;
        for (int i = 0; i < list->note_count; ++i) {
            if (list->notes[i].start_frame == note.start_frame &&
                list->notes[i].duration_frames == note.duration_frames &&
                list->notes[i].note == note.note &&
                list->notes[i].velocity == note.velocity) {
                *out_index = i;
                break;
            }
        }
    }
    return true;
}

bool engine_midi_note_list_update(EngineMidiNoteList* list, int note_index, EngineMidiNote note, int* out_index) {
    if (!list || note_index < 0 || note_index >= list->note_count || !engine_midi_note_is_valid(&note)) {
        return false;
    }
    list->notes[note_index] = note;
    engine_midi_note_list_sort(list);
    if (out_index) {
        *out_index = -1;
        for (int i = 0; i < list->note_count; ++i) {
            if (list->notes[i].start_frame == note.start_frame &&
                list->notes[i].duration_frames == note.duration_frames &&
                list->notes[i].note == note.note &&
                list->notes[i].velocity == note.velocity) {
                *out_index = i;
                break;
            }
        }
    }
    return true;
}

bool engine_midi_note_list_remove(EngineMidiNoteList* list, int note_index) {
    if (!list || note_index < 0 || note_index >= list->note_count) {
        return false;
    }
    int remaining = list->note_count - note_index - 1;
    if (remaining > 0) {
        memmove(&list->notes[note_index],
                &list->notes[note_index + 1],
                sizeof(EngineMidiNote) * (size_t)remaining);
    }
    list->note_count--;
    return true;
}

bool engine_midi_note_list_validate(const EngineMidiNoteList* list) {
    if (!list) {
        return false;
    }
    if (list->note_count < 0 || list->note_count > ENGINE_MIDI_NOTE_CAP) {
        return false;
    }
    if (list->note_count > 0 && !list->notes) {
        return false;
    }
    for (int i = 0; i < list->note_count; ++i) {
        if (!engine_midi_note_is_valid(&list->notes[i])) {
            return false;
        }
        if (i > 0 && engine_midi_note_compare(&list->notes[i - 1], &list->notes[i]) > 0) {
            return false;
        }
    }
    return true;
}
