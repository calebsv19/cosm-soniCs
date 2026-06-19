#include "engine/track_role.h"

EngineTrackRole engine_track_role_from_track(const EngineTrack* track) {
    if (!track || track->clip_count <= 0 || !track->clips) {
        return ENGINE_TRACK_ROLE_EMPTY;
    }

    bool has_audio = false;
    bool has_midi = false;
    for (int i = 0; i < track->clip_count; ++i) {
        switch (engine_clip_get_kind(&track->clips[i])) {
            case ENGINE_CLIP_KIND_AUDIO:
                has_audio = true;
                break;
            case ENGINE_CLIP_KIND_MIDI:
                has_midi = true;
                break;
        }
        if (has_audio && has_midi) {
            return ENGINE_TRACK_ROLE_MIXED;
        }
    }

    if (has_audio) {
        return ENGINE_TRACK_ROLE_AUDIO;
    }
    if (has_midi) {
        return ENGINE_TRACK_ROLE_MIDI;
    }
    return ENGINE_TRACK_ROLE_EMPTY;
}

bool engine_track_role_resolve(const Engine* engine, int track_index, EngineTrackRole* out_role) {
    if (out_role) {
        *out_role = ENGINE_TRACK_ROLE_EMPTY;
    }
    if (!engine || track_index < 0) {
        return false;
    }
    const EngineTrack* tracks = engine_get_tracks(engine);
    int track_count = engine_get_track_count(engine);
    if (!tracks || track_index >= track_count) {
        return false;
    }
    if (out_role) {
        *out_role = engine_track_role_from_track(&tracks[track_index]);
    }
    return true;
}

const char* engine_track_role_label(EngineTrackRole role) {
    switch (role) {
        case ENGINE_TRACK_ROLE_EMPTY:
            return "empty";
        case ENGINE_TRACK_ROLE_AUDIO:
            return "audio";
        case ENGINE_TRACK_ROLE_MIDI:
            return "midi";
        case ENGINE_TRACK_ROLE_MIXED:
            return "mixed";
    }
    return "empty";
}
