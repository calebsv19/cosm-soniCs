#include "engine/engine_internal.h"
#include "engine/engine_clips_automation_internal.h"
#include "engine/instrument.h"
#include "engine/sampler.h"
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

// Owns independently editable clip content for one stable track identity.
typedef struct {
    uint64_t track_id;
    int generated_index;
    EngineClip* clips;
    int count;
    bool active;
    bool instrument_enabled;
    EngineInstrumentPresetId instrument_preset;
    EngineInstrumentParams instrument_params;
} ClipHistoryTrack;

// Retains decoded media and clip metadata until released or invalidated with its owning project.
struct EngineClipContentSnapshot {
    EngineTrack* whole_track; // Owns complete track data for topology undo.
    EffectsManager* whole_fx; // Owns the retained track chain at index zero.
    Engine* owner;
    unsigned references;
    bool has_track_topology;
    int restore_track_count;
    int generated_start;
    uint64_t* inserted_ids;
    int inserted_count;
    ClipHistoryTrack* tracks;
    int count;
    struct EngineClipContentSnapshot* next;
};

// Copies clip-owned metadata and source controls while retaining the existing decoded media version.
static bool history_clone_clip(Engine* engine, const EngineClip* source, EngineClip* out) {
    *out = *source;
    out->media = NULL; out->sampler = NULL; out->instrument = NULL;
    out->midi_notes = (EngineMidiNoteList){0};
    out->automation_lanes = NULL; out->automation_lane_count = out->automation_lane_capacity = 0;
    if (source->media) {
        if (!audio_media_cache_retain(&engine->media_cache, source->media)) goto fail;
        out->media = source->media;
    }
    if (source->sampler) {
        out->sampler = engine_sampler_source_clone(source->sampler);
        if (!out->sampler) goto fail;
    }
    if (!engine_midi_note_list_set(&out->midi_notes, source->midi_notes.notes, source->midi_notes.note_count) ||
        !engine_clip_copy_automation(source, out)) goto fail;
    if (source->kind == ENGINE_CLIP_KIND_MIDI) {
        out->instrument = engine_instrument_source_create();
        if (!out->instrument || !engine_instrument_source_set_midi_clip(out->instrument,
            out->timeline_start_frames, out->duration_frames, out->instrument_preset, out->instrument_params,
            out->midi_notes.notes, out->midi_notes.note_count, NULL, 0,
            out->automation_lanes, out->automation_lane_count)) goto fail;
    }
    return true;
fail:
    engine_clip_destroy(engine, out);
    return false;
}

// Releases all owned clip content while the project's media cache still exists.
static void history_clear_rows(EngineClipContentSnapshot* snapshot) {
    if (snapshot->whole_track) {
        engine_track_clear(snapshot->owner, snapshot->whole_track);
        free(snapshot->whole_track);
        snapshot->whole_track = NULL;
    }
    fxm_destroy(snapshot->whole_fx); snapshot->whole_fx = NULL;
    for (int t = 0; t < snapshot->count; ++t) {
        ClipHistoryTrack* row = &snapshot->tracks[t];
        for (int c = 0; c < row->count; ++c) engine_clip_destroy(snapshot->owner, &row->clips[c]);
        free(row->clips);
    }
    free(snapshot->tracks); snapshot->tracks = NULL; snapshot->count = 0;
    free(snapshot->inserted_ids); snapshot->inserted_ids = NULL; snapshot->inserted_count = 0;
}

// Captures every clip on the requested tracks, rejecting duplicate or missing track targets.
EngineClipContentSnapshot* engine_clip_content_capture(Engine* engine, const int* tracks, int count) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || !tracks || count <= 0) return NULL;
    EngineClipContentSnapshot* snapshot = calloc(1, sizeof(*snapshot));
    if (!snapshot) return NULL;
    snapshot->owner = engine; snapshot->references = 1;
    snapshot->tracks = calloc((size_t)count, sizeof(*snapshot->tracks));
    if (!snapshot->tracks) { free(snapshot); return NULL; }
    snapshot->count = count;
    for (int i = 0; i < count; ++i) {
        if (tracks[i] < 0 || tracks[i] >= engine->track_count) goto fail;
        for (int j = 0; j < i; ++j) if (tracks[i] == tracks[j]) goto fail;
        const EngineTrack* source = &engine->tracks[tracks[i]];
        ClipHistoryTrack* row = &snapshot->tracks[i];
        row->track_id = source->runtime_id; row->generated_index = -1; row->active = source->active;
        row->instrument_enabled = source->midi_instrument_enabled;
        row->instrument_preset = source->midi_instrument_preset; row->instrument_params = source->midi_instrument_params;
        if (source->clip_count) {
            row->clips = calloc((size_t)source->clip_count, sizeof(*row->clips));
            if (!row->clips) goto fail;
        }
        for (int c = 0; c < source->clip_count; ++c) {
            if (!history_clone_clip(engine, &source->clips[c], &row->clips[c])) goto fail;
            row->count++;
        }
    }
    snapshot->next = engine->clip_history_snapshots; engine->clip_history_snapshots = snapshot;
    return snapshot;
