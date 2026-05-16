#include "engine/engine_internal.h"
#include "engine/engine_clips_automation_internal.h"

#include "engine/instrument.h"
#include "engine/midi.h"
#include "engine/sampler.h"

#include "audio/media_clip.h"

#include <SDL2/SDL.h>
#include <math.h>
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

static bool engine_midi_note_fits_duration(const EngineMidiNote* note, uint64_t duration_frames) {
    if (!engine_midi_note_is_valid(note)) {
        return false;
    }
    if (note->start_frame > duration_frames) {
        return false;
    }
    return note->duration_frames <= duration_frames - note->start_frame;
}

static bool engine_midi_notes_fit_duration(const EngineMidiNoteList* notes, uint64_t duration_frames) {
    if (!engine_midi_note_list_validate(notes)) {
        return false;
    }
    for (int i = 0; i < notes->note_count; ++i) {
        if (!engine_midi_note_fits_duration(&notes->notes[i], duration_frames)) {
            return false;
        }
    }
    return true;
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

static void engine_track_sort_clips(EngineTrack* track) {
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

static void engine_clip_refresh_sampler(Engine* engine, EngineClip* clip) {
    if (!clip || !clip->sampler) {
        return;
    }
    if (!engine_clip_resolve_media(engine, clip)) {
        engine_sampler_source_set_clip(clip->sampler, NULL, 0, 0, 0, 0, 0);
        return;
    }
    engine_sampler_source_set_clip(clip->sampler, clip->media,
                                   clip->timeline_start_frames,
                                   clip->offset_frames,
                                   clip->duration_frames,
                                   clip->fade_in_frames,
                                   clip->fade_out_frames);
    engine_sampler_source_set_automation(clip->sampler,
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
                                                  bool use_default_fades) {
    if (!engine || !track || !filepath) {
        return NULL;
    }

    if (!source) {
        source = engine_audio_source_get_or_create(engine, media_id, filepath);
    }

    const char* cache_id = source ? source->media_id : media_id;
    const char* cache_path = (source && source->path[0] != '\0') ? source->path : filepath;
    AudioMediaClip* cached_media = NULL;
    if (!audio_media_cache_acquire(&engine->media_cache,
                                   cache_id,
                                   cache_path,
                                   engine->config.sample_rate,
                                   &cached_media)) {
        SDL_Log("engine_clip_create_with_source: failed to load %s", cache_path);
        return NULL;
    }

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

    engine_clip_refresh_sampler(engine, clip_slot);
    return clip_slot;
}

bool engine_add_clip(Engine* engine, const char* filepath, uint64_t start_frame) {
    return engine_add_clip_to_track_with_id(engine, 0, filepath, NULL, start_frame, NULL);
}

bool engine_add_clip_to_track(Engine* engine, int track_index, const char* filepath, uint64_t start_frame, int* out_clip_index) {
    return engine_add_clip_to_track_with_id(engine, track_index, filepath, NULL, start_frame, out_clip_index);
}

bool engine_add_clip_to_track_with_id(Engine* engine,
                                      int track_index,
                                      const char* filepath,
                                      const char* media_id,
                                      uint64_t start_frame,
                                      int* out_clip_index) {
    if (!engine || !filepath) {
        return false;
    }

    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }

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
                                                           true);
    if (!clip_slot) {
        return false;
    }

    EngineSamplerSource* new_sampler = clip_slot->sampler;
    track->active = true;
    engine_track_sort_clips(track);

    if (out_clip_index) {
        *out_clip_index = 0;
        for (int i = 0; i < track->clip_count; ++i) {
            if (track->clips[i].sampler == new_sampler) {
                *out_clip_index = i;
                break;
            }
        }
    }

    engine_trace(engine, "clip add track=%d start=%llu path=%s",
                 track_index,
                 (unsigned long long)start_frame,
                 filepath ? filepath : "");

    engine_request_rebuild_sources(engine);
    return true;
}

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
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        engine_instrument_source_destroy(instrument);
        return false;
    }

    EngineClip* clip = engine_track_append_clip(engine, track);
    if (!clip) {
        engine_instrument_source_destroy(instrument);
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

    uint64_t creation_index = clip->creation_index;
    track->active = true;
    engine_track_sort_clips(track);

    if (out_clip_index) {
        *out_clip_index = engine_track_find_clip_by_creation_index(track, creation_index);
    }

    engine_trace(engine, "midi clip add track=%d start=%llu duration=%llu",
                 track_index,
                 (unsigned long long)start_frame,
                 (unsigned long long)duration_frames);
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_clip_set_timeline_start(Engine* engine, int track_index, int clip_index, uint64_t start_frame, int* out_clip_index) {
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
    if (clip->kind == ENGINE_CLIP_KIND_MIDI) {
        uint64_t creation_index = clip->creation_index;
        clip->timeline_start_frames = start_frame;
        engine_track_sort_clips(track);
        if (out_clip_index) {
            *out_clip_index = engine_track_find_clip_by_creation_index(track, creation_index);
        }
        engine_request_rebuild_sources(engine);
        return true;
    }
    if (!clip->sampler) {
        return false;
    }
    if (!engine_clip_resolve_media(engine, clip)) {
        return false;
    }

    clip->timeline_start_frames = start_frame;
    engine_clip_refresh_sampler(engine, clip);

    engine_trace(engine, "clip move track=%d clip=%d start=%llu",
                 track_index,
                 clip_index,
                 (unsigned long long)start_frame);

    EngineSamplerSource* sampler = clip->sampler;
    engine_track_sort_clips(track);

    if (out_clip_index) {
        *out_clip_index = clip_index;
        for (int i = 0; i < track->clip_count; ++i) {
            if (track->clips[i].sampler == sampler) {
                *out_clip_index = i;
                break;
            }
        }
    }

    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_clip_set_region(Engine* engine, int track_index, int clip_index, uint64_t offset_frames, uint64_t duration_frames) {
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
    if (clip->kind == ENGINE_CLIP_KIND_MIDI) {
        if (duration_frames == 0) {
            return false;
        }
        if (!engine_midi_notes_fit_duration(&clip->midi_notes, duration_frames)) {
            return false;
        }
        clip->offset_frames = offset_frames;
        clip->duration_frames = duration_frames;
        engine_request_rebuild_sources(engine);
        return true;
    }
    if (!clip->sampler) {
        return false;
    }
    if (!engine_clip_resolve_media(engine, clip)) {
        return false;
    }

    uint64_t total_frames = clip->media ? clip->media->frame_count : 0;
    if (total_frames == 0) {
        return false;
    }

    if (offset_frames >= total_frames) {
        offset_frames = total_frames - 1;
    }
    uint64_t max_duration = total_frames - offset_frames;
    if (max_duration == 0) {
        max_duration = 1;
    }
    if (duration_frames == 0 || duration_frames > max_duration) {
        duration_frames = max_duration;
    }
    if (duration_frames == 0) {
        duration_frames = 1;
    }

    clip->offset_frames = offset_frames;
    clip->duration_frames = duration_frames;
    engine_clip_refresh_sampler(engine, clip);

    engine_request_rebuild_sources(engine);
    return true;
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

bool engine_remove_clip(Engine* engine, int track_index, int clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return false;
    }
    EngineClip* clip = &track->clips[clip_index];
    engine_clip_destroy(engine, clip);
    int remaining = track->clip_count - clip_index - 1;
    if (remaining > 0) {
        memmove(&track->clips[clip_index], &track->clips[clip_index + 1], (size_t)remaining * sizeof(EngineClip));
    }
    track->clip_count--;
    if (track->clip_count >= 0) {
        memset(&track->clips[track->clip_count], 0, sizeof(EngineClip));
    }
    if (track->clip_count > 0) {
        track->active = true;
    } else {
        track->active = false;
    }
    engine_request_rebuild_sources(engine);
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

bool engine_clip_set_gain(Engine* engine, int track_index, int clip_index, float gain) {
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
    clip->gain = gain;
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_clip_set_fades(Engine* engine, int track_index, int clip_index, uint64_t fade_in_frames, uint64_t fade_out_frames) {
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
    if (!engine_clip_resolve_media(engine, clip)) {
        return false;
    }
    uint64_t max_len = clip->duration_frames;
    if (max_len == 0) {
        max_len = engine_clip_get_total_frames(engine, track_index, clip_index);
    }
    if (fade_in_frames > max_len) {
        fade_in_frames = max_len;
    }
    if (fade_out_frames > max_len) {
        fade_out_frames = max_len;
    }
    if (fade_in_frames + fade_out_frames > max_len) {
        uint64_t total = fade_in_frames + fade_out_frames;
        uint64_t excess = total - max_len;
        if (fade_out_frames >= excess) {
            fade_out_frames -= excess;
        } else if (fade_in_frames >= excess) {
            fade_in_frames -= excess;
        } else {
            fade_out_frames = 0;
            fade_in_frames = 0;
        }
    }
    clip->fade_in_frames = fade_in_frames;
    clip->fade_out_frames = fade_out_frames;
    engine_clip_refresh_sampler(engine, clip);
    engine_trace(engine, "clip fades track=%d clip=%d in=%llu out=%llu",
                 track_index,
                 clip_index,
                 (unsigned long long)fade_in_frames,
                 (unsigned long long)fade_out_frames);
    return true;
}

bool engine_clip_set_fade_curves(Engine* engine,
                                 int track_index,
                                 int clip_index,
                                 EngineFadeCurve fade_in_curve,
                                 EngineFadeCurve fade_out_curve) {
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
    if (fade_in_curve < 0 || fade_in_curve >= ENGINE_FADE_CURVE_COUNT) {
        fade_in_curve = ENGINE_FADE_CURVE_LINEAR;
    }
    if (fade_out_curve < 0 || fade_out_curve >= ENGINE_FADE_CURVE_COUNT) {
        fade_out_curve = ENGINE_FADE_CURVE_LINEAR;
    }
    clip->fade_in_curve = fade_in_curve;
    clip->fade_out_curve = fade_out_curve;
    return true;
}

bool engine_add_clip_segment(Engine* engine, int track_index, const EngineClip* source_clip,
                             uint64_t source_relative_offset_frames,
                             uint64_t segment_length_frames,
                             uint64_t start_frame,
                             int* out_clip_index) {
    if (!engine || !source_clip || segment_length_frames == 0) {
        return false;
    }
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
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

    if (source_relative_offset_frames >= media_src->frame_count) {
        return false;
    }

    uint64_t max_length = media_src->frame_count - source_relative_offset_frames;
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
                                                          false);
    if (!new_clip) {
        return false;
    }

    if (source_clip->name[0] != '\0') {
        snprintf(new_clip->name, sizeof(new_clip->name), "%s segment", source_clip->name);
    } else {
        snprintf(new_clip->name, sizeof(new_clip->name), "Clip segment");
    }
    new_clip->fade_in_curve = source_clip->fade_in_curve;
    new_clip->fade_out_curve = source_clip->fade_out_curve;
    engine_clip_copy_automation(source_clip, new_clip);
    engine_sampler_source_set_automation(new_clip->sampler,
                                         new_clip->automation_lanes,
                                         new_clip->automation_lane_count);

    EngineSamplerSource* new_sampler = new_clip->sampler;
    track->active = true;
    engine_track_sort_clips(track);

    if (out_clip_index) {
        *out_clip_index = 0;
        for (int i = 0; i < track->clip_count; ++i) {
            if (track->clips[i].sampler == new_sampler) {
                *out_clip_index = i;
                break;
            }
        }
    }

    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_duplicate_clip(Engine* engine, int track_index, int clip_index, uint64_t start_frame_offset, int* out_clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
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
                                                          false);
    if (!new_clip) {
        return false;
    }

    if (original->name[0] != '\0') {
        snprintf(new_clip->name, sizeof(new_clip->name), "%s copy", original->name);
    } else {
        snprintf(new_clip->name, sizeof(new_clip->name), "Clip copy");
    }
    new_clip->fade_in_curve = original->fade_in_curve;
    new_clip->fade_out_curve = original->fade_out_curve;
    engine_clip_copy_automation(original, new_clip);
    engine_sampler_source_set_automation(new_clip->sampler,
                                         new_clip->automation_lanes,
                                         new_clip->automation_lane_count);
    EngineSamplerSource* new_sampler = new_clip->sampler;
    track->active = true;
    engine_track_sort_clips(track);

    if (out_clip_index) {
        *out_clip_index = 0;
        for (int i = 0; i < track->clip_count; ++i) {
            if (track->clips[i].sampler == new_sampler) {
                *out_clip_index = i;
                break;
            }
        }
    }

    engine_request_rebuild_sources(engine);
    return true;
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

static EngineClip* engine_get_midi_clip_mutable(Engine* engine, int track_index, int clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return NULL;
    }
    EngineTrack* track = &engine->tracks[track_index];
    if (clip_index < 0 || clip_index >= track->clip_count) {
        return NULL;
    }
    EngineClip* clip = &track->clips[clip_index];
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return NULL;
    }
    return clip;
}

bool engine_clip_midi_add_note(Engine* engine,
                               int track_index,
                               int clip_index,
                               EngineMidiNote note,
                               int* out_note_index) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    if (!engine_midi_note_fits_duration(&note, clip->duration_frames)) {
        return false;
    }
    bool ok = engine_midi_note_list_insert(&clip->midi_notes, note, out_note_index);
    if (ok) {
        engine_request_rebuild_sources(engine);
    }
    return ok;
}

bool engine_clip_midi_update_note(Engine* engine,
                                  int track_index,
                                  int clip_index,
                                  int note_index,
                                  EngineMidiNote note,
                                  int* out_note_index) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    if (!engine_midi_note_fits_duration(&note, clip->duration_frames)) {
        return false;
    }
    bool ok = engine_midi_note_list_update(&clip->midi_notes, note_index, note, out_note_index);
    if (ok) {
        engine_request_rebuild_sources(engine);
    }
    return ok;
}

