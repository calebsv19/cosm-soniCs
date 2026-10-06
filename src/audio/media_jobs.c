#include "audio/media_jobs.h"
#include "core_workers.h"
#include "core_time.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Holds one immutable request and its release-published worker result until control retirement.
typedef struct MediaSlot {
    atomic_int state;
    atomic_bool cancel;
    AudioMediaResult result;
    struct stat identity;
    size_t max_sample_bytes;
} MediaSlot;
// Owns fixed admitted slots, shared worker backing and monotonic request IDs.
struct AudioMediaJobs {
    CoreWorkers workers;
    pthread_t thread;
    CoreWorkerTask tasks[AUDIO_MEDIA_JOB_CAPACITY];
    MediaSlot slots[AUDIO_MEDIA_JOB_CAPACITY];
    size_t max_sample_bytes;
    uint64_t next_id, submitted, completed, rejected;
};
// Compares source identity and modification metadata without hashing the sample payload on control.
static bool same_source(const struct stat* a, const struct stat* b) {
#if defined(__APPLE__)
    bool times = a->st_mtimespec.tv_sec == b->st_mtimespec.tv_sec && a->st_mtimespec.tv_nsec == b->st_mtimespec.tv_nsec &&
        a->st_ctimespec.tv_sec == b->st_ctimespec.tv_sec && a->st_ctimespec.tv_nsec == b->st_ctimespec.tv_nsec;
#else
    bool times = a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
#endif
    return times && a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size;
}
// Observes cancellation without touching engine or application state.
static bool slot_cancelled(void* user) { return atomic_load(&((MediaSlot*)user)->cancel); }
// Prepares private media and publishes only into its owned slot, never a shared completion pointer.
static void* prepare_media(void* user) {
    MediaSlot* slot = user;
    atomic_store(&slot->state, 2);
    uint64_t start = core_time_now_ns();
    struct stat current;
    bool stable = !stat(slot->result.request.path, &current) && same_source(&slot->identity, &current);
    if (stable && !slot_cancelled(slot)) {
        AudioMediaLoadControl control = {slot_cancelled, slot, slot->max_sample_bytes};
        if (slot->result.request.kind == AUDIO_MEDIA_JOB_PROBE)
            slot->result.ok = audio_media_probe_controlled(slot->result.request.path, &slot->result.info, &control);
        else if (slot->result.request.cached) slot->result.ok = true;
        else {
            slot->result.ok = audio_media_clip_load_controlled(slot->result.request.path,
                slot->result.request.sample_rate, &slot->result.clip, &control);
        }
    }
    if (stable && !slot_cancelled(slot) &&
        (slot->result.request.kind==AUDIO_MEDIA_JOB_PROBE || !slot->result.request.media_id[0])) {
        bool hashed=media_registry_prepare_entry(slot->result.request.path,&slot->result.registry_entry,slot_cancelled,slot);
        if (hashed && slot->result.request.kind!=AUDIO_MEDIA_JOB_PROBE) {
            const AudioMediaClip* clip=&slot->result.clip;
            slot->result.registry_entry.sample_rate=clip->sample_rate;
            slot->result.registry_entry.channels=clip->channels;
            slot->result.registry_entry.duration_seconds=clip->sample_rate ? (float)((double)clip->frame_count/clip->sample_rate) : 0;
        } else if (hashed) {
            slot->result.registry_entry.sample_rate=slot->result.info.sample_rate;
            slot->result.registry_entry.channels=slot->result.info.channels;
            slot->result.registry_entry.duration_seconds=slot->result.info.sample_rate ?
                (float)((double)slot->result.info.frame_count/slot->result.info.sample_rate) : 0;
        }
        if (hashed) memcpy(slot->result.request.media_id,slot->result.registry_entry.id,MEDIA_ID_MAX);
        else slot->result.ok=false;
    }
    stable = stable && !stat(slot->result.request.path, &current) && same_source(&slot->identity, &current);
    slot->result.source_changed = !stable;
    slot->result.cancelled = slot_cancelled(slot);
    slot->result.ok = slot->result.ok && stable && !slot->result.cancelled;
    if (!slot->result.ok) audio_media_clip_free(&slot->result.clip);
    slot->result.elapsed_ns = core_time_now_ns() - start;
    atomic_store(&slot->state, 3);
    return NULL;
}
// Allocates the bounded store and starts its single shared worker.
AudioMediaJobs* audio_media_jobs_create(size_t max_sample_bytes) {
    if (max_sample_bytes > SIZE_MAX / (AUDIO_MEDIA_JOB_CAPACITY * 3u)) return NULL;
    AudioMediaJobs* jobs = calloc(1, sizeof(*jobs));
    if (!jobs) return NULL;
    jobs->max_sample_bytes = max_sample_bytes ? max_sample_bytes : AUDIO_MEDIA_JOB_SAMPLE_BYTES;
    jobs->next_id = 1;
    for (size_t i=0; i<AUDIO_MEDIA_JOB_CAPACITY; ++i) {
        atomic_init(&jobs->slots[i].state, 0); atomic_init(&jobs->slots[i].cancel, false);
    }
    if (!core_workers_init(&jobs->workers, &jobs->thread, 1, jobs->tasks, AUDIO_MEDIA_JOB_CAPACITY, NULL)) {
        free(jobs); return NULL;
    }
    return jobs;
}
// Leaves queued contexts owned until the shared drain completes, including canceled requests.
void audio_media_jobs_destroy(AudioMediaJobs* jobs) {
    if (!jobs) return;
    audio_media_jobs_cancel(jobs, 0);
    core_workers_shutdown_with_mode(&jobs->workers, CORE_WORKERS_SHUTDOWN_DRAIN);
    for (size_t i=0; i<AUDIO_MEDIA_JOB_CAPACITY; ++i) audio_media_clip_free(&jobs->slots[i].result.clip);
    free(jobs);
}
// Copies validated identity into a free slot before release to the worker queue.
uint64_t audio_media_jobs_submit(AudioMediaJobs* jobs, const AudioMediaRequest* request) {
    if (!jobs) return 0;
    if (!request || !request->path[0] || !memchr(request->path,0,sizeof(request->path)) ||
        !memchr(request->media_id,0,sizeof(request->media_id)) || !request->generation ||
        request->kind < AUDIO_MEDIA_JOB_PROBE || request->kind > AUDIO_MEDIA_JOB_BOUNCE ||
        (request->kind != AUDIO_MEDIA_JOB_PROBE && request->sample_rate <= 0) || jobs->next_id == UINT64_MAX) goto reject;
    for (size_t i=0; i<AUDIO_MEDIA_JOB_CAPACITY; ++i) {
        MediaSlot* slot = &jobs->slots[i];
        if (atomic_load(&slot->state) != 0) continue;
        if (stat(request->path, &slot->identity) || !S_ISREG(slot->identity.st_mode)) goto reject;
        slot->result = (AudioMediaResult){.request=*request, .id=jobs->next_id++};
        slot->max_sample_bytes = jobs->max_sample_bytes;
        atomic_store(&slot->cancel, false); atomic_store(&slot->state, 1);
        if (!core_workers_submit(&jobs->workers, prepare_media, slot)) { atomic_store(&slot->state,0); goto reject; }
        ++jobs->submitted;
        return slot->result.id;
    }
reject:
    ++jobs->rejected; return 0;
}
// Marks slots canceled without freeing memory still visible to the worker.
void audio_media_jobs_cancel(AudioMediaJobs* jobs, uint64_t id) {
    if (!jobs) return;
    for (size_t i=0; i<AUDIO_MEDIA_JOB_CAPACITY; ++i) {
        MediaSlot* slot=&jobs->slots[i];
        if (atomic_load(&slot->state) && (!id || slot->result.id==id)) atomic_store(&slot->cancel,true);
    }
}
// Revalidates source identity at adoption time and transfers one completed result.
bool audio_media_jobs_poll(AudioMediaJobs* jobs, AudioMediaResult* out) {
    if (!jobs || !out) return false;
    for (size_t i=0; i<AUDIO_MEDIA_JOB_CAPACITY; ++i) {
        MediaSlot* slot=&jobs->slots[i];
        if (atomic_load(&slot->state)!=3) continue;
        struct stat current;
        if (stat(slot->result.request.path,&current) || !same_source(&slot->identity,&current)) slot->result.source_changed=true;
        slot->result.cancelled |= atomic_load(&slot->cancel);
        slot->result.ok &= !slot->result.cancelled && !slot->result.source_changed;
        if (!slot->result.ok) audio_media_clip_free(&slot->result.clip);
        *out=slot->result;
        slot->result=(AudioMediaResult){0};
        atomic_store(&slot->state,0); ++jobs->completed;
        return true;
    }
    return false;
}
// Reads result sizes only after publication and conservatively reserves two sample buffers plus sinc scratch.
AudioMediaJobStats audio_media_jobs_stats(const AudioMediaJobs* jobs) {
    AudioMediaJobStats stats={0}; if (!jobs) return stats;
    stats.submitted=jobs->submitted; stats.completed=jobs->completed; stats.rejected=jobs->rejected;
    for (size_t i=0; i<AUDIO_MEDIA_JOB_CAPACITY; ++i) {
        const MediaSlot* slot=&jobs->slots[i]; int state=atomic_load(&slot->state);
        if (!state) continue;
        ++stats.admitted;
        stats.reserved_bytes += 2 * jobs->max_sample_bytes + 16u * 1024u * 1024u;
        if (state==1) ++stats.queued;
        if (state==2) ++stats.running;
        if (state==3) { ++stats.ready; stats.ready_bytes += slot->result.clip.frame_count * slot->result.clip.channels * sizeof(float); }
    }
    return stats;
}