fail:
    history_clear_rows(snapshot); free(snapshot); return NULL;
}

// Shares an immutable history snapshot without duplicating its media pins.
bool engine_clip_content_retain(EngineClipContentSnapshot* snapshot) {
    if (!snapshot || !snapshot->owner || snapshot->references == UINT_MAX) return false;
    snapshot->references++; return true;
}

// Releases history on the control thread; project-invalidated handles remain safe to release.
void engine_clip_content_release(EngineClipContentSnapshot* snapshot) {
    if (!snapshot || --snapshot->references) return;
    if (snapshot->owner) {
        EngineClipContentSnapshot** link = &snapshot->owner->clip_history_snapshots;
        while (*link && *link != snapshot) link = &(*link)->next;
        if (*link) *link = snapshot->next;
        history_clear_rows(snapshot);
    }
    free(snapshot);
}

// Invalidates outstanding history before media-cache shutdown without leaving dangling owner pointers.
void engine_clip_history_invalidate(Engine* engine) {
    EngineClipContentSnapshot* snapshot = engine->clip_history_snapshots;
    engine->clip_history_snapshots = NULL;
    while (snapshot) {
        EngineClipContentSnapshot* next = snapshot->next;
        history_clear_rows(snapshot); snapshot->owner = NULL; snapshot->next = NULL;
        snapshot = next;
    }
}

