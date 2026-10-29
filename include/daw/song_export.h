#pragma once
#include "daw/mix_capture.h"
#include "audio/media_registry.h"
// Describes an immutable job result and atomic progress without exposing worker-owned graph
// pointers.
typedef struct DawSongExportInfo {
    char id[96], path[512], state[32], content_id[MEDIA_ID_MAX];
    uint64_t generation, end, tail, done, total, bytes, fingerprint;
    int rate, channels;
    EngineBounceWavFormat format;
    bool normalize, verified, published, durable;
} DawSongExportInfo;
// Owns one shared worker, one active export and eight bounded completion records.
typedef struct DawSongExports DawSongExports;
// Admits a whole-song captured production export against the exact current mix guard.
DawActionStatus daw_song_export_start(struct AppState*, const DawMixSnapshot*,
                                      EngineBounceWavFormat, uint64_t tail, bool normalize,
                                      DawSongExportInfo*);
// Reads an addressed retained job, or the latest job for a null identifier, on control.
bool daw_song_export_get(struct AppState*, const char* id, DawSongExportInfo*);
// Requests cancellation before the atomic publication gate and reports late requests explicitly.
const char* daw_song_export_cancel(struct AppState*, const char* id);
// Reaps completed graphs on control without removing accepted files or terminal readback.
void daw_song_export_poll(struct AppState*);
// Cancels/drains workers before their engine/sample-cache owner is retired.
void daw_song_export_shutdown(struct AppState*);
