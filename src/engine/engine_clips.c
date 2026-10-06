#include "engine/engine_internal.h"
#include "engine/engine_clips_automation_internal.h"

#include "engine/instrument.h"
#include "engine/midi.h"
#include "engine/sampler.h"

#include "audio/media_clip.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void engine_clip_destroy(Engine* engine, EngineClip* clip) {
    if (!clip) {
        return;
    }
    if (clip->automation_lanes) {
        for (int i = 0; i < clip->automation_lane_count; ++i) {
            engine_automation_lane_free(&clip->automation_lanes[i]);
        }
        free(clip->automation_lanes);
        clip->automation_lanes = NULL;
    }
    clip->automation_lane_count = 0;
    clip->automation_lane_capacity = 0;
    engine_midi_note_list_free(&clip->midi_notes);
    if (clip->sampler) {
        engine_sampler_source_destroy(clip->sampler);
        clip->sampler = NULL;
    }
    if (clip->instrument) {
        engine_instrument_source_destroy(clip->instrument);
        clip->instrument = NULL;
    }
    if (clip->media) {
        if (engine) {
            audio_media_cache_release(&engine->media_cache, clip->media);
        } else {
            audio_media_clip_free(clip->media);
            free(clip->media);
        }
        clip->media = NULL;
    }
    clip->source = NULL;
    clip->kind = ENGINE_CLIP_KIND_AUDIO;
    clip->instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    clip->instrument_params = engine_instrument_default_params(clip->instrument_preset);
    clip->instrument_inherits_track = false;
    clip->gain = 0.0f;
    clip->active = false;
    clip->name[0] = '\0';
    clip->timeline_start_frames = 0;
    clip->duration_frames = 0;
    clip->offset_frames = 0;
    clip->fade_in_frames = 0;
    clip->fade_out_frames = 0;
    clip->fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
    clip->fade_out_curve = ENGINE_FADE_CURVE_LINEAR;
    clip->creation_index = 0;
    clip->selected = false;
    clip->media = NULL;
    clip->source = NULL;
}