// Restores complete affected-track content with one publication and no source-file reopening.
bool engine_clip_content_restore(Engine* engine, const EngineClipContentSnapshot* snapshot) {
    if (!engine || !snapshot || snapshot->owner != engine || SDL_ThreadID() != engine->control_thread_id) return false;
    if (snapshot->whole_track) return false;
    int original_count = engine->track_count;
    int target_count = snapshot->has_track_topology ? snapshot->restore_track_count : original_count;
    if (target_count > original_count) {
        if (original_count != snapshot->generated_start || !engine_get_track_mutable(engine, target_count - 1)) return false;
        for (int i = 0; i < snapshot->count; ++i) {
            const ClipHistoryTrack* row = &snapshot->tracks[i];
            if (row->generated_index >= original_count && row->generated_index < target_count)
                engine->tracks[row->generated_index].runtime_id = row->track_id;
        }
    }
    int count = snapshot->count;
    EngineTrack* previous = calloc((size_t)count, sizeof(*previous));
    EngineTrack* candidate = calloc((size_t)count, sizeof(*candidate));
    int* indices = calloc((size_t)count, sizeof(*indices));
    bool accepted = false;
    if (!previous || !candidate || !indices) goto done;
    for (int i = 0; i < count; ++i) {
        const ClipHistoryTrack* row = &snapshot->tracks[i];
        indices[i] = -1;
        for (int t = 0; t < engine->track_count; ++t)
            if (engine->tracks[t].runtime_id == row->track_id) { indices[i] = t; break; }
        if (indices[i] < 0) goto done;
        previous[i] = engine->tracks[indices[i]];
        candidate[i] = previous[i]; candidate[i].clips = NULL;
        candidate[i].clip_count = 0; candidate[i].clip_capacity = row->count; candidate[i].active = row->active;
        candidate[i].midi_instrument_enabled = row->instrument_enabled;
        candidate[i].midi_instrument_preset = row->instrument_preset; candidate[i].midi_instrument_params = row->instrument_params;
        if (row->count) {
            candidate[i].clips = calloc((size_t)row->count, sizeof(EngineClip));
            if (!candidate[i].clips) goto done;
        }
        for (int c = 0; c < row->count; ++c) {
            if (!history_clone_clip(engine, &row->clips[c], &candidate[i].clips[c])) goto done;
            candidate[i].clip_count++;
        }
    }
    // A clip moved outside the captured rows must not be duplicated by restoring its old row.
    for (int t = 0; t < engine->track_count; ++t) {
        bool captured = false;
        for (int i = 0; i < count; ++i) if (indices[i] == t) { captured = true; break; }
        if (captured) continue;
        for (int c = 0; c < engine->tracks[t].clip_count; ++c)
            for (int i = 0; i < snapshot->inserted_count; ++i)
                if (engine->tracks[t].clips[c].creation_index == snapshot->inserted_ids[i]) goto done;
        for (int c = 0; c < engine->tracks[t].clip_count; ++c)
            for (int i = 0; i < count; ++i)
                for (int k = 0; k < candidate[i].clip_count; ++k)
                    if (engine->tracks[t].clips[c].creation_index == candidate[i].clips[k].creation_index) goto done;
    }
    // Retiring rows must be empty in the destination and contain only clips restored elsewhere.
    for (int t = target_count; t < original_count; ++t) {
        bool empty_destination = false;
        for (int i = 0; i < count; ++i)
            if (indices[i] == t && candidate[i].clip_count == 0) empty_destination = true;
        if (!empty_destination) goto done;
        for (int c = 0; c < engine->tracks[t].clip_count; ++c) {
            bool retained = false;
            for (int i = 0; i < count; ++i) if (indices[i] < target_count)
                for (int k = 0; k < candidate[i].clip_count; ++k)
                    if (candidate[i].clips[k].creation_index == engine->tracks[t].clips[c].creation_index) retained = true;
            for (int i = 0; i < snapshot->inserted_count; ++i)
                if (snapshot->inserted_ids[i] == engine->tracks[t].clips[c].creation_index) retained = true;
            if (!retained) goto done;
        }
    }
    for (int i = 0; i < count; ++i) engine->tracks[indices[i]] = candidate[i];
    engine->track_count = target_count;
    SDL_LockMutex(engine->fxm_mutex);
    EffectsManager* previous_fx = engine->fxm;
    EffectsManager* candidate_fx = snapshot->has_track_topology && previous_fx ? fxm_clone_for_render(previous_fx) : previous_fx;
    engine->fxm = candidate_fx;
    accepted = (!previous_fx || (candidate_fx && (!snapshot->has_track_topology || fxm_set_track_count(candidate_fx, target_count)))) &&
        engine_request_rebuild_sources(engine);
    if (snapshot->has_track_topology) {
        if (accepted) fxm_destroy(previous_fx);
        else { engine->fxm = previous_fx; fxm_destroy(candidate_fx); }
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    if (!accepted) {
        engine->track_count = original_count > target_count ? original_count : target_count;
        for (int i = 0; i < count; ++i) engine->tracks[indices[i]] = previous[i];
    }
    if (accepted && target_count < original_count) for (int t = target_count; t < original_count; ++t) {
        engine_track_clear(engine, &engine->tracks[t]); engine_track_init(&engine->tracks[t]);
    }
done:
    if (candidate && previous) for (int i = 0; i < count; ++i) {
        EngineTrack* release = accepted ? &previous[i] : &candidate[i];
        for (int c = 0; c < release->clip_count; ++c) engine_clip_destroy(engine, &release->clips[c]);
        free(release->clips);
    }
    if (!accepted && target_count > original_count) {
        for (int t = original_count; t < target_count; ++t) {
            engine_track_clear(engine, &engine->tracks[t]); engine_track_init(&engine->tracks[t]);
        }
        engine->track_count = original_count;
    }
    free(previous); free(candidate); free(indices);
    return accepted;
}

// Applies placement to private clip rows; failure is contained in the disposable candidate.
static bool history_transform(Engine* engine, EngineClipContentSnapshot* snapshot, const EngineClipBatchTransform* edits, int count) {
    for (int i = 0; i < count; ++i) {
        if (edits[i].destination_track < 0 || edits[i].destination_track >= engine->track_count) return false;
        for (int j = 0; j < i; ++j) if (edits[j].creation_index == edits[i].creation_index) return false;
        ClipHistoryTrack* source = NULL; ClipHistoryTrack* destination = NULL; int index = -1;
        for (int t = 0; t < snapshot->count; ++t) {
            ClipHistoryTrack* row = &snapshot->tracks[t];
            if (row->track_id == engine->tracks[edits[i].destination_track].runtime_id) destination = row;
            for (int c = 0; c < row->count; ++c)
                if (row->clips[c].creation_index == edits[i].creation_index) { source = row; index = c; }
        }
        if (!source || !destination) return false;
        EngineClip original = source->clips[index], moved = original;
        if (!engine_clip_prepare_transform(&original, &edits[i].transform, &moved)) return false;
        if (source != destination) {
            if (destination->count == INT_MAX) { if (moved.kind == ENGINE_CLIP_KIND_MIDI) engine_midi_note_list_free(&moved.midi_notes); return false; }
            EngineClip* clips = calloc((size_t)destination->count + 1, sizeof(*clips));
            if (!clips) { if (moved.kind == ENGINE_CLIP_KIND_MIDI) engine_midi_note_list_free(&moved.midi_notes); return false; }
            if (destination->count) memcpy(clips, destination->clips, (size_t)destination->count * sizeof(*clips));
            clips[destination->count++] = moved;
            free(destination->clips); destination->clips = clips; destination->active = true;
            memmove(source->clips + index, source->clips + index + 1, (size_t)(source->count - index - 1) * sizeof(*clips));
            if (--source->count == 0) source->active = false;
            if (moved.kind == ENGINE_CLIP_KIND_MIDI && !destination->instrument_enabled) {
                destination->instrument_enabled = true; destination->instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
                destination->instrument_params = engine_instrument_default_params(destination->instrument_preset);
            }
        } else source->clips[index] = moved;
        if (moved.kind == ENGINE_CLIP_KIND_MIDI) engine_midi_note_list_free(&original.midi_notes);
        if (moved.sampler) {
            engine_sampler_source_set_clip(moved.sampler, moved.media, moved.timeline_start_frames, moved.offset_frames,
                moved.duration_frames, moved.fade_in_frames, moved.fade_out_frames);
            engine_sampler_source_set_fade_curves(moved.sampler, moved.fade_in_curve, moved.fade_out_curve);
        }
        EngineTrack sorted = {.clips = destination->clips, .clip_count = destination->count};
        engine_track_sort_clips(&sorted);
    }
    return true;
}

// Prepares placement, track growth, overlap and both history states before one publication.
bool engine_clip_content_drop(Engine* engine, const EngineClipBatchTransform* initial,
                              const EngineClipBatchTransform* final, int count,
                              EngineClipContentSnapshot** out_before, EngineClipContentSnapshot** out_after) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || !initial || !final || count <= 0 || !out_before || !out_after) return false;
    int original_count = engine->track_count, required_count = original_count;
    for (int i = 0; i < count; ++i) {
        if (initial[i].creation_index != final[i].creation_index || initial[i].destination_track < 0 ||
            initial[i].destination_track >= original_count || final[i].destination_track < 0 || final[i].destination_track == INT_MAX) return false;
        if (final[i].destination_track >= required_count) required_count = final[i].destination_track + 1;
    }
    int* tracks = calloc((size_t)required_count, sizeof(*tracks));
    if (!tracks) return false;
    if (required_count > original_count && !engine_get_track_mutable(engine, required_count - 1)) { free(tracks); return false; }
    int used = 0;
    for (int t = 0; t < engine->track_count; ++t) {
        bool affected = t >= original_count;
        for (int i = 0; i < count; ++i) {
            if (initial[i].creation_index != final[i].creation_index || initial[i].destination_track < 0 ||
                initial[i].destination_track >= engine->track_count || final[i].destination_track < 0 ||
                final[i].destination_track >= engine->track_count) { free(tracks); return false; }
            if (initial[i].destination_track == t || final[i].destination_track == t) affected = true;
            for (int c = 0; c < engine->tracks[t].clip_count; ++c)
                if (engine->tracks[t].clips[c].creation_index == final[i].creation_index) affected = true;
        }
        if (affected) tracks[used++] = t;
    }
    EngineClipContentSnapshot* before = engine_clip_content_capture(engine, tracks, used);
    EngineClipContentSnapshot* after = engine_clip_content_capture(engine, tracks, used);
    free(tracks);
    bool accepted = false;
    if (required_count > original_count && before && after) {
        before->has_track_topology = after->has_track_topology = true;
        before->restore_track_count = original_count; after->restore_track_count = required_count;
        before->generated_start = after->generated_start = original_count;
        for (int i = 0; i < used; ++i) if (i < before->count) {
            for (int t = original_count; t < required_count; ++t)
                if (before->tracks[i].track_id == engine->tracks[t].runtime_id)
                    before->tracks[i].generated_index = after->tracks[i].generated_index = t;
        }
    }
    if (!before || !after || !history_transform(engine, before, initial, count) || !history_transform(engine, after, final, count)) goto done;
    for (int i = 0; i < count; ++i) {
        for (int t = 0; t < after->count; ++t) {
            ClipHistoryTrack* row = &after->tracks[t];
            for (int c = 0; c < row->count; ++c) if (row->clips[c].creation_index == final[i].creation_index && row->clips[c].sampler) {
                EngineTrack candidate = {.clips = row->clips, .clip_count = row->count, .clip_capacity = row->count};
                if (!engine_track_prepare_no_overlap(engine, &candidate, row->clips[c].sampler)) goto done;
                row->clips = candidate.clips; row->count = candidate.clip_count;
                break;
            }
        }
    }
    if (!engine_clip_content_restore(engine, after)) goto done;
    *out_before = before; *out_after = after; accepted = true;