bool engine_clip_midi_remove_note(Engine* engine, int track_index, int clip_index, int note_index) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    bool ok = engine_midi_note_list_remove(&clip->midi_notes, note_index);
    if (ok) {
        engine_request_rebuild_sources(engine);
    }
    return ok;
}

bool engine_clip_midi_set_notes(Engine* engine,
                                int track_index,
                                int clip_index,
                                const EngineMidiNote* notes,
                                int note_count) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip || note_count < 0) {
        return false;
    }
    if (note_count > 0 && !notes) {
        return false;
    }
    EngineMidiNoteList replacement;
    engine_midi_note_list_init(&replacement);
    bool ok = engine_midi_note_list_set(&replacement, notes, note_count);
    if (ok) {
        ok = engine_midi_notes_fit_duration(&replacement, clip->duration_frames);
    }
    if (ok) {
        engine_midi_note_list_free(&clip->midi_notes);
        clip->midi_notes = replacement;
        engine_midi_note_list_init(&replacement);
        engine_request_rebuild_sources(engine);
    }
    engine_midi_note_list_free(&replacement);
    return ok;
}

int engine_clip_midi_note_count(const EngineClip* clip) {
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return 0;
    }
    return clip->midi_notes.note_count;
}

const EngineMidiNote* engine_clip_midi_notes(const EngineClip* clip) {
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return NULL;
    }
    return clip->midi_notes.notes;
}

