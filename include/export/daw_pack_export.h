#pragma once

#include "engine/engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct AppState;

// Defines the binary header chunk for DAW profile packs.
typedef struct {
    uint32_t version;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t samples_per_pixel;
    uint64_t point_count;
    uint64_t start_frame;
    uint64_t end_frame;
    uint64_t project_duration_frames;
} DawPackHeader;

// Enumerates timeline marker categories exported by DAW profile packs.
typedef enum {
    DAW_PACK_MARKER_TEMPO = 1,
    DAW_PACK_MARKER_TIME_SIGNATURE = 2,
    DAW_PACK_MARKER_LOOP_START = 3,
    DAW_PACK_MARKER_LOOP_END = 4,
    DAW_PACK_MARKER_PLAYHEAD = 5
} DawPackMarkerKind;

// Defines one exported marker row for the MRKS chunk.
typedef struct {
    uint64_t frame;
    double beat;
    uint32_t kind;
    uint32_t reserved;
    double value_a;
    double value_b;
} DawPackMarker;

// Builds the sibling .pack path for an existing .wav path.
bool daw_pack_path_from_wav(const char* wav_path, char* out_pack_path, size_t out_pack_path_len);

// Writes a DAW profile .pack from the in-memory bounced buffer and timeline metadata.
bool daw_pack_export_from_bounce(const char* pack_path,
                                 const struct AppState* state,
                                 const EngineBounceBuffer* bounce,
                                 uint64_t start_frame,
                                 uint64_t end_frame,
                                 uint64_t project_duration_frames);