done:
    if (!accepted) {
        engine_clip_content_release(before); engine_clip_content_release(after);
        for (int t = original_count; t < required_count; ++t) {
            engine_track_clear(engine, &engine->tracks[t]); engine_track_init(&engine->tracks[t]);
        }
        engine->track_count = original_count;
    }
    return accepted;
}

// Prepares a complete duplicate/delete action with owned media and history before one publication.
bool engine_clip_content_edit(Engine* engine, const uint64_t* identities, int count, bool duplicate, uint64_t gap,
                              uint64_t* output_ids, EngineClipContentSnapshot** out_before, EngineClipContentSnapshot** out_after) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || !identities || count <= 0 ||
        !out_before || !out_after || (duplicate && !output_ids)) return false;
    int* indices = calloc((size_t)engine->track_count, sizeof(*indices));
    uint64_t* created = calloc((size_t)count, sizeof(*created));
    if (!indices || !created) { free(indices); free(created); return false; }
    int used = 0;
    for (int i = 0; i < count; ++i) {
        if (!identities[i]) goto invalid;
        for (int j = 0; j < i; ++j) if (identities[j] == identities[i]) goto invalid;
        int track = -1;
        for (int t = 0; t < engine->track_count; ++t)
            for (int c = 0; c < engine->tracks[t].clip_count; ++c)
                if (engine->tracks[t].clips[c].creation_index == identities[i]) track = t;
        if (track < 0) goto invalid;
        bool seen = false;
        for (int t = 0; t < used; ++t) if (indices[t] == track) seen = true;
        if (!seen) indices[used++] = track;
    }
    EngineClipContentSnapshot* before = engine_clip_content_capture(engine, indices, used);
    EngineClipContentSnapshot* after = engine_clip_content_capture(engine, indices, used);
    bool accepted = false;
    if (!before || !after) goto done;
    if (duplicate) {
        before->inserted_ids = calloc((size_t)count, sizeof(*before->inserted_ids));
        if (!before->inserted_ids) goto done;
    }
    for (int i = 0; i < count; ++i) {
        ClipHistoryTrack* row = NULL; int index = -1;
        for (int t = 0; t < after->count; ++t)
            for (int c = 0; c < after->tracks[t].count; ++c)
                if (after->tracks[t].clips[c].creation_index == identities[i]) { row = &after->tracks[t]; index = c; }
        if (!row) goto done;
        if (duplicate) {
            const EngineClip* source = &row->clips[index];
            if (row->count == INT_MAX || engine->next_clip_id == UINT64_MAX ||
                source->duration_frames > UINT64_MAX - source->timeline_start_frames ||
                gap > UINT64_MAX - source->timeline_start_frames - source->duration_frames) goto done;
            uint64_t start = source->timeline_start_frames + source->duration_frames + gap;
            if (source->duration_frames > UINT64_MAX - start) goto done;
            EngineClip copy;
            if (!history_clone_clip(engine, source, &copy)) goto done;
            EngineClip* clips = calloc((size_t)row->count + 1, sizeof(*clips));
            if (!clips) { engine_clip_destroy(engine, &copy); goto done; }
            copy.creation_index = engine->next_clip_id++;
            copy.timeline_start_frames = start;
            snprintf(copy.name, sizeof(copy.name), "%s copy", source->name[0] ? source->name : "Clip");
            if (copy.sampler) engine_sampler_source_set_clip(copy.sampler, copy.media, start, copy.offset_frames,
                copy.duration_frames, copy.fade_in_frames, copy.fade_out_frames);
            memcpy(clips, row->clips, (size_t)row->count * sizeof(*clips));
            clips[row->count++] = copy; free(row->clips); row->clips = clips;
            row->active = true; created[i] = copy.creation_index;
            before->inserted_ids[before->inserted_count++] = copy.creation_index;
        } else {
            engine_clip_destroy(engine, &row->clips[index]);
            memmove(row->clips + index, row->clips + index + 1, (size_t)(row->count - index - 1) * sizeof(*row->clips));
            if (--row->count == 0) row->active = false;
        }
    }
    for (int t = 0; t < after->count; ++t) {
        EngineTrack sorted = {.clips = after->tracks[t].clips, .clip_count = after->tracks[t].count};
        engine_track_sort_clips(&sorted);
    }
    if (!engine_clip_content_restore(engine, after)) goto done;
    if (duplicate) memcpy(output_ids, created, (size_t)count * sizeof(*created));
    *out_before = before; *out_after = after; accepted = true;