static bool engine_instrument_params_equal(EngineInstrumentParams a, EngineInstrumentParams b);

EngineInstrumentPresetId engine_clip_midi_instrument_preset(const EngineClip* clip) {
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    }
    return engine_instrument_preset_clamp(clip->instrument_preset);
}

bool engine_clip_midi_inherits_track_instrument(const EngineClip* clip) {
    return clip && clip->kind == ENGINE_CLIP_KIND_MIDI && clip->instrument_inherits_track;
}

bool engine_track_midi_instrument_enabled(const Engine* engine, int track_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return false;
    }
    return engine->tracks[track_index].midi_instrument_enabled;
}

EngineInstrumentPresetId engine_track_midi_instrument_preset(const Engine* engine, int track_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    }
    return engine_instrument_preset_clamp(engine->tracks[track_index].midi_instrument_preset);
}

EngineInstrumentParams engine_track_midi_instrument_params(const Engine* engine, int track_index) {
    EngineInstrumentPresetId preset = engine_track_midi_instrument_preset(engine, track_index);
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return engine_instrument_default_params(preset);
    }
    return engine_instrument_params_sanitize(preset, engine->tracks[track_index].midi_instrument_params);
}

bool engine_track_midi_set_instrument_preset(Engine* engine,
                                             int track_index,
                                             EngineInstrumentPresetId preset) {
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }
    EngineInstrumentPresetId clamped = engine_instrument_preset_clamp(preset);
    if (track->midi_instrument_enabled &&
        track->midi_instrument_preset == clamped) {
        return true;
    }
    track->midi_instrument_enabled = true;
    track->midi_instrument_preset = clamped;
    track->midi_instrument_params = engine_instrument_default_params(clamped);
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_track_midi_set_instrument_params(Engine* engine,
                                             int track_index,
                                             EngineInstrumentParams params) {
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }
    EngineInstrumentPresetId preset = engine_instrument_preset_clamp(track->midi_instrument_preset);
    EngineInstrumentParams clamped = engine_instrument_params_sanitize(preset, params);
    if (track->midi_instrument_enabled &&
        engine_instrument_params_equal(track->midi_instrument_params, clamped)) {
        return true;
    }
    track->midi_instrument_enabled = true;
    track->midi_instrument_params = clamped;
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_track_midi_set_instrument_enabled(Engine* engine, int track_index, bool enabled) {
    EngineTrack* track = engine_get_track_mutable(engine, track_index);
    if (!track) {
        return false;
    }
    if (track->midi_instrument_enabled == enabled) {
        return true;
    }
    track->midi_instrument_enabled = enabled;
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_clip_midi_set_inherits_track_instrument(Engine* engine,
                                                    int track_index,
                                                    int clip_index,
                                                    bool inherits_track) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    if (clip->instrument_inherits_track == inherits_track) {
        return true;
    }
    clip->instrument_inherits_track = inherits_track;
    engine_request_rebuild_sources(engine);
    return true;
}

