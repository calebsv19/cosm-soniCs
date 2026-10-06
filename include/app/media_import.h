#pragma once
#include "audio/media_jobs.h"
struct AppState;
// Holds application-lifetime ownership outside replaceable engine/project contents.
typedef struct DawMediaImport DawMediaImport;
// Reports asynchronous insertion outcomes without conflating saved files with clip publication.
typedef struct DawMediaImportStats {
    AudioMediaJobStats jobs;
    uint64_t generation, inserted, failed, cancelled, stale;
    size_t pinned_bytes;
} DawMediaImportStats;
// Starts the application import owner; repeated calls preserve admitted work.
bool daw_media_import_init(struct AppState* state);
// Cancels and drains private jobs before releasing the application owner.
void daw_media_import_shutdown(struct AppState* state);
// Invalidates admitted work only when a project replacement commits successfully.
void daw_media_import_invalidate(struct AppState* state);
// Copies a user insertion request and reports admission failure without synchronous decoding.
uint64_t daw_media_import_submit(struct AppState* state, const char* path, const char* media_id,
                                 int target_track, uint64_t start_frame, AudioMediaJobKind kind);
// Publishes at most one completion and admits at most one metadata probe per update.
bool daw_media_import_poll(struct AppState* state);
// Cancels current pending jobs while preserving any already published WAV files.
void daw_media_import_cancel(struct AppState* state);
// Reports worker ownership and existing non-evictable decoded cache residency.
DawMediaImportStats daw_media_import_stats(const struct AppState* state);
