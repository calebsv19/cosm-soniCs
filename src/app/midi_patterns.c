#include "daw/midi_patterns.h"
#include <math.h>
#include <limits.h>
#include <string.h>
// Converts a captured region-relative quarter-note beat to a checked absolute timeline frame.
static bool frame(const DawMidiSnapshot* snapshot, double origin, double beat, uint64_t* out) {
    double value = tempo_map_beats_to_samples(&snapshot->tempo, origin + beat);
    if (!isfinite(value) || value < 0 || value >= (double)INT64_MAX) return false;
    uint64_t absolute = (uint64_t)llround(value);
    if (absolute < snapshot->start_frame) return false;
    *out = absolute - snapshot->start_frame; return true;
}
// Generates bounded major-key patterns without rounding a constant beat length across tempo changes.
DawMidiResult daw_midi_pattern_build(const DawMidiSnapshot* snapshot, DawMidiPattern pattern, DawMidiPatternNotes* out) {
    DawMidiResult invalid = {DAW_MIDI_INVALID, "Pattern needs four quarter-note beats within this region and valid pitch/timing.", 0};
    if (!snapshot || !out || snapshot->start_frame > INT64_MAX || snapshot->offset_frames != 0 ||
        !isfinite(pattern.start_beat) || pattern.start_beat < 0 || pattern.start_beat > 1000000 ||
        !isfinite(pattern.velocity) || pattern.velocity <= 0 || pattern.velocity > 1 ||
        pattern.root_note < 0 || pattern.root_note > 127 || pattern.kind < DAW_MIDI_MELODY || pattern.kind > DAW_MIDI_ARPEGGIO ||
        snapshot->tempo.event_count <= 0 || !snapshot->tempo.events || !isfinite(snapshot->tempo.sample_rate) || snapshot->tempo.sample_rate <= 0) return invalid;
    DawMidiPatternNotes next = {0};
    double origin = tempo_map_samples_to_beats(&snapshot->tempo, (int64_t)snapshot->start_frame);
    uint64_t end;
    if (!isfinite(origin) || !frame(snapshot, origin, pattern.start_beat + 4, &end) || end > snapshot->duration_frames) return invalid;
    const int melody[] = {0, 4, 7, 9, 7, 4, 2, 0};
    const int arpeggio[] = {0, 4, 7, 12, 7, 4, 2, 7};
    const int chords[][3] = {{0, 4, 7}, {9, 12, 16}, {5, 9, 12}, {7, 11, 14}};
    int steps = pattern.kind == DAW_MIDI_CHORDS ? 4 : 8;
    for (int i = 0; i < steps; ++i) {
        double step = pattern.kind == DAW_MIDI_CHORDS ? 1 : .5;
        uint64_t begin, finish;
        if (!frame(snapshot, origin, pattern.start_beat + i * step, &begin) ||
            !frame(snapshot, origin, pattern.start_beat + (i + .85) * step, &finish) || finish <= begin || finish > snapshot->duration_frames) return invalid;
        for (int voice = 0; voice < (pattern.kind == DAW_MIDI_CHORDS ? 3 : 1); ++voice) {
            int pitch = pattern.root_note + (pattern.kind == DAW_MIDI_CHORDS ? chords[i][voice] : pattern.kind == DAW_MIDI_MELODY ? melody[i] : arpeggio[i]);
            if (pitch > ENGINE_MIDI_NOTE_MAX) return invalid;
            next.notes[next.count++] = (EngineMidiNote){begin, finish - begin, (uint8_t)pitch, pattern.velocity};
        }
    }
    *out = next;
    return (DawMidiResult){DAW_MIDI_UNCHANGED, "Four-beat major-key pattern prepared; project unchanged.", next.count};
}
