#include "app/media_import.h"
#include "app_state.h"
#include "input/timeline_selection.h"
#include "input/inspector_input.h"
#include "engine/sampler.h"
#include "undo/undo_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Owns the asynchronous store and generation independently of shallow-copied project candidates.
struct DawMediaImport {
    AudioMediaJobs* jobs;
    uint64_t generation, inserted, failed, cancelled, stale;
};
// Starts one persistent decoder owner before interactive imports are admitted.
bool daw_media_import_init(AppState* state) {
    if (!state) return false;
    if (state->media_import) return true;
    DawMediaImport* owner=calloc(1,sizeof(*owner)); if (!owner) return false;
    owner->jobs=audio_media_jobs_create(0);
    if (!owner->jobs) { free(owner); return false; }
    owner->generation=1; state->media_import=owner; return true;
}
// Drains canceled jobs that never reference the retiring project or engine.
void daw_media_import_shutdown(AppState* state) {
    if (!state || !state->media_import) return;
    audio_media_jobs_destroy(state->media_import->jobs);
    free(state->media_import); state->media_import=NULL;
}
// Changes the lifetime generation before old engine destruction; failed restore never calls this.
void daw_media_import_invalidate(AppState* state) {
    if (!state || !state->media_import) return;
    audio_media_jobs_cancel(state->media_import->jobs,0);
    if (state->media_import->generation) ++state->media_import->generation;
    // Exhaustion retires admission permanently rather than reusing an old project identity.
}
// Reports user-facing insertion state in the library and, when idle, the recording controls.
static void import_status(AppState* state, AudioMediaJobKind kind, const char* text, const char* path) {
    const char* name=path ? strrchr(path,'/') : NULL; name=name ? name+1 : path;
    snprintf(state->library.status_line,sizeof(state->library.status_line),"%s%s%s",text,name ? ": " : "",name ? name : "");
    if (kind==AUDIO_MEDIA_JOB_RECORDING && !daw_audio_recording_is_active(&state->audio_recording))
        SDL_strlcpy(state->audio_recording.status_message,state->library.status_line,sizeof(state->audio_recording.status_message));
    SDL_Log("media_import: %s (%s)",text,path ? path : "");
}
// Admits owned coordinates without creating a track or reading sample data on the main thread.
uint64_t daw_media_import_submit(AppState* state, const char* path, const char* media_id,
                                 int target_track, uint64_t start_frame, AudioMediaJobKind kind) {
    if (!state || !state->engine || !path || !*path || strlen(path)>=512 ||
        (media_id && strlen(media_id)>=MEDIA_ID_MAX) || kind==AUDIO_MEDIA_JOB_PROBE ||
        target_track<0 || target_track>engine_get_track_count(state->engine)) return 0;
    if (!daw_media_import_init(state)) { import_status(state,kind,"Import unavailable; source file preserved",path); return 0; }
    AudioMediaRequest request={.generation=state->media_import->generation,.kind=kind,
        .sample_rate=engine_get_config(state->engine)->sample_rate,.start_frame=start_frame,
        .new_track=target_track==engine_get_track_count(state->engine)};
    if (!request.new_track) request.track_id=engine_get_tracks(state->engine)[target_track].runtime_id;
    SDL_strlcpy(request.path,path,sizeof(request.path));
    const MediaRegistryEntry* known=media_id && *media_id ? NULL : media_registry_find_by_path(&state->media_registry,path);
    SDL_strlcpy(request.media_id,media_id && *media_id ? media_id : known ? known->id : "",sizeof(request.media_id));
    request.cached=engine_has_cached_media(state->engine,path,request.media_id);
    uint64_t id=audio_media_jobs_submit(state->media_import->jobs,&request);
    import_status(state,kind,id ? "Loading (Ctrl/Cmd+Esc cancels)" : "Import queue full or source unavailable; file preserved",path);
    return id;
}
// Captures the final published clip using the existing undo command representation.
static void push_insert_undo(AppState* state, int track, int index) {
    const EngineClip* clip=&engine_get_tracks(state->engine)[track].clips[index];
    UndoCommand cmd={0}; cmd.type=UNDO_CMD_CLIP_ADD_REMOVE;
    cmd.data.clip_add_remove.added=true; cmd.data.clip_add_remove.track_index=track;
    cmd.data.clip_add_remove.sampler=clip->sampler;
    SessionClip* saved=&cmd.data.clip_add_remove.clip;
    saved->kind=ENGINE_CLIP_KIND_AUDIO;
    SDL_strlcpy(saved->media_id,engine_clip_get_media_id(clip),sizeof(saved->media_id));
    SDL_strlcpy(saved->media_path,engine_clip_get_media_path(clip),sizeof(saved->media_path));
    SDL_strlcpy(saved->name,clip->name,sizeof(saved->name));
    saved->start_frame=clip->timeline_start_frames; saved->duration_frames=clip->duration_frames;
    saved->offset_frames=clip->offset_frames; saved->fade_in_frames=clip->fade_in_frames;
    saved->fade_out_frames=clip->fade_out_frames; saved->fade_in_curve=clip->fade_in_curve;
    saved->fade_out_curve=clip->fade_out_curve; saved->gain=clip->gain;
    (void)undo_manager_push(&state->undo,&cmd);
}
// Resolves stable destination identity and publishes through existing engine transaction/undo behavior.
static bool publish_import(AppState* state, AudioMediaResult* result, bool* deferred) {
    *deferred=false;
    const AudioMediaRequest* request=&result->request;
    if (!state->engine || engine_get_config(state->engine)->sample_rate!=request->sample_rate) return false;
    int track=-1, count=engine_get_track_count(state->engine);
    if (request->new_track) track=count;
    else for (int i=0;i<count;++i) if (engine_get_tracks(state->engine)[i].runtime_id==request->track_id) { track=i; break; }
    if (track<0) return false;
    int index=-1;
    if (request->cached) {
        bool available=false;
        if (!engine_add_cached_clip(state->engine,track,request->path,request->media_id,request->start_frame,&index,&available)) {
            if (!available) {
                AudioMediaRequest retry=*request; retry.cached=false;
                *deferred=audio_media_jobs_submit(state->media_import->jobs,&retry)!=0;
            }
            return false;
        }
    } else if (!engine_add_prepared_clip(state->engine,track,request->path,request->media_id,
                                         request->start_frame,&result->clip,&index)) return false;
    if (request->kind==AUDIO_MEDIA_JOB_BOUNCE) engine_track_set_name(state->engine,track,"Bounce");
    if (request->kind==AUDIO_MEDIA_JOB_LIBRARY) {
        EngineSamplerSource* sampler=engine_get_tracks(state->engine)[track].clips[index].sampler;
        int resolved=index;
        if (engine_track_apply_no_overlap(state->engine,track,sampler,&resolved) && resolved>=0) index=resolved;
    }
    push_insert_undo(state,track,index);
    timeline_selection_set_single(state,track,index);
    if (request->kind==AUDIO_MEDIA_JOB_LIBRARY)
        inspector_input_show(state,track,index,&engine_get_tracks(state->engine)[track].clips[index]);
    library_browser_refresh_project_usage(&state->library,state->engine);
    return true;
}
// Publishes matching metadata only into the current scan's matching file, never an obsolete item index.
static bool publish_probe(AppState* state, const AudioMediaResult* result) {
    if (result->request.library_generation!=state->library.scan_generation) return false;
    for (int i=0;i<state->library.count;++i) {
        LibraryItem* item=&state->library.items[i]; char path[512];
        snprintf(path,sizeof(path),"%s/%s",state->library.directory,item->name);
        if (!strcmp(path,result->request.path)) {
            if (result->registry_entry.id[0] && !result->cancelled && !result->source_changed)
                SDL_strlcpy(item->media_id,result->registry_entry.id,sizeof(item->media_id));
            item->metadata_loaded=!result->cancelled && !result->source_changed;
            item->duration_seconds=result->ok ? (float)((double)result->info.frame_count/result->info.sample_rate) : 0;
            return true;
        }
    }
    return false;
}
// Retires one result per update and uses only one admitted slot for low-priority metadata probing.
bool daw_media_import_poll(AppState* state) {
    if (!state || !state->media_import) return false;
    engine_collect_retired_media(state->engine);
    DawMediaImport* owner=state->media_import; AudioMediaResult result={0}; bool changed=false;
    if (audio_media_jobs_poll(owner->jobs,&result)) {
        if (result.request.generation!=owner->generation) ++owner->stale;
        else if (result.request.kind==AUDIO_MEDIA_JOB_PROBE) {
            changed=publish_probe(state,&result);
            if (changed && result.registry_entry.id[0] && !result.cancelled && !result.source_changed)
                (void)media_registry_adopt_entry(&state->media_registry,&result.registry_entry);
        }
        else {
            if (result.ok && result.registry_entry.id[0])
                (void)media_registry_adopt_entry(&state->media_registry,&result.registry_entry);
            bool deferred=false;
            bool inserted=result.ok && publish_import(state,&result,&deferred);
            if (deferred) { audio_media_clip_free(&result.clip); return false; }
            if (inserted) ++owner->inserted;
            else if (result.cancelled) ++owner->cancelled;
            else ++owner->failed;
            import_status(state,result.request.kind,inserted ? "Imported" :
                result.cancelled ? "Insertion cancelled; source file preserved" :
                result.source_changed ? "Source changed; retry import" : "Insertion failed or exceeds memory limit; source file preserved",result.request.path);
            changed=true;
        }
        audio_media_clip_free(&result.clip);
    }
    if (audio_media_jobs_stats(owner->jobs).admitted==0 && owner->generation) {
        for (int i=0;i<state->library.count;++i) {
            LibraryItem* item=&state->library.items[i]; if (item->metadata_requested) continue;
            AudioMediaRequest request={.generation=owner->generation,.kind=AUDIO_MEDIA_JOB_PROBE,
                .library_generation=state->library.scan_generation};
            snprintf(request.path,sizeof(request.path),"%s/%s",state->library.directory,item->name);
            item->metadata_requested=true;
            (void)audio_media_jobs_submit(owner->jobs,&request); break;
        }
    }
    return changed;
}
// Cancels pending publication without removing any external source or durable recording/export.
void daw_media_import_cancel(AppState* state) {
    if (!state || !state->media_import) return;
    audio_media_jobs_cancel(state->media_import->jobs,0);
    import_status(state,AUDIO_MEDIA_JOB_LIBRARY,"Cancelling pending imports; source files preserved",NULL);
}
// Separates admitted/ready job ownership from engine cache pins.
DawMediaImportStats daw_media_import_stats(const AppState* state) {
    DawMediaImportStats stats={0}; if (!state || !state->media_import) return stats;
    const DawMediaImport* owner=state->media_import;
    stats.jobs=audio_media_jobs_stats(owner->jobs); stats.generation=owner->generation;
    stats.inserted=owner->inserted; stats.failed=owner->failed; stats.cancelled=owner->cancelled; stats.stale=owner->stale;
    stats.pinned_bytes=engine_media_resident_bytes(state->engine); return stats;
}
