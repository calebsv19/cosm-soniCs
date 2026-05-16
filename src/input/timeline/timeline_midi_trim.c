#include "input/timeline/timeline_midi_trim.h"

#include "engine/midi.h"

#include <stdlib.h>
#include <string.h>

static bool note_end_frame(const EngineMidiNote* note, uint64_t* out_end) {
    if (!note || !out_end || UINT64_MAX - note->start_frame < note->duration_frames) {
        return false;
    }
    *out_end = note->start_frame + note->duration_frames;
    return true;
}

static bool timeline_midi_left_trim_build_from_notes(uint64_t old_start,
                                                     uint64_t old_duration,
                                                     const EngineMidiNote* src_notes,
                                                     int src_count,
                                                     uint64_t new_start_frames,
                                                     TimelineMidiLeftTrimResult* out_result);

void timeline_midi_left_trim_result_clear(TimelineMidiLeftTrimResult* result) {
    if (!result) {
        return;
    }
    free(result->notes);
    result->notes = NULL;
    result->note_count = 0;
}

bool timeline_midi_left_trim_build(const EngineClip* clip,
                                   uint64_t new_start_frames,
                                   TimelineMidiLeftTrimResult* out_result) {
    if (!clip || !out_result || engine_clip_get_kind(clip) != ENGINE_CLIP_KIND_MIDI) {
        return false;
    }
    return timeline_midi_left_trim_build_from_notes(clip->timeline_start_frames,
                                                   clip->duration_frames,
                                                   engine_clip_midi_notes(clip),
                                                   engine_clip_midi_note_count(clip),
                                                   new_start_frames,
                                                   out_result);
}

static bool timeline_midi_left_trim_build_from_notes(uint64_t old_start,
                                                     uint64_t old_duration,
                                                     const EngineMidiNote* src_notes,
                                                     int src_count,
                                                     uint64_t new_start_frames,
                                                     TimelineMidiLeftTrimResult* out_result) {
    if (!out_result || old_duration == 0 ||
        src_count < 0 || src_count > ENGINE_MIDI_NOTE_CAP ||
        (src_count > 0 && !src_notes)) {
        return false;
    }
    memset(out_result, 0, sizeof(*out_result));
    out_result->timeline_start_frames = new_start_frames;
    if (src_count > 0) {
        out_result->notes = (EngineMidiNote*)calloc((size_t)src_count, sizeof(EngineMidiNote));
        if (!out_result->notes) {
            return false;
        }
    }

    if (new_start_frames >= old_start) {
        const uint64_t delta = new_start_frames - old_start;
        if (delta >= old_duration) {
            timeline_midi_left_trim_result_clear(out_result);
            return false;
        }
        out_result->duration_frames = old_duration - delta;
        for (int i = 0; i < src_count; ++i) {
            uint64_t end = 0;
            if (!note_end_frame(&src_notes[i], &end)) {
                timeline_midi_left_trim_result_clear(out_result);
                return false;
            }
            if (end <= delta) {
                continue;
            }
            EngineMidiNote dst = src_notes[i];
            if (dst.start_frame < delta) {
                dst.start_frame = 0;
                dst.duration_frames = end - delta;
            } else {
                dst.start_frame -= delta;
            }
            if (dst.duration_frames == 0 ||
                UINT64_MAX - dst.start_frame < dst.duration_frames ||
                dst.start_frame + dst.duration_frames > out_result->duration_frames) {
                timeline_midi_left_trim_result_clear(out_result);
                return false;
            }
            out_result->notes[out_result->note_count++] = dst;
        }
        return true;
    }

    const uint64_t delta = old_start - new_start_frames;
    if (UINT64_MAX - old_duration < delta) {
        timeline_midi_left_trim_result_clear(out_result);
        return false;
    }
    out_result->duration_frames = old_duration + delta;
    for (int i = 0; i < src_count; ++i) {
        EngineMidiNote dst = src_notes[i];
        if (UINT64_MAX - dst.start_frame < delta) {
            timeline_midi_left_trim_result_clear(out_result);
            return false;
        }
        dst.start_frame += delta;
        uint64_t end = 0;
        if (!note_end_frame(&dst, &end) || end > out_result->duration_frames) {
            timeline_midi_left_trim_result_clear(out_result);
            return false;
        }
        out_result->notes[out_result->note_count++] = dst;
    }
    return true;
}