EngineInstrumentPresetId engine_clip_midi_effective_instrument_preset(const Engine* engine,
                                                                      int track_index,
                                                                      int clip_index) {
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return ENGINE_INSTRUMENT_PRESET_PURE_SINE;
    }
    const EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return engine_track_midi_instrument_preset(engine, track_index);
    }
    const EngineClip* clip = &track->clips[clip_index];
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return engine_track_midi_instrument_preset(engine, track_index);
    }
    if (clip->instrument_inherits_track) {
        return engine_track_midi_instrument_preset(engine, track_index);
    }
    return engine_clip_midi_instrument_preset(clip);
}

EngineInstrumentParams engine_clip_midi_effective_instrument_params(const Engine* engine,
                                                                    int track_index,
                                                                    int clip_index) {
    EngineInstrumentPresetId preset = engine_clip_midi_effective_instrument_preset(engine, track_index, clip_index);
    if (!engine || track_index < 0 || track_index >= engine->track_count) {
        return engine_instrument_default_params(preset);
    }
    const EngineTrack* track = &engine->tracks[track_index];
    if (!track || clip_index < 0 || clip_index >= track->clip_count) {
        return engine_track_midi_instrument_params(engine, track_index);
    }
    const EngineClip* clip = &track->clips[clip_index];
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI || clip->instrument_inherits_track) {
        return engine_track_midi_instrument_params(engine, track_index);
    }
    return engine_clip_midi_instrument_params(clip);
}