done:
    if (!accepted) { engine_clip_content_release(before); engine_clip_content_release(after); }
    free(indices); free(created); return accepted;
invalid:
    free(indices); free(created); return false;
}

// Materializes one private audio/MIDI source without publishing or inserting a project clip.
static bool history_prepare_insert(Engine* engine, const EngineClipInsert* input, EngineClip* out) {
    memset(out, 0, sizeof(*out));
    if ((input->kind != ENGINE_CLIP_KIND_AUDIO && input->kind != ENGINE_CLIP_KIND_MIDI) ||
        input->automation_lane_count < 0 || (input->automation_lane_count && !input->automation_lanes)) return false;
    for (int l = 0; l < input->automation_lane_count; ++l) {
        const EngineAutomationLane* lane = &input->automation_lanes[l];
        if (lane->target < 0 || lane->target >= ENGINE_AUTOMATION_TARGET_COUNT ||
            (input->kind == ENGINE_CLIP_KIND_AUDIO && engine_automation_target_is_instrument_param(lane->target)) ||
            lane->point_count < 0 || (lane->point_count && !lane->points)) return false;
        for (int j = 0; j < l; ++j) if (input->automation_lanes[j].target == lane->target) return false;
        for (int i = 0; i < lane->point_count; ++i) if (!isfinite(lane->points[i].value)) return false;
    }
    out->kind = input->kind; out->active = true;
    if (input->kind == ENGINE_CLIP_KIND_AUDIO) {
        if (!input->media_path || !input->media_path[0]) return false;
        out->source = engine_audio_source_get_or_create(engine, input->media_id, input->media_path);
        if (!out->source || !audio_media_cache_acquire(&engine->media_cache, input->media_id, input->media_path,
            engine->config.sample_rate, &out->media)) goto fail;
        out->sampler = engine_sampler_source_create();
        if (!out->sampler) goto fail;
    }
    EngineClip original = *out;
    if (!engine_clip_prepare_transform(&original, &input->transform, out)) goto fail;
    EngineClip lanes = {.automation_lanes = (EngineAutomationLane*)input->automation_lanes,
        .automation_lane_count = input->automation_lane_count};
    if (!engine_clip_copy_automation(&lanes, out)) goto fail;
    if (input->name) snprintf(out->name, sizeof(out->name), "%s", input->name);
    if (out->sampler) {
        engine_sampler_source_set_clip(out->sampler, out->media, out->timeline_start_frames, out->offset_frames,
            out->duration_frames, out->fade_in_frames, out->fade_out_frames);
        engine_sampler_source_set_fade_curves(out->sampler, out->fade_in_curve, out->fade_out_curve);
        if (!engine_sampler_source_set_automation(out->sampler, out->automation_lanes, out->automation_lane_count)) goto fail;
    } else {
        out->instrument = engine_instrument_source_create();
        if (!out->instrument || !engine_instrument_source_set_midi_clip(out->instrument, out->timeline_start_frames,
            out->duration_frames, out->instrument_preset, out->instrument_params, out->midi_notes.notes,
            out->midi_notes.note_count, NULL, 0, out->automation_lanes, out->automation_lane_count)) goto fail;
    }
    return true;
fail:
    engine_clip_destroy(engine, out); memset(out, 0, sizeof(*out)); return false;
}