static void engine_clip_set_name_from_path(EngineClip* clip, const char* path) {
    if (!clip) {
        return;
    }
    clip->name[0] = '\0';
    if (!path) {
        return;
    }
    const char* base = strrchr(path, '/');
#if defined(_WIN32)
    const char* alt = strrchr(path, '\\');
    if (!base || (alt && alt > base)) {
        base = alt;
    }
#endif
    base = base ? base + 1 : path;
    char temp[ENGINE_CLIP_NAME_MAX];
    strncpy(temp, base, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char* dot = strrchr(temp, '.');
    if (dot) {
        *dot = '\0';
    }
    strncpy(clip->name, temp, sizeof(clip->name) - 1);
    clip->name[sizeof(clip->name) - 1] = '\0';
}

static EngineClip* engine_track_append_clip(Engine* engine, EngineTrack* track) {
    if (!engine || !track) {
        return NULL;
    }
    if (track->clip_count == track->clip_capacity) {
        int new_cap = track->clip_capacity == 0 ? 4 : track->clip_capacity * 2;
        EngineClip* new_clips = (EngineClip*)realloc(track->clips, sizeof(EngineClip) * (size_t)new_cap);
        if (!new_clips) {
            return NULL;
        }
        track->clips = new_clips;
        track->clip_capacity = new_cap;
    }
    EngineClip* clip = &track->clips[track->clip_count++];
    clip->kind = ENGINE_CLIP_KIND_AUDIO;
    clip->sampler = NULL;
    clip->instrument = NULL;
    clip->media = NULL;
    clip->source = NULL;
    engine_midi_note_list_init(&clip->midi_notes);
    clip->instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    clip->instrument_params = engine_instrument_default_params(clip->instrument_preset);
    clip->instrument_inherits_track = false;
    clip->gain = 1.0f;
    clip->active = true;
    clip->name[0] = '\0';
    clip->timeline_start_frames = 0;
    clip->duration_frames = 0;
    clip->offset_frames = 0;
    clip->fade_in_frames = 0;
    clip->fade_out_frames = 0;
    clip->fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
    clip->fade_out_curve = ENGINE_FADE_CURVE_LINEAR;
    clip->automation_lanes = NULL;
    clip->automation_lane_count = 0;
    clip->automation_lane_capacity = 0;
    clip->creation_index = engine->next_clip_id++;
    clip->selected = false;
    engine_clip_init_automation(clip);
    if (!clip->automation_lanes) {
        --track->clip_count;
        return NULL;
    }
    return clip;
}

static int engine_track_find_clip_by_creation_index(const EngineTrack* track, uint64_t creation_index) {
    if (!track) {
        return -1;
    }
    for (int i = 0; i < track->clip_count; ++i) {
        if (track->clips[i].creation_index == creation_index) {
            return i;
        }
    }
    return -1;
}

static int engine_clip_compare_timeline(const void* a, const void* b) {
    const EngineClip* ca = (const EngineClip*)a;
    const EngineClip* cb = (const EngineClip*)b;
    if (ca->timeline_start_frames < cb->timeline_start_frames) {
        return -1;
    } else if (ca->timeline_start_frames > cb->timeline_start_frames) {
        return 1;
    }
    if (ca->creation_index < cb->creation_index) {
        return -1;
    }
    if (ca->creation_index > cb->creation_index) {
        return 1;
    }
    return 0;
}

// Sorts owned or staged descriptors by timeline position and creation order.
void engine_track_sort_clips(EngineTrack* track) {
    if (!track || track->clip_count <= 1 || !track->clips) {
        return;
    }
    qsort(track->clips, (size_t)track->clip_count, sizeof(EngineClip), engine_clip_compare_timeline);
}

static uint64_t engine_ms_to_frames(const EngineRuntimeConfig* cfg, float ms) {
    if (!cfg || cfg->sample_rate <= 0 || ms <= 0.0f) {
        return 0;
    }
    double frames = (double)cfg->sample_rate * (double)ms / 1000.0;
    if (frames <= 0.0) {
        return 0;
    }
    return (uint64_t)(frames + 0.5);
}

static void engine_compute_default_fades(const EngineRuntimeConfig* cfg,
                                         uint64_t clip_length,
                                         uint64_t* out_fade_in,
                                         uint64_t* out_fade_out) {
    if (!out_fade_in || !out_fade_out) {
        return;
    }
    *out_fade_in = 0;
    *out_fade_out = 0;
    if (!cfg || clip_length == 0) {
        return;
    }

    uint64_t fade_in = engine_ms_to_frames(cfg, cfg->default_fade_in_ms);
    uint64_t fade_out = engine_ms_to_frames(cfg, cfg->default_fade_out_ms);

    if (fade_in > clip_length) {
        fade_in = clip_length;
    }
    if (fade_out > clip_length) {
        fade_out = clip_length;
    }
    if (fade_in + fade_out > clip_length) {
        uint64_t excess = (fade_in + fade_out) - clip_length;
        if (fade_out >= excess) {
            fade_out -= excess;
        } else if (fade_in >= excess) {
            fade_in -= excess;
        } else {
            fade_in = 0;
            fade_out = 0;
        }
    }

    *out_fade_in = fade_in;
    *out_fade_out = fade_out;
}

static bool engine_clip_resolve_media(Engine* engine, EngineClip* clip) {
    if (!engine || !clip) {
        return false;
    }
    if (clip->media) {
        return true;
    }
    if (!clip->source || clip->source->path[0] == '\0') {
        return false;
    }
    AudioMediaClip* cached_media = NULL;
    if (!audio_media_cache_acquire(&engine->media_cache,
                                   clip->source->media_id,
                                   clip->source->path,
                                   engine->config.sample_rate,
                                   &cached_media)) {
        SDL_Log("engine_clip_resolve_media: failed to load %s", clip->source->path);
        return false;
    }
    if (!cached_media || cached_media->channels <= 0) {
        audio_media_cache_release(&engine->media_cache, cached_media);
        return false;
    }
    clip->media = cached_media;
    if (clip->source) {
        clip->source->clip = cached_media;
        clip->source->sample_rate = cached_media->sample_rate;
        clip->source->channels = cached_media->channels;
        clip->source->frame_count = cached_media->frame_count;
    }
    return true;
}

// Prepares sampler timing and automation, reporting incomplete source preparation.
static bool engine_clip_refresh_sampler(Engine* engine, EngineClip* clip) {
    if (!clip || !clip->sampler) {
        return false;
    }
    if (!engine_clip_resolve_media(engine, clip)) {
        engine_sampler_source_set_clip(clip->sampler, NULL, 0, 0, 0, 0, 0);
        return false;
    }
    engine_sampler_source_set_clip(clip->sampler, clip->media,
                                   clip->timeline_start_frames,
                                   clip->offset_frames,
                                   clip->duration_frames,
                                   clip->fade_in_frames,
                                   clip->fade_out_frames);
    engine_sampler_source_set_fade_curves(clip->sampler, clip->fade_in_curve, clip->fade_out_curve);
    return engine_sampler_source_set_automation(clip->sampler,
                                         clip->automation_lanes,
                                         clip->automation_lane_count);
}

static EngineClip* engine_clip_create_with_source(Engine* engine,
                                                  EngineTrack* track,
                                                  EngineAudioSource* source,
                                                  const char* filepath,
                                                  const char* media_id,
                                                  uint64_t start_frame,
                                                  uint64_t offset_frames,
                                                  uint64_t duration_frames,
                                                  float gain,
                                                  uint64_t fade_in_frames,
                                                  uint64_t fade_out_frames,
                                                  bool use_default_fades,
                                                  const AudioMediaClip* prepared_pin) {
    if (!engine || !track || !filepath) {
        return NULL;
    }

    if (!source) {
        source = engine_audio_source_get_or_create(engine, media_id, filepath);
    }

    const char* cache_id = source ? source->media_id : media_id;
    const char* cache_path = (source && source->path[0] != '\0') ? source->path : filepath;
    AudioMediaClip* cached_media = (AudioMediaClip*)prepared_pin;
    bool acquired = prepared_pin ? audio_media_cache_retain(&engine->media_cache, prepared_pin) :
        audio_media_cache_acquire(&engine->media_cache, cache_id, cache_path,
                                   engine->config.sample_rate, &cached_media);
    if (!acquired) return NULL;

    if (!cached_media || cached_media->channels <= 0) {
        audio_media_cache_release(&engine->media_cache, cached_media);
        return NULL;
    }

    EngineClip* clip_slot = engine_track_append_clip(engine, track);
    if (!clip_slot) {
        audio_media_cache_release(&engine->media_cache, cached_media);
        return NULL;
    }

    clip_slot->sampler = engine_sampler_source_create();
    if (!clip_slot->sampler) {
        engine_clip_destroy(engine, clip_slot);
        track->clip_count--;
        audio_media_cache_release(&engine->media_cache, cached_media);
        return NULL;
    }

    clip_slot->kind = ENGINE_CLIP_KIND_AUDIO;
    clip_slot->instrument = NULL;
    clip_slot->media = cached_media;
    clip_slot->source = source;
    clip_slot->timeline_start_frames = start_frame;
    clip_slot->offset_frames = offset_frames;
    clip_slot->duration_frames = duration_frames > 0 ? duration_frames : cached_media->frame_count;
    clip_slot->selected = false;
    engine_clip_set_name_from_path(clip_slot, filepath);
    clip_slot->gain = gain;
    clip_slot->active = true;
    if (use_default_fades) {
        uint64_t default_fade_in = 0;
        uint64_t default_fade_out = 0;
        uint64_t clip_length = clip_slot->duration_frames > 0 ? clip_slot->duration_frames : cached_media->frame_count;
        engine_compute_default_fades(&engine->config, clip_length, &default_fade_in, &default_fade_out);
        clip_slot->fade_in_frames = default_fade_in;
        clip_slot->fade_out_frames = default_fade_out;
    } else {
        clip_slot->fade_in_frames = fade_in_frames;
        clip_slot->fade_out_frames = fade_out_frames;
    }
    clip_slot->fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
    clip_slot->fade_out_curve = ENGINE_FADE_CURVE_LINEAR;

    if (!engine_clip_refresh_sampler(engine, clip_slot)) {
        engine_clip_destroy(engine, clip_slot);
        --track->clip_count;
        return NULL;
    }
    return clip_slot;
}

// Holds staged clip descriptors while existing sources remain borrowed from the editable track.
typedef struct ClipAddition {
    EngineTrack track;
    uint64_t first_new_id;
} ClipAddition;

// Prepares descriptor capacity without moving any existing clip or creating project tracks.
static bool clip_addition_begin(Engine* engine, int index, ClipAddition* addition) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || index < 0 || index == INT_MAX ||
        engine->next_clip_id == UINT64_MAX) return false;
    if (index < engine->track_count) addition->track = engine->tracks[index];
    else engine_track_init(&addition->track);
    if (addition->track.clip_count == INT_MAX) return false;
    int capacity = addition->track.clip_count + 1;
    EngineClip* clips = calloc((size_t)capacity, sizeof(*clips));
    if (!clips) return false;
    if (addition->track.clip_count)
        memcpy(clips, addition->track.clips, (size_t)addition->track.clip_count * sizeof(*clips));
    addition->track.clips = clips;
    addition->track.clip_capacity = capacity;
    addition->first_new_id = engine->next_clip_id;
    return true;
}

