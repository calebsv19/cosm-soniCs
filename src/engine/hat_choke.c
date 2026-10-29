#include "engine/hat_choke.h"
#include "engine/engine_internal.h"
#include "engine/instrument_percussion.h"
#include <math.h>
#include <stdlib.h>

// Orders simultaneous events by persistent region identity and original authored note index.
static int event_compare(const void* left, const void* right) {
    const EngineHatChokeEvent* a = left; const EngineHatChokeEvent* b = right;
    if (a->frame != b->frame) return a->frame < b->frame ? -1 : 1;
    if (a->clip_id != b->clip_id) return a->clip_id < b->clip_id ? -1 : 1;
    return (a->note_index > b->note_index) - (a->note_index < b->note_index);
}

// Restricts participation to enabled, active hat regions with valid instrument settings.
static bool participates(const Engine* engine, int track, int clip) {
    const EngineClip* region = &engine->tracks[track].clips[clip];
    EngineInstrumentPresetId preset = engine_clip_midi_effective_instrument_preset(engine, track, clip);
    EngineInstrumentParams params = engine_clip_midi_effective_instrument_params(engine, track, clip);
    return region->active && region->kind == ENGINE_CLIP_KIND_MIDI &&
        engine_instrument_percussion_hat(preset) && params.hat_choke >= .5f;
}

// Rejects silent triggers, region-external notes and overflowing absolute onsets.
static bool valid_onset(const EngineClip* clip, const EngineMidiNote* note, uint64_t end) {
    return note->duration_frames && isfinite(note->velocity) && note->velocity > 0 &&
        note->start_frame < clip->duration_frames &&
        note->start_frame <= UINT64_MAX - clip->timeline_start_frames &&
        clip->timeline_start_frames + note->start_frame < end;
}

// Allocates once off the render thread and sorts only meaningful track-local onsets.
bool engine_hat_choke_prepare(EngineHatChokeTimeline* timeline, const Engine* engine, int track, uint64_t end) {
    if (!timeline || !engine || track < 0 || track >= engine->track_count) return false;
    *timeline = (EngineHatChokeTimeline){0};
    const EngineTrack* lane = &engine->tracks[track];
    for (int c = 0; c < lane->clip_count; ++c) if (participates(engine, track, c)) {
        const EngineClip* clip = &lane->clips[c];
        for (int n = 0; n < clip->midi_notes.note_count; ++n)
            if (valid_onset(clip, &clip->midi_notes.notes[n], end)) {
                if (timeline->count == SIZE_MAX / sizeof(*timeline->events)) return false;
                ++timeline->count;
            }
    }
    if (!timeline->count) return true;
    timeline->events = malloc(timeline->count * sizeof(*timeline->events));
    if (!timeline->events) return false;
    size_t at = 0;
    for (int c = 0; c < lane->clip_count; ++c) if (participates(engine, track, c)) {
        const EngineClip* clip = &lane->clips[c];
        for (int n = 0; n < clip->midi_notes.note_count; ++n)
            if (valid_onset(clip, &clip->midi_notes.notes[n], end))
                timeline->events[at++] = (EngineHatChokeEvent){clip->timeline_start_frames + clip->midi_notes.notes[n].start_frame, clip->creation_index, n};
    }
    qsort(timeline->events, timeline->count, sizeof(*timeline->events), event_compare);
    return true;
}

// Uses upper-bound lookup so later simultaneous authored hits silence earlier hits deterministically.
bool engine_hat_choke_apply(const EngineHatChokeTimeline* timeline, const EngineClip* clip, EngineInstrumentSource* source) {
    if (!timeline || !clip || !source || clip->midi_notes.note_count > ENGINE_MIDI_NOTE_CAP) return false;
    uint64_t fences[ENGINE_MIDI_NOTE_CAP];
    for (int n = 0; n < clip->midi_notes.note_count; ++n) {
        fences[n] = UINT64_MAX;
        const EngineMidiNote* note = &clip->midi_notes.notes[n];
        if (!valid_onset(clip, note, UINT64_MAX)) continue;
        EngineHatChokeEvent key = {clip->timeline_start_frames + note->start_frame, clip->creation_index, n};
        size_t low = 0, high = timeline->count;
        while (low < high) {
            size_t middle = low + (high - low) / 2;
            if (event_compare(&timeline->events[middle], &key) <= 0) low = middle + 1;
            else high = middle;
        }
        if (low < timeline->count) fences[n] = timeline->events[low].frame - clip->timeline_start_frames;
    }
    return engine_instrument_source_set_hat_chokes(source, fences, clip->midi_notes.note_count);
}

// Frees only this task's transient preparation storage, including failed allocations.
void engine_hat_choke_free(EngineHatChokeTimeline* timeline) {
    if (!timeline) return;
    free(timeline->events);
    *timeline = (EngineHatChokeTimeline){0};
}