// Prepares all pasted content and destination topology before exposing a single new project revision.
bool engine_clip_content_insert(Engine* engine, int destination, const EngineClipInsert* clips, int count,
                                uint64_t* output_ids, EngineClipContentSnapshot** out_before, EngineClipContentSnapshot** out_after) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || destination < 0 || destination == INT_MAX ||
        !clips || count <= 0 || !output_ids || !out_before || !out_after) return false;
    int previous_count = engine->track_count;
    int required = destination >= previous_count ? destination + 1 : previous_count;
    int row_count = required > previous_count ? required - previous_count : 1;
    int* rows = calloc((size_t)row_count, sizeof(*rows));
    uint64_t* created = calloc((size_t)count, sizeof(*created));
    if (!rows || !created) { free(rows); free(created); return false; }
    if (required > previous_count && !engine_get_track_mutable(engine, required - 1)) { free(rows); free(created); return false; }
    for (int i = 0; i < row_count; ++i) rows[i] = required > previous_count ? previous_count + i : destination;
    EngineClipContentSnapshot* before = engine_clip_content_capture(engine, rows, row_count);
    EngineClipContentSnapshot* after = engine_clip_content_capture(engine, rows, row_count);
    bool accepted = false;
    if (!before || !after) goto done;
    before->inserted_ids = calloc((size_t)count, sizeof(*before->inserted_ids));
    if (!before->inserted_ids) goto done;
    if (required > previous_count) {
        before->has_track_topology = after->has_track_topology = true;
        before->restore_track_count = previous_count; after->restore_track_count = required;
        before->generated_start = after->generated_start = previous_count;
        for (int i = 0; i < row_count; ++i) before->tracks[i].generated_index = after->tracks[i].generated_index = rows[i];
    }
    ClipHistoryTrack* row = &after->tracks[row_count - 1];
    if (count > INT_MAX - row->count) goto done;
    EngineClip* expanded = calloc((size_t)row->count + (size_t)count, sizeof(*expanded));
    if (!expanded) goto done;
    if (row->count) memcpy(expanded, row->clips, (size_t)row->count * sizeof(*expanded));
    free(row->clips); row->clips = expanded;
    for (int i = 0; i < count; ++i) {
        if (engine->next_clip_id == UINT64_MAX || !history_prepare_insert(engine, &clips[i], &row->clips[row->count])) goto done;
        EngineClip* clip = &row->clips[row->count++];
        clip->creation_index = engine->next_clip_id++; created[i] = clip->creation_index;
        if (before->inserted_ids) before->inserted_ids[before->inserted_count++] = clip->creation_index;
        if (clip->kind == ENGINE_CLIP_KIND_MIDI && !row->instrument_enabled) {
            row->instrument_enabled = true; row->instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
            row->instrument_params = engine_instrument_default_params(row->instrument_preset);
        }
    }
    row->active = true;
    EngineTrack sorted = {.clips = row->clips, .clip_count = row->count}; engine_track_sort_clips(&sorted);
    if (!engine_clip_content_restore(engine, after)) goto done;
    memcpy(output_ids, created, (size_t)count * sizeof(*created));
    *out_before = before; *out_after = after; accepted = true;
done:
    if (!accepted) {
        engine_clip_content_release(before); engine_clip_content_release(after);
        for (int t = previous_count; t < required; ++t) {
            engine_track_clear(engine, &engine->tracks[t]); engine_track_init(&engine->tracks[t]);
        }
        engine->track_count = previous_count;
    }
    free(rows); free(created); return accepted;
}

// Copies complete editable track ownership while retaining decoded media and original identities.
static bool history_clone_track(Engine* engine, const EngineTrack* source, EngineTrack* target) {
    *target = *source;
    target->clips = NULL; target->clip_count = target->clip_capacity = 0;
    target->midi_instrument_automation_lanes = NULL;
    target->midi_instrument_automation_lane_count = target->midi_instrument_automation_lane_capacity = 0;
    target->track_eq = (EngineEqState){0};
    if (!engine_eq_clone_configuration(&target->track_eq, &source->track_eq)) goto fail;
    if (source->clip_count) {
        target->clips = calloc((size_t)source->clip_count, sizeof(*target->clips));
        if (!target->clips) goto fail;
        target->clip_capacity = source->clip_count;
        for (int c = 0; c < source->clip_count; ++c) {
            if (!history_clone_clip(engine, &source->clips[c], &target->clips[c])) goto fail;
            ++target->clip_count;
        }
    }
    int count = source->midi_instrument_automation_lane_count;
    if (count) {
        target->midi_instrument_automation_lanes = calloc((size_t)count, sizeof(EngineAutomationLane));
        if (!target->midi_instrument_automation_lanes) goto fail;
        target->midi_instrument_automation_lane_capacity = count;
        for (int l = 0; l < count; ++l) {
            ++target->midi_instrument_automation_lane_count;
            if (!engine_automation_lane_copy(&source->midi_instrument_automation_lanes[l],
                                             &target->midi_instrument_automation_lanes[l])) goto fail;
        }
    }
    return true;
fail:
    engine_track_clear(engine, target);
    return false;
}