// Frees newly constructed clips and descriptor storage while retaining all borrowed sources.
static void clip_addition_discard(Engine* engine, ClipAddition* addition) {
    for (int i = 0; i < addition->track.clip_count; ++i)
        if (addition->track.clips[i].creation_index >= addition->first_new_id)
            engine_clip_destroy(engine, &addition->track.clips[i]);
    free(addition->track.clips);
    addition->track.clips = NULL;
}

// Publishes an addition and any implicit track growth together, restoring the project on rejection.
static bool clip_addition_commit(Engine* engine, int index, ClipAddition* addition, int* out_index) {
    int previous_count = engine->track_count;
    EngineTrack* track = engine_get_track_mutable(engine, index);
    if (!track) { clip_addition_discard(engine, addition); return false; }
    SDL_LockMutex(engine->fxm_mutex);
    EffectsManager* previous_fx = engine->fxm;
    EffectsManager* candidate_fx = previous_fx;
    bool growing = engine->track_count != previous_count;
    if (growing && previous_fx) candidate_fx = fxm_clone_for_render(previous_fx);
    EngineTrack previous = *track;
    uint64_t id = addition->track.clips[addition->track.clip_count - 1].creation_index;
    engine_track_sort_clips(&addition->track);
    track->clips = addition->track.clips;
    track->clip_count = addition->track.clip_count;
    track->clip_capacity = addition->track.clip_capacity;
    track->active = true;
    track->midi_instrument_enabled = addition->track.midi_instrument_enabled;
    track->midi_instrument_preset = addition->track.midi_instrument_preset;
    track->midi_instrument_params = addition->track.midi_instrument_params;
    engine->fxm = candidate_fx;
    bool accepted = (!previous_fx || candidate_fx) && engine_request_rebuild_sources(engine);
    if (!accepted) {
        *track = previous;
        engine->fxm = previous_fx;
        if (growing) {
            for (int i = previous_count; i < engine->track_count; ++i) {
                engine_track_clear(engine, &engine->tracks[i]);
                engine_track_init(&engine->tracks[i]);
            }
            engine->track_count = previous_count;
            fxm_destroy(candidate_fx);
        }
        SDL_UnlockMutex(engine->fxm_mutex);
        clip_addition_discard(engine, addition);
        return false;
    }
    if (growing) fxm_destroy(previous_fx);
    SDL_UnlockMutex(engine->fxm_mutex);
    free(previous.clips);
    if (out_index) *out_index = engine_track_find_clip_by_creation_index(track, id);
    return true;
}

// Declares the shared transaction used by synchronous and privately prepared clip additions.
static bool add_clip_with_media(Engine*, int, const char*, const char*, uint64_t, int*, const AudioMediaClip*);
// Preserves synchronous loading for restore and explicit offline callers.
bool engine_add_clip_to_track_with_id(Engine* engine, int track, const char* path, const char* id,
                                      uint64_t start, int* out) {
    return add_clip_with_media(engine, track, path, id, start, out, NULL);
}

bool engine_add_clip(Engine* engine, const char* filepath, uint64_t start_frame) {
    return engine_add_clip_to_track_with_id(engine, 0, filepath, NULL, start_frame, NULL);
}

bool engine_add_clip_to_track(Engine* engine, int track_index, const char* filepath, uint64_t start_frame, int* out_clip_index) {
    return engine_add_clip_to_track_with_id(engine, track_index, filepath, NULL, start_frame, out_clip_index);
}

// Stages an audio addition and commits its sources and any new tracks together.
static bool add_clip_with_media(Engine* engine,
                                      int track_index,
                                      const char* filepath,
                                      const char* media_id,
                                      uint64_t start_frame,
                                      int* out_clip_index, const AudioMediaClip* prepared_pin) {
    if (!engine || !filepath) {
        return false;
    }

    ClipAddition addition;
    if (!clip_addition_begin(engine, track_index, &addition)) {
        return false;
    }
    EngineTrack* track = &addition.track;

    EngineAudioSource* source = engine_audio_source_get_or_create(engine, media_id, filepath);
    EngineClip* clip_slot = engine_clip_create_with_source(engine,
                                                           track,
                                                           source,
                                                           filepath,
                                                           media_id,
                                                           start_frame,
                                                           0,
                                                           0,
                                                           1.0f,
                                                           0,
                                                           0,
                                                           true, prepared_pin);
    if (!clip_slot) {
        clip_addition_discard(engine, &addition);
        return false;
    }

    return clip_addition_commit(engine, track_index, &addition, out_clip_index);
}

// Stages a MIDI region and track instrument defaults before publishing them together.
bool engine_add_midi_clip_to_track(Engine* engine,
                                   int track_index,
                                   uint64_t start_frame,
                                   uint64_t duration_frames,
                                   int* out_clip_index) {
    if (!engine || duration_frames == 0) {
        return false;
    }
    EngineInstrumentSource* instrument = engine_instrument_source_create();
    if (!instrument) {
        return false;
    }
    ClipAddition addition;
    if (!clip_addition_begin(engine, track_index, &addition)) {
        engine_instrument_source_destroy(instrument);
        return false;
    }
    EngineTrack* track = &addition.track;

    EngineClip* clip = engine_track_append_clip(engine, track);
    if (!clip) {
        engine_instrument_source_destroy(instrument);
        clip_addition_discard(engine, &addition);
        return false;
    }
    clip->kind = ENGINE_CLIP_KIND_MIDI;
    clip->instrument = instrument;
    if (!track->midi_instrument_enabled) {
        track->midi_instrument_enabled = true;
        track->midi_instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
        track->midi_instrument_params = engine_instrument_default_params(track->midi_instrument_preset);
    }
    clip->instrument_preset = track->midi_instrument_preset;
    clip->instrument_params = engine_instrument_params_sanitize(clip->instrument_preset,
                                                                track->midi_instrument_params);
    clip->instrument_inherits_track = true;
    clip->timeline_start_frames = start_frame;
    clip->duration_frames = duration_frames;
    clip->offset_frames = 0;
    clip->gain = 1.0f;
    clip->active = true;
    snprintf(clip->name, sizeof(clip->name), "MIDI Region");

    return clip_addition_commit(engine, track_index, &addition, out_clip_index);
}

