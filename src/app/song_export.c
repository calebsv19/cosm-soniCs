#define _POSIX_C_SOURCE 200809L
#include "app_state.h"
#include "daw/song_export.h"
#include "daw/midi_actions.h"
#include "engine/export_stream.h"
#include "core_workers.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>
// Tracks the cancellation/publication race independently of provisional frame progress.
enum { JOB_QUEUED = 1, JOB_RENDERING, JOB_PUBLISHING, JOB_CANCEL, JOB_DONE };
// Holds one immutable render owner and terminal metadata released through its phase fence.
typedef struct ExportSlot {
    atomic_int phase;
    atomic_uint_fast64_t done, total;
    EngineExportSnapshot* capture;
    DawMixSnapshot* guard;
    DawSongExportInfo info;
} ExportSlot;
// Keeps all graph disposal on control and bounds result retention independently of work duration.
struct DawSongExports {
    CoreWorkers worker;
    pthread_t thread;
    CoreWorkerTask task[1];
    ExportSlot slots[8];
    unsigned next;
    int latest;
};
// Publishes progress and arbitrates the final cancellation boundary before WAV publication.
static bool progress(uint64_t done, uint64_t total, void* context) {
    ExportSlot* s = context;
    atomic_store(&s->done, done);
    atomic_store(&s->total, total);
    if (done == total) {
        int expected = JOB_RENDERING;
        return atomic_compare_exchange_strong(&s->phase, &expected, JOB_PUBLISHING);
    }
    return atomic_load(&s->phase) == JOB_RENDERING;
}
// Checks the actual delivered encoding and exact byte size independently of the WAV writer.
static bool encoding(const DawSongExportInfo* i, uint64_t bytes) {
    unsigned width = i->format == ENGINE_BOUNCE_WAV_FLOAT32 ? 4 : 2;
    if (bytes != 44 + (i->end + i->tail) * i->channels * width)
        return false;
    FILE* file = fopen(i->path, "rb");
    if (!file)
        return false;
    unsigned char header[44];
    bool ok = fread(header, 1, sizeof(header), file) == sizeof(header);
    if (fclose(file))
        ok = false;
    return ok && !memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVEfmt ", 8) &&
           header[20] == (width == 4 ? 3 : 1) && !header[21] && header[34] == width * 8 &&
           !header[35];
}
// Renders and validates independently without touching application, engine-control or undo state.
static void* run(void* context) {
    ExportSlot* s = context;
    int expected = JOB_QUEUED;
    if (!atomic_compare_exchange_strong(&s->phase, &expected, JOB_RENDERING)) {
        strcpy(s->info.state, "cancelled");
        goto done;
    }
    EngineBounceStreamCallbacks cb = {.progress = progress, .user = s};
    DawSaveResult result = engine_export_run(s->capture, s->info.path, &cb);
    s->info.published = result != DAW_SAVE_FAILED;
    s->info.durable = result == DAW_SAVE_SYNCED;
    if (result != DAW_SAVE_FAILED) {
        AudioMediaInfo media;
        MediaRegistryEntry identity = {0};
        s->info.verified = audio_media_probe(s->info.path, &media) &&
                           media.frame_count == s->info.end + s->info.tail &&
                           media.channels == s->info.channels &&
                           media.sample_rate == s->info.rate &&
                           media_registry_prepare_entry(s->info.path, &identity, NULL, NULL) &&
                           encoding(&s->info, identity.file_size);
        if (s->info.verified) {
            s->info.bytes = identity.file_size;
            strcpy(s->info.content_id, identity.id);
        }
        strcpy(s->info.state, s->info.verified
                                  ? (s->info.durable ? "completed" : "published_unsynced")
                                  : "published_unverified");
    } else
        strcpy(s->info.state, atomic_load(&s->phase) == JOB_CANCEL ? "cancelled" : "failed");
done:
    if (!s->info.published) {
        char directory[512];
        strcpy(directory, s->info.path);
        char* slash = strrchr(directory, '/');
        if (slash) {
            *slash = 0;
            (void)rmdir(directory);
        }
    }
    atomic_store_explicit(&s->phase, JOB_DONE, memory_order_release);
    return NULL;
}
// Retires completed captured plans after their worker has stopped reading sample pins.
void daw_song_export_poll(AppState* state) {
    if (!state || !state->exports)
        return;
    for (int i = 0; i < 8; ++i) {
        ExportSlot* s = &state->exports->slots[i];
        if (atomic_load_explicit(&s->phase, memory_order_acquire) == JOB_DONE && s->capture) {
            engine_export_free(s->capture);
            s->capture = NULL;
            daw_mix_snapshot_free(s->guard);
            s->guard = NULL;
        }
    }
}
// Returns a copied record only after accepting the worker completion fence.
bool daw_song_export_get(AppState* state, const char* id, DawSongExportInfo* out) {
    if (!state || !state->exports || !out)
        return false;
    daw_song_export_poll(state);
    DawSongExports* owner = state->exports;
    for (int i = 0; i < 8; ++i) {
        ExportSlot* s = &owner->slots[i];
        if (!s->info.id[0] || (id ? strcmp(s->info.id, id) : i != owner->latest))
            continue;
        int phase = atomic_load_explicit(&s->phase, memory_order_acquire);
        if (phase == JOB_DONE)
            *out = s->info;
        else { // Only immutable fields are read while the worker writes terminal metadata.
            *out = (DawSongExportInfo){.generation = s->info.generation,
                                       .end = s->info.end,
                                       .tail = s->info.tail,
                                       .fingerprint = s->info.fingerprint,
                                       .rate = s->info.rate,
                                       .channels = s->info.channels,
                                       .format = s->info.format,
                                       .normalize = s->info.normalize};
            strcpy(out->id, s->info.id);
            strcpy(out->path, s->info.path);
            strcpy(out->state, phase == JOB_QUEUED       ? "queued"
                               : phase == JOB_RENDERING  ? "rendering"
                               : phase == JOB_PUBLISHING ? "publishing"
                                                         : "cancelling");
        }
        out->done = atomic_load(&s->done);
        out->total = atomic_load(&s->total);
        return true;
    }
    return false;
}
// Cancels only the exact retained job and refuses cancellation once publication has won the race.
const char* daw_song_export_cancel(AppState* state, const char* id) {
    if (!state || !state->exports || !id)
        return "unknown_export";
    for (int i = 0; i < 8; ++i) {
        ExportSlot* s = &state->exports->slots[i];
        if (strcmp(s->info.id, id))
            continue;
        int phase = atomic_load(&s->phase);
        for (;;) {
            if (phase == JOB_DONE)
                return "already_finished";
            if (phase == JOB_PUBLISHING)
                return "publication_started";
            if (phase == JOB_CANCEL)
                return "cancelling";
            if (atomic_compare_exchange_weak(&s->phase, &phase, JOB_CANCEL))
                return "cancel_requested";
        }
    }
    return "unknown_export";
}
// Captures the whole active arrangement while bounding duration, file size and available staging
// space.
DawActionStatus daw_song_export_start(AppState* state, const DawMixSnapshot* expected,
                                      EngineBounceWavFormat format, uint64_t tail, bool normalize,
                                      DawSongExportInfo* out) {
    if (!state || !state->engine || !out || !expected)
        return DAW_ACTION_INVALID;
    if (!engine_is_control_thread(state->engine))
        return DAW_ACTION_WRONG_THREAD;
    if (!daw_mix_snapshot_current(state, expected))
        return DAW_ACTION_STALE_TARGET;
    if (daw_midi_actions_busy(state))
        return DAW_ACTION_BUSY;
    int rate = engine_get_config(state->engine)->sample_rate;
    uint64_t end = 0;
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    for (int t = 0; t < engine_get_track_count(state->engine); ++t)
        for (int c = 0; c < tracks[t].clip_count; ++c)
            if (tracks[t].clips[c].active) {
                uint64_t length = engine_clip_get_total_frames(state->engine, t, c),
                         start = tracks[t].clips[c].timeline_start_frames;
                if (length > UINT64_MAX - start)
                    return DAW_ACTION_INVALID;
                if (start + length > end)
                    end = start + length;
            }
    if (rate < 1 || !end || end > (uint64_t)rate * 3600 || tail > (uint64_t)rate * 60 ||
        (format != ENGINE_BOUNCE_WAV_PCM16 && format != ENGINE_BOUNCE_WAV_FLOAT32))
        return DAW_ACTION_INVALID;
    daw_song_export_poll(state);
    if (state->exports)
        for (int i = 0; i < 8; ++i) {
            int phase = atomic_load(&state->exports->slots[i].phase);
            if (phase && phase != JOB_DONE)
                return DAW_ACTION_BUSY;
        }
    if (!daw_data_path_is_safe_write_root(state->data_paths.output_root))
        return DAW_ACTION_INVALID;
    char root[512];
    if (snprintf(root, sizeof(root), "%s/Exports", state->data_paths.output_root) >=
            (int)sizeof(root) ||
        !daw_data_path_ensure_directory_recursive(root))
        return DAW_ACTION_ENGINE_REJECTED;
    struct statvfs space;
    uint64_t required =
        (end + tail) * 2 * (sizeof(float) + (format == ENGINE_BOUNCE_WAV_FLOAT32 ? 4 : 2)) +
        UINT64_C(67108864);
    if (statvfs(root, &space) || required > (uint64_t)space.f_bavail * space.f_frsize)
        return DAW_ACTION_HISTORY_UNAVAILABLE;
    if (!state->exports) {
        DawSongExports* owner = calloc(1, sizeof(*owner));
        if (!owner)
            return DAW_ACTION_HISTORY_UNAVAILABLE;
        owner->latest = -1;
        for (int i = 0; i < 8; ++i) {
            atomic_init(&owner->slots[i].phase, 0);
            atomic_init(&owner->slots[i].done, 0);
            atomic_init(&owner->slots[i].total, 0);
        }
        if (!core_workers_init(&owner->worker, &owner->thread, 1, owner->task, 1, NULL)) {
            free(owner);
            return DAW_ACTION_HISTORY_UNAVAILABLE;
        }
        state->exports = owner;
    }
    DawSongExports* owner = state->exports;
    unsigned slot = owner->next % 8;
    ExportSlot* s = &owner->slots[slot];
    DawMixSnapshot* guard = NULL;
    if (daw_action_get_mix_snapshot(state, &guard) != DAW_ACTION_UNCHANGED)
        return DAW_ACTION_ENGINE_REJECTED;
    EngineBounceOptions options = engine_bounce_options_default();
    options.tail_frames = tail;
    options.normalize_if_clipping = normalize;
    EngineExportSnapshot* capture =
        engine_export_prepare(state->engine, 0, end, &options, format, true);
    if (!capture) {
        daw_mix_snapshot_free(guard);
        return DAW_ACTION_ENGINE_REJECTED;
    }
    char directory[512];
    if (snprintf(directory, sizeof(directory), "%s/song-XXXXXX", root) >= (int)sizeof(directory) ||
        strlen(directory) + 10 >= sizeof(s->info.path) || !mkdtemp(directory)) {
        engine_export_free(capture);
        daw_mix_snapshot_free(guard);
        return DAW_ACTION_ENGINE_REJECTED;
    }
    s->info = (DawSongExportInfo){.generation = guard->generation,
                                  .end = end,
                                  .tail = tail,
                                  .rate = rate,
                                  .channels = 2,
                                  .format = format,
                                  .normalize = normalize,
                                  .fingerprint = guard->settings_fingerprint};
    snprintf(s->info.id, sizeof(s->info.id), "%llu:export:%u:%s",
             (unsigned long long)guard->generation, owner->next + 1, strrchr(directory, '/') + 1);
    snprintf(s->info.path, sizeof(s->info.path), "%s/song.wav", directory);
    s->guard = guard;
    s->capture = capture;
    atomic_store(&s->done, 0);
    atomic_store(&s->total, 0);
    atomic_store(&s->phase, JOB_QUEUED);
    if (!core_workers_submit(&owner->worker, run, s)) {
        atomic_store(&s->phase, JOB_DONE);
        engine_export_free(capture);
        s->capture = NULL;
        daw_mix_snapshot_free(guard);
        s->guard = NULL;
        rmdir(directory);
        return DAW_ACTION_ENGINE_REJECTED;
    }
    owner->latest = slot;
    ++owner->next;
    daw_song_export_get(state, s->info.id, out);
    return DAW_ACTION_APPLIED;
}
// Joins the export worker before retiring its graph and engine-cache owner, preserving published
// files.
void daw_song_export_shutdown(AppState* state) {
    if (!state || !state->exports)
        return;
    DawSongExports* owner = state->exports;
    for (int i = 0; i < 8; ++i)
        if (owner->slots[i].info.id[0])
            daw_song_export_cancel(state, owner->slots[i].info.id);
    core_workers_shutdown_with_mode(&owner->worker, CORE_WORKERS_SHUTDOWN_DRAIN);
    daw_song_export_poll(state);
    free(owner);
    state->exports = NULL;
}
