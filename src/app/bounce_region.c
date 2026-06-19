#include "app/bounce_region.h"

#include "daw/data_paths.h"
#include "engine/engine.h"
#include "input/timeline_selection.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

const char* daw_bounce_library_root(const AppState* state) {
    if (!state) {
        return DAW_DATA_PATH_DEFAULT_INPUT_ROOT;
    }
    if (state->library.directory[0] != '\0') {
        return state->library.directory;
    }
    return daw_data_paths_library_root(&state->data_paths);
}

bool daw_bounce_next_path_for_directory(const char* directory, char* out, size_t len) {
    if (!directory || directory[0] == '\0' || !out || len == 0 ||
        !daw_data_path_is_directory(directory)) {
        return false;
    }
    for (int i = 0; i < 10000; ++i) {
        char name[64];
        if (i == 0) {
            snprintf(name, sizeof(name), "bounce.wav");
        } else {
            snprintf(name, sizeof(name), "bounce%d.wav", i);
        }
        int written = snprintf(out, len, "%s/%s", directory, name);
        if (written < 0 || (size_t)written >= len) {
            return false;
        }
        if (!daw_data_path_exists(out)) {
            return true;
        }
    }
    return false;
}

bool daw_bounce_next_path_for_state(const AppState* state, char* out, size_t len) {
    return daw_bounce_next_path_for_directory(daw_bounce_library_root(state), out, len);
}

bool daw_bounce_insert_audio_track(AppState* state,
                                   const char* wav_path,
                                   uint64_t start_frame,
                                   int* out_track_index,
                                   int* out_clip_index) {
    if (!state || !state->engine || !wav_path || wav_path[0] == '\0') {
        return false;
    }

    MediaRegistryEntry media_entry = {0};
    const char* media_id = NULL;
    if (media_registry_ensure_for_path(&state->media_registry, wav_path, "Bounce", &media_entry)) {
        media_id = media_entry.id[0] != '\0' ? media_entry.id : NULL;
        (void)media_registry_save(&state->media_registry);
    } else {
        SDL_Log("Bounce insert warning: failed to register %s; adding clip by path only.", wav_path);
    }

    int track_index = engine_add_track(state->engine);
    if (track_index < 0) {
        SDL_Log("Bounce insert skipped: unable to create bounce track.");
        return false;
    }
    engine_track_set_name(state->engine, track_index, "Bounce");

    int clip_index = -1;
    if (!engine_add_clip_to_track_with_id(state->engine,
                                          track_index,
                                          wav_path,
                                          media_id,
                                          start_frame,
                                          &clip_index)) {
        SDL_Log("Bounce insert warning: failed to add bounced clip to track %d.", track_index);
        engine_remove_track(state->engine, track_index);
        return false;
    }

    timeline_selection_set_single(state, track_index, clip_index);

    if (out_track_index) {
        *out_track_index = track_index;
    }
    if (out_clip_index) {
        *out_clip_index = clip_index;
    }
    return true;
}