// Refreshes scalar sampler timing without reallocating unchanged automation storage.
static void engine_clip_refresh_sampler_timing(EngineClip* clip) {
    if (!clip->sampler || !clip->media) return;
    engine_sampler_source_set_clip(clip->sampler, clip->media, clip->timeline_start_frames,
        clip->offset_frames, clip->duration_frames, clip->fade_in_frames, clip->fade_out_frames);
    engine_sampler_source_set_fade_curves(clip->sampler, clip->fade_in_curve, clip->fade_out_curve);
}

// Publishes scalar clip metadata and restores its sampler configuration when preparation fails.
static bool engine_clip_commit_scalar_edit(Engine* engine, EngineClip* clip, const EngineClip* previous) {
    engine_clip_refresh_sampler_timing(clip);
    if (engine_request_rebuild_sources(engine)) return true;
    *clip = *previous;
    engine_clip_refresh_sampler_timing(clip);
    return false;
}

// Resolves an existing clip only on the control thread, without growing tracks as a side effect.
static EngineClip* engine_clip_for_edit(Engine* engine, int track, int clip) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track < 0 || track >= engine->track_count ||
        clip < 0 || clip >= engine->tracks[track].clip_count) return NULL;
    return &engine->tracks[track].clips[clip];
}

// Moves a clip transactionally, retaining its original sorted position and output index on rejection.
bool engine_clip_set_timeline_start(Engine* engine, int track_index, int clip_index, uint64_t start_frame, int* out_clip_index) {
    EngineClip* clip = engine_clip_for_edit(engine, track_index, clip_index);
    if (!clip || (clip->kind != ENGINE_CLIP_KIND_MIDI && (!clip->sampler || !engine_clip_resolve_media(engine, clip)))) return false;
    EngineClip previous = *clip;
    EngineTrack* track = &engine->tracks[track_index];
    clip->timeline_start_frames = start_frame;
    engine_clip_refresh_sampler_timing(clip);
    engine_track_sort_clips(track);
    int moved = engine_track_find_clip_by_creation_index(track, previous.creation_index);
    if (!engine_request_rebuild_sources(engine)) {
        track->clips[moved] = previous;
        engine_clip_refresh_sampler_timing(&track->clips[moved]);
        engine_track_sort_clips(track);
        return false;
    }
    if (out_clip_index) *out_clip_index = moved;
    return true;
}

// Validates complete clip fields and prepares replacement MIDI notes without modifying original ownership.
bool engine_clip_prepare_transform(const EngineClip* original, const EngineClipTransform* transform, EngineClip* moved) {
    if (!isfinite(transform->gain) || transform->gain < 0 || !transform->duration_frames ||
        transform->duration_frames > UINT64_MAX - transform->start_frame ||
        transform->fade_in_curve < 0 || transform->fade_in_curve >= ENGINE_FADE_CURVE_COUNT ||
        transform->fade_out_curve < 0 || transform->fade_out_curve >= ENGINE_FADE_CURVE_COUNT) return false;
    if (original->kind == ENGINE_CLIP_KIND_MIDI) {
        for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i)
            if (!isfinite(engine_instrument_params_get(transform->instrument_params, (EngineInstrumentParamId)i))) return false;
        EngineMidiNoteList notes = {0};
        if (!engine_midi_note_list_set(&notes, transform->midi_notes, transform->midi_note_count)) return false;
        if (!engine_midi_notes_fit_duration(&notes, transform->duration_frames)) {
            engine_midi_note_list_free(&notes);
            return false;
        }
        moved->midi_notes = notes;
        moved->instrument_preset = engine_instrument_preset_clamp(transform->instrument_preset);
        moved->instrument_params = engine_instrument_params_sanitize(moved->instrument_preset, transform->instrument_params);
        moved->instrument_inherits_track = transform->instrument_inherits_track;
    } else if (!original->sampler || !original->media || transform->offset_frames >= original->media->frame_count ||
               transform->duration_frames > original->media->frame_count - transform->offset_frames) return false;
    moved->timeline_start_frames = transform->start_frame;
    moved->offset_frames = transform->offset_frames;
    moved->duration_frames = transform->duration_frames;
    moved->gain = transform->gain;
    moved->fade_in_frames = transform->fade_in_frames > transform->duration_frames ? transform->duration_frames : transform->fade_in_frames;
    uint64_t remaining = transform->duration_frames - moved->fade_in_frames;
    moved->fade_out_frames = transform->fade_out_frames > remaining ? remaining : transform->fade_out_frames;
    moved->fade_in_curve = transform->fade_in_curve;
    moved->fade_out_curve = transform->fade_out_curve;
    return true;
}

