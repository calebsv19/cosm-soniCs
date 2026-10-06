#pragma once
#include "audio/media_clip.h"
#include "audio/media_registry.h"
#include <stddef.h>
#include <stdint.h>

#define AUDIO_MEDIA_JOB_CAPACITY 4
#define AUDIO_MEDIA_JOB_SAMPLE_BYTES (64u * 1024u * 1024u)
// Distinguishes metadata requests from insertion workflows without retaining UI pointers.
typedef enum AudioMediaJobKind { AUDIO_MEDIA_JOB_PROBE, AUDIO_MEDIA_JOB_LIBRARY,
    AUDIO_MEDIA_JOB_RECORDING, AUDIO_MEDIA_JOB_BOUNCE } AudioMediaJobKind;
// Owns request identity and stable insertion coordinates independently of any engine lifetime.
typedef struct AudioMediaRequest {
    char path[512];
    char media_id[MEDIA_ID_MAX];
    uint64_t generation, track_id, start_frame, library_generation;
    int sample_rate;
    AudioMediaJobKind kind;
    bool new_track, cached; // Cached requests revalidate residency on control without decoding.
} AudioMediaRequest;
// Owns a completed private result until the control thread adopts or frees its samples.
typedef struct AudioMediaResult {
    AudioMediaRequest request;
    AudioMediaClip clip;
    AudioMediaInfo info;
    MediaRegistryEntry registry_entry;
    uint64_t id, elapsed_ns;
    bool ok, cancelled, source_changed;
} AudioMediaResult;
// Reports admitted work separately from ready decoded bytes and conservative reserved storage.
typedef struct AudioMediaJobStats {
    size_t admitted, queued, running, ready, ready_bytes, reserved_bytes;
    uint64_t submitted, completed, rejected;
} AudioMediaJobStats;
// Opaque request owner uses one existing shared worker and fixed DAW-owned result slots.
typedef struct AudioMediaJobs AudioMediaJobs;
// Starts one decoder worker; zero sample limit selects the documented default.
AudioMediaJobs* audio_media_jobs_create(size_t max_sample_bytes);
// Cancels and drains every slot before freeing the owner, independent of engine lifetime.
void audio_media_jobs_destroy(AudioMediaJobs* jobs);
// Copies one request; returns zero on invalid input, unavailable source or full admission.
uint64_t audio_media_jobs_submit(AudioMediaJobs* jobs, const AudioMediaRequest* request);
// Cancels one admitted request, or every request when id is zero.
void audio_media_jobs_cancel(AudioMediaJobs* jobs, uint64_t id);
// Moves one ready result to the caller; canceled results never expose decoded storage.
bool audio_media_jobs_poll(AudioMediaJobs* jobs, AudioMediaResult* out);
// Snapshots bounded queue/result ownership on the control thread.
AudioMediaJobStats audio_media_jobs_stats(const AudioMediaJobs* jobs);
