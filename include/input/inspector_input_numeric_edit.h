#pragma once

#include "app_state.h"
#include "engine/engine.h"

#include <stdbool.h>
#include <stdint.h>

bool inspector_numeric_is_editing(const ClipInspectorEditState* edit);
char* inspector_numeric_active_buffer(ClipInspectorEditState* edit);
void inspector_numeric_clear_edit(AppState* state);
double inspector_numeric_clip_sample_rate(const AppState* state, const EngineClip* clip);
uint64_t inspector_numeric_clip_total_frames(const AppState* state, const EngineClip* clip);
uint64_t inspector_numeric_clip_duration_frames(const AppState* state, const EngineClip* clip);
void inspector_numeric_begin_edit(AppState* state, const EngineClip* clip, bool* flag);
bool inspector_numeric_commit_edit(AppState* state);