// Commits placement and optional complete state through the same two-track ownership transaction.
static bool engine_clip_transfer_transaction(Engine* engine, int source_index, int clip_index, int destination_index,
                                             uint64_t start_frame, const EngineClipTransform* transform, int* out_clip_index) {
    EngineClip* source_clip = engine_clip_for_edit(engine, source_index, clip_index);
    if (!source_clip || destination_index < 0 || destination_index == INT_MAX) return false;
    bool same_track = source_index == destination_index;
    if (same_track && !transform)
        return engine_clip_set_timeline_start(engine, source_index, clip_index, start_frame, out_clip_index);
    EngineClip original = *source_clip;
    EngineClip moved = original;
    if (transform && !engine_clip_prepare_transform(&original, transform, &moved)) return false;
    bool owns_notes = transform && moved.kind == ENGINE_CLIP_KIND_MIDI;
    EngineTrack source_previous = engine->tracks[source_index];
    int destination_count = same_track ? source_previous.clip_count - 1 :
        destination_index < engine->track_count ? engine->tracks[destination_index].clip_count : 0;
    if (destination_count == INT_MAX) {
        if (owns_notes) engine_midi_note_list_free(&moved.midi_notes);
        return false;
    }
    EngineClip* source_clips = source_previous.clip_count > 1 ?
        calloc((size_t)source_previous.clip_count - 1, sizeof(*source_clips)) : NULL;
    EngineClip* destination_clips = calloc((size_t)destination_count + 1, sizeof(*destination_clips));
    if ((source_previous.clip_count > 1 && !source_clips) || !destination_clips) {
        free(source_clips);
        free(destination_clips);
        if (owns_notes) engine_midi_note_list_free(&moved.midi_notes);
        return false;
    }
    for (int i = 0, j = 0; i < source_previous.clip_count; ++i)
        if (i != clip_index) source_clips[j++] = source_previous.clips[i];
    if (destination_count) memcpy(destination_clips, same_track ? source_clips : engine->tracks[destination_index].clips,
                                   (size_t)destination_count * sizeof(*destination_clips));
    int previous_count = engine->track_count;
    EngineTrack* destination = engine_get_track_mutable(engine, destination_index);
    if (!destination) {
        free(source_clips);
        free(destination_clips);
        if (owns_notes) engine_midi_note_list_free(&moved.midi_notes);
        return false;
    }
    EngineTrack* source = &engine->tracks[source_index];
    EngineTrack destination_previous = *destination;
    SDL_LockMutex(engine->fxm_mutex);
    EffectsManager* previous_fx = engine->fxm;
    bool growing = engine->track_count != previous_count;
    EffectsManager* candidate_fx = growing && previous_fx ? fxm_clone_for_render(previous_fx) : previous_fx;
    source->clips = source_clips;
    source->clip_count--;
    source->clip_capacity = source->clip_count;
    if (!source->clip_count) source->active = false;
    destination->clips = destination_clips;
    destination->clip_count = destination->clip_capacity = destination_count + 1;
    destination->active = same_track ? source_previous.active : true;
    if (!same_track && moved.kind == ENGINE_CLIP_KIND_MIDI && !destination->midi_instrument_enabled) {
        destination->midi_instrument_enabled = true;
        destination->midi_instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
        destination->midi_instrument_params = engine_instrument_default_params(destination->midi_instrument_preset);
    }
    destination_clips[destination_count] = moved;
    destination_clips[destination_count].timeline_start_frames = start_frame;
    engine_clip_refresh_sampler_timing(&destination_clips[destination_count]);
    engine_track_sort_clips(destination);
    engine->fxm = candidate_fx;
    if ((!previous_fx || candidate_fx) && engine_request_rebuild_sources(engine)) {
        if (growing) fxm_destroy(previous_fx);
        SDL_UnlockMutex(engine->fxm_mutex);
        free(source_previous.clips);
        if (!same_track) free(destination_previous.clips);
        else free(source_clips);
        if (owns_notes) engine_midi_note_list_free(&original.midi_notes);
        if (out_clip_index) *out_clip_index = engine_track_find_clip_by_creation_index(destination, moved.creation_index);
        return true;
    }
    *source = source_previous;
    *destination = destination_previous;
    engine_clip_refresh_sampler_timing(&original);
    engine->fxm = previous_fx;
    if (growing) {
        for (int i = previous_count; i < engine->track_count; ++i) {
            engine_track_clear(engine, &engine->tracks[i]);
            engine_track_init(&engine->tracks[i]);
        }
        engine->track_count = previous_count;
        fxm_destroy(candidate_fx);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    free(source_clips);
    free(destination_clips);
    if (owns_notes) engine_midi_note_list_free(&moved.midi_notes);
    return false;
}

// Transfers one clip between tracks in a single revision, retaining its identity and owned sources.
bool engine_move_clip_to_track(Engine* engine, int source_track, int clip_index, int destination_track,
                               uint64_t start_frame, int* out_clip_index) {
    return engine_clip_transfer_transaction(engine, source_track, clip_index, destination_track, start_frame, NULL, out_clip_index);
}

// Restores all transform fields and placement with one publication and no intermediate project states.
bool engine_transform_clip(Engine* engine, int source_track, int clip_index, int destination_track,
                           const EngineClipTransform* transform, int* out_clip_index) {
    return transform && engine_clip_transfer_transaction(engine, source_track, clip_index, destination_track,
                                                         transform->start_frame, transform, out_clip_index);
}

// Prepares all affected clip arrays and MIDI ownership before publishing one compound revision.
static bool engine_transform_clips_with_track_count(Engine* engine, const EngineClipBatchTransform* edits, int count, int retained_tracks) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || !edits || count <= 0 ||
        engine->track_count <= 0) return false;
    int previous_count = engine->track_count;
    if (retained_tracks < 0 || retained_tracks > previous_count) return false;
    int tracks = previous_count;
    for (int i = 0; i < count; ++i) {
        if (!edits[i].creation_index || edits[i].destination_track < 0 || edits[i].destination_track == INT_MAX) return false;
        if (edits[i].destination_track >= tracks) tracks = edits[i].destination_track + 1;
    }
    if (retained_tracks > 0 && tracks > previous_count) return false;
    if (tracks > previous_count && !engine_get_track_mutable(engine, tracks - 1)) return false;
    bool growing = tracks > previous_count;
    bool shrinking = retained_tracks > 0 && retained_tracks < previous_count;
    if (shrinking && growing) return false;
    EngineTrack* previous = calloc((size_t)tracks, sizeof(*previous));
    EngineTrack* candidate = calloc((size_t)tracks, sizeof(*candidate));
    EngineClip* originals = calloc((size_t)count, sizeof(*originals));
    EngineClip* replacements = calloc((size_t)count, sizeof(*replacements));
    int* sources = calloc((size_t)count, sizeof(*sources));
    int prepared = 0;
    bool published = false;
    if (!previous || !candidate || !originals || !replacements || !sources) goto done;
    memcpy(previous, engine->tracks, (size_t)tracks * sizeof(*previous));
    memcpy(candidate, previous, (size_t)tracks * sizeof(*candidate));
    // Candidate arrays borrow nested ownership until the single publication succeeds.
    for (int t = 0; t < tracks; ++t) candidate[t].clips = NULL;
    for (int i = 0; i < count; ++i) {
        if (!edits[i].creation_index || edits[i].destination_track < 0 ||
            edits[i].destination_track >= tracks) goto done;
        for (int j = 0; j < i; ++j)
            if (edits[j].creation_index == edits[i].creation_index) goto done;
        int found = -1;
        for (int t = 0; t < tracks && found < 0; ++t) {
            int c = engine_track_find_clip_by_creation_index(&previous[t], edits[i].creation_index);
            if (c >= 0) { sources[i] = t; originals[i] = previous[t].clips[c]; found = c; }
        }
        if (found < 0) goto done;
        replacements[i] = originals[i];
        if (!engine_clip_prepare_transform(&originals[i], &edits[i].transform, &replacements[i])) goto done;
        prepared++;
        candidate[sources[i]].clip_count--;
        if (candidate[edits[i].destination_track].clip_count == INT_MAX) goto done;
        candidate[edits[i].destination_track].clip_count++;
    }
    for (int t = 0; t < tracks; ++t) {
        candidate[t].clip_capacity = candidate[t].clip_count;
        if (candidate[t].clip_count) {
            candidate[t].clips = calloc((size_t)candidate[t].clip_count, sizeof(EngineClip));
            if (!candidate[t].clips) goto done;
        }
        int n = 0;
        for (int c = 0; c < previous[t].clip_count; ++c) {
            bool replaced = false;
            for (int i = 0; i < count; ++i)
                if (previous[t].clips[c].creation_index == edits[i].creation_index) { replaced = true; break; }
            if (!replaced) candidate[t].clips[n++] = previous[t].clips[c];
        }
        for (int i = 0; i < count; ++i) if (edits[i].destination_track == t) {
            candidate[t].clips[n++] = replacements[i];
            if (sources[i] != t) {
                candidate[t].active = true;
                if (replacements[i].kind == ENGINE_CLIP_KIND_MIDI && !candidate[t].midi_instrument_enabled) {
                    candidate[t].midi_instrument_enabled = true;
                    candidate[t].midi_instrument_preset = ENGINE_INSTRUMENT_PRESET_PURE_SINE;
                    candidate[t].midi_instrument_params = engine_instrument_default_params(ENGINE_INSTRUMENT_PRESET_PURE_SINE);
                }
            }
        }
        if (!n && previous[t].clip_count > 0) candidate[t].active = false;
        engine_track_sort_clips(&candidate[t]);
    }
    if (shrinking) for (int t = retained_tracks; t < tracks; ++t)
        if (candidate[t].clip_count != 0) goto done;
    memcpy(engine->tracks, candidate, (size_t)tracks * sizeof(*candidate));
    if (shrinking) engine->track_count = retained_tracks;
    for (int i = 0; i < count; ++i) engine_clip_refresh_sampler_timing(&replacements[i]);
    SDL_LockMutex(engine->fxm_mutex);
    EffectsManager* previous_fx = engine->fxm;
    EffectsManager* candidate_fx = (growing || shrinking) && previous_fx ? fxm_clone_for_render(previous_fx) : previous_fx;
    engine->fxm = candidate_fx;
    published = (!previous_fx || (candidate_fx && (!shrinking || fxm_set_track_count(candidate_fx, retained_tracks)))) && engine_request_rebuild_sources(engine);
    if (published) {
        if (growing || shrinking) fxm_destroy(previous_fx);
    } else {
        engine->fxm = previous_fx;
        if (growing || shrinking) fxm_destroy(candidate_fx);
    }
    SDL_UnlockMutex(engine->fxm_mutex);
    if (!published) {
        engine->track_count = tracks;
        memcpy(engine->tracks, previous, (size_t)tracks * sizeof(*previous));
        for (int i = 0; i < count; ++i) engine_clip_refresh_sampler_timing(&originals[i]);
    }