// Captures complete track history before a destructive action or constructs an unpublished empty track.
EngineClipContentSnapshot* engine_track_history_capture(Engine* engine, int track_index) {
    if (!engine || !engine_is_control_thread(engine) || track_index < -1 || track_index >= engine->track_count) return NULL;
    EngineClipContentSnapshot* snapshot = calloc(1, sizeof(*snapshot));
    if (!snapshot) return NULL;
    snapshot->owner = engine; snapshot->references = 1;
    snapshot->whole_track = calloc(1, sizeof(EngineTrack));
    if (!snapshot->whole_track) goto fail;
    if (track_index >= 0) {
        if (!history_clone_track(engine, &engine->tracks[track_index], snapshot->whole_track)) goto fail;
        if (engine->fxm) {
            snapshot->whole_fx = fxm_clone_track_for_history(engine->fxm, track_index);
            if (!snapshot->whole_fx) goto fail;
        }
    } else {
        engine_track_init(snapshot->whole_track);
        engine_eq_init(&snapshot->whole_track->track_eq, (float)engine->config.sample_rate, engine_graph_get_channels(engine->graph));
        snapshot->whole_track->active = false;
        snprintf(snapshot->whole_track->name, sizeof(snapshot->whole_track->name), "Track %d", engine->track_count + 1);
    }
    snapshot->next = engine->clip_history_snapshots; engine->clip_history_snapshots = snapshot;
    return snapshot;
fail:
    history_clear_rows(snapshot); free(snapshot); return NULL;
}

// Inserts complete retained content and a prepared effect chain, rolling back all authored state on refusal.
bool engine_track_history_insert(Engine* engine, int track_index, const EngineClipContentSnapshot* snapshot) {
    if (!engine || !engine_is_control_thread(engine) || !snapshot || snapshot->owner != engine ||
        !snapshot->whole_track || track_index < 0 || track_index > engine->track_count) return false;
    for (int t = 0; t < engine->track_count; ++t) {
        if (engine->tracks[t].runtime_id == snapshot->whole_track->runtime_id) return false;
        for (int c = 0; c < engine->tracks[t].clip_count; ++c)
            for (int k = 0; k < snapshot->whole_track->clip_count; ++k)
                if (engine->tracks[t].clips[c].creation_index == snapshot->whole_track->clips[k].creation_index) return false;
    }
    EngineTrack candidate = {0};
    if (!history_clone_track(engine, snapshot->whole_track, &candidate)) return false;
    if (!engine_ensure_track_capacity(engine, engine->track_count + 1)) {
        engine_track_clear(engine, &candidate); return false;
    }
    SDL_LockMutex(engine->fxm_mutex);
    EffectsManager* previous_fx = engine->fxm;
    EffectsManager* candidate_fx = previous_fx ? fxm_clone_for_render(previous_fx) : NULL;
    if ((previous_fx && (!candidate_fx || !fxm_set_track_count(candidate_fx, engine->track_count) ||
                         !fxm_insert_track(candidate_fx, track_index))) ||
        (snapshot->whole_fx && !fxm_copy_track(candidate_fx, track_index, snapshot->whole_fx, 0))) {
        fxm_destroy(candidate_fx); engine_track_clear(engine, &candidate);
        SDL_UnlockMutex(engine->fxm_mutex); return false;
    }
    int remaining = engine->track_count - track_index;
    EngineTrack spare = engine->tracks[engine->track_count];
    memmove(&engine->tracks[track_index + 1], &engine->tracks[track_index], (size_t)remaining * sizeof(EngineTrack));
    engine->tracks[track_index] = candidate;
    ++engine->track_count; engine->fxm = candidate_fx;
    int armed = atomic_load(&engine->record_armed_track_index);
    if (armed >= track_index) atomic_store(&engine->record_armed_track_index, armed + 1);
    if (!engine_request_rebuild_sources(engine)) {
        memmove(&engine->tracks[track_index], &engine->tracks[track_index + 1], (size_t)remaining * sizeof(EngineTrack));
        engine->tracks[--engine->track_count] = spare;
        engine->fxm = previous_fx; atomic_store(&engine->record_armed_track_index, armed);
        fxm_destroy(candidate_fx); engine_track_clear(engine, &candidate);
        SDL_UnlockMutex(engine->fxm_mutex); return false;
    }
    fxm_destroy(previous_fx); engine_track_clear(engine, &spare);
    SDL_UnlockMutex(engine->fxm_mutex);
    engine_track_reset_published_caches(engine);
    return true;
}

