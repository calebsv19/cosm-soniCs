#pragma once

#include "engine/engine.h"

#include <stdbool.h>

typedef enum {
    ENGINE_TRACK_ROLE_EMPTY = 0,
    ENGINE_TRACK_ROLE_AUDIO,
    ENGINE_TRACK_ROLE_MIDI,
    ENGINE_TRACK_ROLE_MIXED
} EngineTrackRole;

EngineTrackRole engine_track_role_from_track(const EngineTrack* track);
bool engine_track_role_resolve(const Engine* engine, int track_index, EngineTrackRole* out_role);
const char* engine_track_role_label(EngineTrackRole role);