done:
    if (published && shrinking) for (int t = retained_tracks; t < tracks; ++t) {
        engine_track_clear(engine, &engine->tracks[t]);
        engine_track_init(&engine->tracks[t]);
    }
    if (candidate && previous) for (int t = 0; t < tracks; ++t)
        free(published ? previous[t].clips : candidate[t].clips);
    for (int i = 0; i < prepared; ++i) if (originals[i].kind == ENGINE_CLIP_KIND_MIDI)
        engine_midi_note_list_free(published ? &originals[i].midi_notes : &replacements[i].midi_notes);
    if (!published && growing) {
        for (int t = previous_count; t < tracks; ++t) {
            engine_track_clear(engine, &engine->tracks[t]);
            engine_track_init(&engine->tracks[t]);
        }
        engine->track_count = previous_count;
    }
    free(previous); free(candidate); free(originals); free(replacements); free(sources);
    return published;
}

// Publishes a transform batch, appending destination tracks as required.
bool engine_transform_clips(Engine* engine, const EngineClipBatchTransform* edits, int count) {
    return engine_transform_clips_with_track_count(engine, edits, count, 0);
}

// Publishes the complete move before retiring now-empty trailing tracks.
bool engine_transform_clips_trim_tracks(Engine* engine, const EngineClipBatchTransform* edits, int count, int retained_tracks) {
    if (retained_tracks <= 0) return false;
    return engine_transform_clips_with_track_count(engine, edits, count, retained_tracks);
}

// Trims a region only when the resulting source revision can be prepared successfully.
bool engine_clip_set_region(Engine* engine, int track_index, int clip_index, uint64_t offset_frames, uint64_t duration_frames) {
    EngineClip* clip = engine_clip_for_edit(engine, track_index, clip_index);
    if (!clip) return false;
    if (clip->kind == ENGINE_CLIP_KIND_MIDI) {
        if (!duration_frames || !engine_midi_notes_fit_duration(&clip->midi_notes, duration_frames)) return false;
    } else {
        if (!clip->sampler || !engine_clip_resolve_media(engine, clip) || !clip->media->frame_count) return false;
        uint64_t total = clip->media->frame_count;
        if (offset_frames >= total) offset_frames = total - 1;
        uint64_t available = total - offset_frames;
        if (!duration_frames || duration_frames > available) duration_frames = available;
    }
    EngineClip previous = *clip;
    clip->offset_frames = offset_frames;
    clip->duration_frames = duration_frames;
    return engine_clip_commit_scalar_edit(engine, clip, &previous);
}

uint64_t engine_clip_get_total_frames(const Engine* engine, int track_index, int clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return 0;
    }
    const EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return 0;
    }
    const EngineClip* clip = &track->clips[clip_index];
    if (!clip) {
        return 0;
    }
    if (clip->kind == ENGINE_CLIP_KIND_MIDI) {
        return clip->duration_frames;
    }
    if (!clip->media) {
        return 0;
    }
    return clip->media->frame_count;
}

