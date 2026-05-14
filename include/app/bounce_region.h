#pragma once

#include "app_state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

const char* daw_bounce_library_root(const AppState* state);
bool daw_bounce_next_path_for_directory(const char* directory, char* out, size_t len);
bool daw_bounce_next_path_for_state(const AppState* state, char* out, size_t len);
bool daw_bounce_insert_audio_track(AppState* state,
                                   const char* wav_path,
                                   uint64_t start_frame,
                                   int* out_track_index,
                                   int* out_clip_index);
