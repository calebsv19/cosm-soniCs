#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio/media_clip.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "session.h"

uint64_t timeline_clip_frame_count(const EngineClip* clip);
uint64_t timeline_clip_midi_content_end_frame(const EngineClip* clip);
uint64_t timeline_clip_midi_min_duration_frames(const EngineClip* clip);
bool timeline_clip_is_timeline_region(const EngineClip* clip);
bool timeline_session_clip_from_engine(const EngineClip* clip, SessionClip* out_clip);
void timeline_session_clip_clear(SessionClip* clip);