// Retains a detached clip until its replacement render revision is prepared, restoring it on failure.
bool engine_remove_clip(Engine* engine, int track_index, int clip_index) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track_index < 0 || track_index >= engine->track_count) return false;
    EngineTrack* track = &engine->tracks[track_index];
    if (clip_index < 0 || clip_index >= track->clip_count) return false;
    EngineClip removed = track->clips[clip_index];
    bool previous_active = track->active;
    int remaining = track->clip_count - clip_index - 1;
    memmove(&track->clips[clip_index], &track->clips[clip_index + 1], (size_t)remaining * sizeof(EngineClip));
    --track->clip_count;
    track->active = track->clip_count > 0;
    if (!engine_request_rebuild_sources(engine)) {
        memmove(&track->clips[clip_index + 1], &track->clips[clip_index], (size_t)remaining * sizeof(EngineClip));
        track->clips[clip_index] = removed;
        ++track->clip_count;
        track->active = previous_active;
        return false;
    }
    memset(&track->clips[track->clip_count], 0, sizeof(EngineClip));
    engine_clip_destroy(engine, &removed);
    return true;
}

bool engine_clip_set_name(Engine* engine, int track_index, int clip_index, const char* name) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    EngineClip* clip = &track->clips[clip_index];
    if (!clip) {
        return false;
    }
    if (name && name[0] != '\0') {
        strncpy(clip->name, name, sizeof(clip->name) - 1);
        clip->name[sizeof(clip->name) - 1] = '\0';
    } else {
        clip->name[0] = '\0';
    }
    return true;
}

// Preserves explicit zero gain and rolls back rejected gain changes.
bool engine_clip_set_gain(Engine* engine, int track_index, int clip_index, float gain) {
    EngineClip* clip = engine_clip_for_edit(engine, track_index, clip_index);
    if (!clip || !isfinite(gain) || gain < 0) return false;
    EngineClip previous = *clip;
    clip->gain = gain;
    return engine_clip_commit_scalar_edit(engine, clip, &previous);
}

// Bounds fade lengths without overflowing and retains prior fades on publication rejection.
bool engine_clip_set_fades(Engine* engine, int track_index, int clip_index, uint64_t fade_in_frames, uint64_t fade_out_frames) {
    EngineClip* clip = engine_clip_for_edit(engine, track_index, clip_index);
    if (!clip || !engine_clip_resolve_media(engine, clip)) return false;
    uint64_t max_len = clip->duration_frames;
    if (!max_len) max_len = engine_clip_get_total_frames(engine, track_index, clip_index);
    if (fade_in_frames > max_len) fade_in_frames = max_len;
    if (fade_out_frames > max_len - fade_in_frames) fade_out_frames = max_len - fade_in_frames;
    EngineClip previous = *clip;
    clip->fade_in_frames = fade_in_frames;
    clip->fade_out_frames = fade_out_frames;
    return engine_clip_commit_scalar_edit(engine, clip, &previous);
}

// Publishes existing fade shapes with scalar-edit rollback and legacy invalid-enum fallback.
bool engine_clip_set_fade_curves(Engine* engine, int track_index, int clip_index,
                                 EngineFadeCurve fade_in_curve, EngineFadeCurve fade_out_curve) {
    EngineClip* clip = engine_clip_for_edit(engine, track_index, clip_index);
    if (!clip) return false;
    if (fade_in_curve < 0 || fade_in_curve >= ENGINE_FADE_CURVE_COUNT) fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
    if (fade_out_curve < 0 || fade_out_curve >= ENGINE_FADE_CURVE_COUNT) fade_out_curve = ENGINE_FADE_CURVE_LINEAR;
    EngineClip previous = *clip;
    clip->fade_in_curve = fade_in_curve;
    clip->fade_out_curve = fade_out_curve;
    return engine_clip_commit_scalar_edit(engine, clip, &previous);
}

// Copies a bounded media segment without moving its source descriptor during preparation.
bool engine_add_clip_segment(Engine* engine, int track_index, const EngineClip* source_clip,
                             uint64_t source_relative_offset_frames,
                             uint64_t segment_length_frames,
                             uint64_t start_frame,
                             int* out_clip_index) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track_index < 0 || track_index == INT_MAX ||
        !source_clip || segment_length_frames == 0) {
        return false;
    }
    EngineClip* mutable_source = (EngineClip*)source_clip;
    if (!engine_clip_resolve_media(engine, mutable_source)) {
        return false;
    }
    const AudioMediaClip* media_src = mutable_source->media;
    if (!media_src || media_src->frame_count == 0) {
        return false;
    }

    if (source_clip->offset_frames >= media_src->frame_count ||
        source_relative_offset_frames >= media_src->frame_count - source_clip->offset_frames) {
        return false;
    }

    uint64_t max_length = media_src->frame_count - source_clip->offset_frames - source_relative_offset_frames;
    if (segment_length_frames > max_length) {
        segment_length_frames = max_length;
    }
    if (segment_length_frames == 0) {
        return false;
    }

    const char* media_id = engine_clip_get_media_id(source_clip);
    const char* media_path = engine_clip_get_media_path(source_clip);
    EngineAudioSource* source = source_clip->source;
    if (!source) {
        source = engine_audio_source_get_or_create(engine, media_id, media_path);
    }
    ClipAddition addition;
    if (!clip_addition_begin(engine, track_index, &addition)) {
        return false;
    }
    EngineTrack* track = &addition.track;

    EngineClip* new_clip = engine_clip_create_with_source(engine,
                                                          track,
                                                          source,
                                                          media_path ? media_path : "",
                                                          media_id && media_id[0] != '\0' ? media_id : NULL,
                                                          start_frame,
                                                          source_clip->offset_frames + source_relative_offset_frames,
                                                          segment_length_frames,
                                                          source_clip->gain,
                                                          source_clip->fade_in_frames,
                                                          source_clip->fade_out_frames,
                                                          false, NULL);
    if (!new_clip) {
        clip_addition_discard(engine, &addition);
        return false;
    }

    if (source_clip->name[0] != '\0') {
        snprintf(new_clip->name, sizeof(new_clip->name), "%s segment", source_clip->name);
    } else {
        snprintf(new_clip->name, sizeof(new_clip->name), "Clip segment");
    }
    new_clip->fade_in_curve = source_clip->fade_in_curve;
    new_clip->fade_out_curve = source_clip->fade_out_curve;
    engine_sampler_source_set_fade_curves(new_clip->sampler, new_clip->fade_in_curve, new_clip->fade_out_curve);
    if (!engine_clip_copy_automation(source_clip, new_clip) ||
        !engine_sampler_source_set_automation(new_clip->sampler,
            new_clip->automation_lanes, new_clip->automation_lane_count)) {
        clip_addition_discard(engine, &addition);
        return false;
    }

    return clip_addition_commit(engine, track_index, &addition, out_clip_index);
}