bool timeline_midi_left_trim_apply(Engine* engine,
                                   int track_index,
                                   int* inout_clip_index,
                                   uint64_t new_start_frames) {
    if (!engine || !inout_clip_index || *inout_clip_index < 0) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(engine);
    int track_count = engine_get_track_count(engine);
    if (!tracks || track_index < 0 || track_index >= track_count) {
        return false;
    }
    const EngineTrack* track = &tracks[track_index];
    int clip_index = *inout_clip_index;
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }

    TimelineMidiLeftTrimResult trim = {0};
    if (!timeline_midi_left_trim_build(&track->clips[clip_index], new_start_frames, &trim)) {
        return false;
    }

    bool ok = true;
    uint64_t current_duration = track->clips[clip_index].duration_frames;
    if (trim.duration_frames < current_duration) {
        ok = engine_clip_midi_set_notes(engine, track_index, clip_index, trim.notes, trim.note_count) &&
             engine_clip_set_region(engine, track_index, clip_index, 0, trim.duration_frames);
    } else {
        ok = engine_clip_set_region(engine, track_index, clip_index, 0, trim.duration_frames) &&
             engine_clip_midi_set_notes(engine, track_index, clip_index, trim.notes, trim.note_count);
    }
    if (ok) {
        int new_index = clip_index;
        ok = engine_clip_set_timeline_start(engine, track_index, clip_index, trim.timeline_start_frames, &new_index);
        if (ok) {
            *inout_clip_index = new_index;
        }
    }
    timeline_midi_left_trim_result_clear(&trim);
    return ok;
}

bool timeline_midi_left_trim_apply_from_notes(Engine* engine,
                                              int track_index,
                                              int* inout_clip_index,
                                              uint64_t initial_start_frames,
                                              uint64_t initial_duration_frames,
                                              const EngineMidiNote* initial_notes,
                                              int initial_note_count,
                                              uint64_t new_start_frames) {
    if (!engine || !inout_clip_index || *inout_clip_index < 0) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(engine);
    int track_count = engine_get_track_count(engine);
    if (!tracks || track_index < 0 || track_index >= track_count) {
        return false;
    }
    const EngineTrack* track = &tracks[track_index];
    int clip_index = *inout_clip_index;
    if (!track || clip_index < 0 || clip_index >= track->clip_count ||
        engine_clip_get_kind(&track->clips[clip_index]) != ENGINE_CLIP_KIND_MIDI) {
        return false;
    }

    TimelineMidiLeftTrimResult trim = {0};
    if (!timeline_midi_left_trim_build_from_notes(initial_start_frames,
                                                 initial_duration_frames,
                                                 initial_notes,
                                                 initial_note_count,
                                                 new_start_frames,
                                                 &trim)) {
        return false;
    }

    bool ok = true;
    uint64_t current_duration = track->clips[clip_index].duration_frames;
    if (trim.duration_frames < current_duration) {
        ok = engine_clip_midi_set_notes(engine, track_index, clip_index, trim.notes, trim.note_count) &&
             engine_clip_set_region(engine, track_index, clip_index, 0, trim.duration_frames);
    } else {
        ok = engine_clip_set_region(engine, track_index, clip_index, 0, trim.duration_frames) &&
             engine_clip_midi_set_notes(engine, track_index, clip_index, trim.notes, trim.note_count);
    }
    if (ok) {
        int new_index = clip_index;
        ok = engine_clip_set_timeline_start(engine, track_index, clip_index, trim.timeline_start_frames, &new_index);
        if (ok) {
            *inout_clip_index = new_index;
        }
    }
    timeline_midi_left_trim_result_clear(&trim);
    return ok;
}