// Compares authored automation without depending on allocation capacity or pointer identity.
static bool history_lanes_equal(const EngineAutomationLane* a, int ac, const EngineAutomationLane* b, int bc) {
    if (ac != bc) return false;
    for (int l = 0; l < ac; ++l) {
        if (a[l].target != b[l].target || a[l].point_count != b[l].point_count) return false;
        for (int p = 0; p < a[l].point_count; ++p)
            if (a[l].points[p].frame != b[l].points[p].frame || a[l].points[p].value != b[l].points[p].value) return false;
    }
    return true;
}

// Compares instrument targets independently of padding and render history.
static bool history_instrument_equal(EngineInstrumentParams a, EngineInstrumentParams b) {
    for (int p = 0; p < ENGINE_INSTRUMENT_PARAM_COUNT; ++p)
        if (engine_instrument_params_get(a, (EngineInstrumentParamId)p) !=
            engine_instrument_params_get(b, (EngineInstrumentParamId)p)) return false;
    return true;
}

// Rejects deletion when later unrecorded authored edits would otherwise be silently discarded.
static bool history_track_equal(const EngineTrack* a, const EngineTrack* b) {
    if (a->runtime_id != b->runtime_id || a->clip_count != b->clip_count || a->gain != b->gain ||
        a->pan != b->pan || a->muted != b->muted || a->solo != b->solo || a->active != b->active ||
        strcmp(a->name, b->name) || a->midi_instrument_enabled != b->midi_instrument_enabled ||
        a->midi_instrument_preset != b->midi_instrument_preset ||
        !history_instrument_equal(a->midi_instrument_params, b->midi_instrument_params) ||
        !history_lanes_equal(a->midi_instrument_automation_lanes, a->midi_instrument_automation_lane_count,
                             b->midi_instrument_automation_lanes, b->midi_instrument_automation_lane_count)) return false;
    const EngineEqCurve* x = &a->track_eq.curve; const EngineEqCurve* y = &b->track_eq.curve;
    if (x->low_cut.enabled != y->low_cut.enabled || x->low_cut.freq_hz != y->low_cut.freq_hz ||
        x->high_cut.enabled != y->high_cut.enabled || x->high_cut.freq_hz != y->high_cut.freq_hz) return false;
    for (int i = 0; i < ENGINE_EQ_BANDS; ++i)
        if (x->bands[i].enabled != y->bands[i].enabled || x->bands[i].freq_hz != y->bands[i].freq_hz ||
            x->bands[i].gain_db != y->bands[i].gain_db || x->bands[i].q_width != y->bands[i].q_width) return false;
    for (int c = 0; c < a->clip_count; ++c) {
        const EngineClip* x = &a->clips[c]; const EngineClip* y = &b->clips[c];
        if (x->creation_index != y->creation_index || x->kind != y->kind || x->media != y->media ||
            x->source != y->source || x->gain != y->gain || x->active != y->active || strcmp(x->name, y->name) ||
            x->timeline_start_frames != y->timeline_start_frames || x->duration_frames != y->duration_frames ||
            x->offset_frames != y->offset_frames || x->fade_in_frames != y->fade_in_frames ||
            x->fade_out_frames != y->fade_out_frames || x->fade_in_curve != y->fade_in_curve ||
            x->fade_out_curve != y->fade_out_curve || x->instrument_preset != y->instrument_preset ||
            x->instrument_inherits_track != y->instrument_inherits_track ||
            !history_instrument_equal(x->instrument_params, y->instrument_params) ||
            x->midi_notes.note_count != y->midi_notes.note_count ||
            !history_lanes_equal(x->automation_lanes, x->automation_lane_count, y->automation_lanes, y->automation_lane_count)) return false;
        for (int n = 0; n < x->midi_notes.note_count; ++n) {
            const EngineMidiNote* u = &x->midi_notes.notes[n]; const EngineMidiNote* v = &y->midi_notes.notes[n];
            if (u->start_frame != v->start_frame || u->duration_frames != v->duration_frames ||
                u->note != v->note || u->velocity != v->velocity) return false;
        }
    }
    return true;
}

// Resolves the captured track lifetime instead of deleting whichever row now occupies its old index.
bool engine_track_history_remove(Engine* engine, const EngineClipContentSnapshot* snapshot) {
    if (!engine || !engine_is_control_thread(engine) || !snapshot || snapshot->owner != engine || !snapshot->whole_track) return false;
    for (int t = 0; t < engine->track_count; ++t)
        if (engine->tracks[t].runtime_id == snapshot->whole_track->runtime_id) {
            if (!history_track_equal(&engine->tracks[t], snapshot->whole_track)) return false;
            FxMasterSnapshot current = {0}, captured = {0};
            if (engine->fxm && !fxm_track_snapshot(engine->fxm, t, &current)) return false;
            if (snapshot->whole_fx && !fxm_track_snapshot(snapshot->whole_fx, 0, &captured)) return false;
            if (memcmp(&current, &captured, sizeof(current)) != 0) return false;
            return engine_remove_track(engine, t);
        }
    return false;
}