// Copies a clip through independent descriptors and rejects timeline-start overflow.
bool engine_duplicate_clip(Engine* engine, int track_index, int clip_index, uint64_t start_frame_offset, int* out_clip_index) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    EngineClip* original = &track->clips[clip_index];
    if (!original) {
        return false;
    }
    if (!engine_clip_resolve_media(engine, original)) {
        return false;
    }

    if (original->duration_frames > UINT64_MAX - original->timeline_start_frames ||
        start_frame_offset > UINT64_MAX - original->timeline_start_frames - original->duration_frames) return false;
    ClipAddition addition;
    if (!clip_addition_begin(engine, track_index, &addition)) return false;
    track = &addition.track;
    uint64_t offset = start_frame_offset;
    uint64_t new_start = original->timeline_start_frames + original->duration_frames + offset;
    const char* media_id = engine_clip_get_media_id(original);
    const char* media_path = engine_clip_get_media_path(original);
    EngineAudioSource* source = original->source;
    if (!source) {
        source = engine_audio_source_get_or_create(engine, media_id, media_path);
    }
    EngineClip* new_clip = engine_clip_create_with_source(engine,
                                                          track,
                                                          source,
                                                          media_path ? media_path : "",
                                                          media_id && media_id[0] != '\0' ? media_id : NULL,
                                                          new_start,
                                                          original->offset_frames,
                                                          original->duration_frames,
                                                          original->gain,
                                                          original->fade_in_frames,
                                                          original->fade_out_frames,
                                                          false, NULL);
    if (!new_clip) {
        clip_addition_discard(engine, &addition);
        return false;
    }

    if (original->name[0] != '\0') {
        snprintf(new_clip->name, sizeof(new_clip->name), "%s copy", original->name);
    } else {
        snprintf(new_clip->name, sizeof(new_clip->name), "Clip copy");
    }
    new_clip->fade_in_curve = original->fade_in_curve;
    new_clip->fade_out_curve = original->fade_out_curve;
    engine_sampler_source_set_fade_curves(new_clip->sampler, new_clip->fade_in_curve, new_clip->fade_out_curve);
    if (!engine_clip_copy_automation(original, new_clip) ||
        !engine_sampler_source_set_automation(new_clip->sampler,
            new_clip->automation_lanes, new_clip->automation_lane_count)) {
        clip_addition_discard(engine, &addition);
        return false;
    }
    return clip_addition_commit(engine, track_index, &addition, out_clip_index);
}

const char* engine_clip_get_media_id(const EngineClip* clip) {
    if (!clip) {
        return NULL;
    }
    if (clip->source && clip->source->media_id[0] != '\0') {
        return clip->source->media_id;
    }
    return NULL;
}

EngineClipKind engine_clip_get_kind(const EngineClip* clip) {
    return clip ? clip->kind : ENGINE_CLIP_KIND_AUDIO;
}

const char* engine_clip_get_media_path(const EngineClip* clip) {
    if (!clip) {
        return NULL;
    }
    if (clip->source && clip->source->path[0] != '\0') {
        return clip->source->path;
    }
    return NULL;
}

// Pins an adopted source while the ordinary transaction constructs and publishes its clip without decoding again.
bool engine_add_prepared_clip(Engine* engine, int track, const char* path, const char* media_id,
                              uint64_t start_frame, AudioMediaClip* prepared, int* out_clip) {
    if (!engine || !engine_is_control_thread(engine) || track < 0 || track > engine_get_track_count(engine) ||
        !path || strlen(path) >= AUDIO_MEDIA_CACHE_PATH_MAX || !prepared ||
        prepared->sample_rate != engine->config.sample_rate) return false;
    EngineAudioSource* source = engine_audio_source_get_or_create(engine, media_id, path);
    if (!source) return false;
    AudioMediaClip* pin = NULL;
    if (!audio_media_cache_adopt(&engine->media_cache, source->media_id, path,
                                engine->config.sample_rate, prepared, &pin)) return false;
    bool ok = add_clip_with_media(engine, track, path, source->media_id, start_frame, out_clip, pin);
    audio_media_cache_release(&engine->media_cache, pin);
    return ok;
}
// Exposes existing cache residency only to the engine control owner.
size_t engine_media_resident_bytes(const Engine* engine) {
    return engine && engine_is_control_thread(engine) ? audio_media_cache_resident_bytes(&engine->media_cache) : 0;
}

// Resolves the existing source key and borrows current-version decoded storage on control.
static const AudioMediaClip* cached_source(Engine* engine, const char* path, const char* media_id) {
    if (!engine || !engine_is_control_thread(engine) || !path) return NULL;
    if (media_id && *media_id)
        return audio_media_cache_lookup(&engine->media_cache,media_id,path,engine->config.sample_rate);
    for (int i=engine->audio_source_count-1;i>=0;--i) {
        const EngineAudioSource* source=engine->audio_sources[i];
        if (strcmp(source->path,path)) continue;
        const AudioMediaClip* clip=audio_media_cache_lookup(&engine->media_cache,source->media_id,path,engine->config.sample_rate);
        if (clip) return clip;
    }
    return NULL;
}
// Allows asynchronous admission to skip redundant decoding for already resident source versions.
bool engine_has_cached_media(Engine* engine, const char* path, const char* media_id) {
    return cached_source(engine,path,media_id)!=NULL;
}
// Rechecks residency at publication; a retired cache entry can be resubmitted for private decoding.
bool engine_add_cached_clip(Engine* engine, int track, const char* path, const char* media_id,
                             uint64_t start_frame, int* out_clip, bool* available) {
    const AudioMediaClip* pin=cached_source(engine,path,media_id);
    if (available) *available=pin!=NULL;
    return pin && add_clip_with_media(engine,track,path,media_id,start_frame,out_clip,pin);
}

// Periodically collects retired plans on control without touching the active render plan.
void engine_collect_retired_media(Engine* engine) {
    if (engine && engine_is_control_thread(engine)) engine_source_plan_collect(engine);
}