bool engine_clip_midi_set_instrument_preset(Engine* engine,
                                            int track_index,
                                            int clip_index,
                                            EngineInstrumentPresetId preset) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    EngineInstrumentPresetId clamped = engine_instrument_preset_clamp(preset);
    if (clip->instrument_preset == clamped && !clip->instrument_inherits_track) {
        return true;
    }
    clip->instrument_preset = clamped;
    clip->instrument_params = engine_instrument_default_params(clamped);
    clip->instrument_inherits_track = false;
    engine_request_rebuild_sources(engine);
    return true;
}

EngineInstrumentParams engine_clip_midi_instrument_params(const EngineClip* clip) {
    EngineInstrumentPresetId preset = engine_clip_midi_instrument_preset(clip);
    if (!clip || clip->kind != ENGINE_CLIP_KIND_MIDI) {
        return engine_instrument_default_params(preset);
    }
    return engine_instrument_params_sanitize(preset, clip->instrument_params);
}

static bool engine_instrument_params_equal(EngineInstrumentParams a, EngineInstrumentParams b) {
    for (int i = 0; i < ENGINE_INSTRUMENT_PARAM_COUNT; ++i) {
        EngineInstrumentParamId param = (EngineInstrumentParamId)i;
        if (engine_instrument_params_get(a, param) != engine_instrument_params_get(b, param)) {
            return false;
        }
    }
    return true;
}

bool engine_clip_midi_set_instrument_params(Engine* engine,
                                            int track_index,
                                            int clip_index,
                                            EngineInstrumentParams params) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    EngineInstrumentPresetId preset = clip->instrument_inherits_track
                                          ? engine_clip_midi_effective_instrument_preset(engine,
                                                                                        track_index,
                                                                                        clip_index)
                                          : engine_clip_midi_instrument_preset(clip);
    EngineInstrumentParams clamped = engine_instrument_params_sanitize(preset, params);
    if (engine_instrument_params_equal(clip->instrument_params, clamped) && !clip->instrument_inherits_track) {
        return true;
    }
    clip->instrument_preset = preset;
    clip->instrument_params = clamped;
    clip->instrument_inherits_track = false;
    engine_request_rebuild_sources(engine);
    return true;
}

bool engine_clip_midi_set_instrument_param(Engine* engine,
                                           int track_index,
                                           int clip_index,
                                           EngineInstrumentParamId param,
                                           float value) {
    EngineClip* clip = engine_get_midi_clip_mutable(engine, track_index, clip_index);
    if (!clip) {
        return false;
    }
    EngineInstrumentPresetId preset = clip->instrument_inherits_track
                                          ? engine_clip_midi_effective_instrument_preset(engine,
                                                                                        track_index,
                                                                                        clip_index)
                                          : engine_clip_midi_instrument_preset(clip);
    EngineInstrumentParams base = clip->instrument_inherits_track
                                      ? engine_clip_midi_effective_instrument_params(engine, track_index, clip_index)
                                      : clip->instrument_params;
    EngineInstrumentParams params = engine_instrument_params_set(preset, base, param, value);
    return engine_clip_midi_set_instrument_params(engine, track_index, clip_index, params);
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
