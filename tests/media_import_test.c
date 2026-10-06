#include "app_state.h"
#include "app/media_import.h"
#include "engine/engine_internal.h"
#include "core_time.h"
#include "session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// Writes a compact PCM16 fixture with a selectable signed constant and exact duration.
static void fixture(const char* path, unsigned frames, short value) {
    unsigned char h[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
        0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',0,0,0,0};
    unsigned size=frames*2; for(int i=0;i<4;++i) {h[4+i]=(size+36)>>(8*i);h[40+i]=size>>(8*i);}
    FILE* f=fopen(path,"wb");assert(f);assert(fwrite(h,1,44,f)==44);
    for(unsigned i=0;i<frames;++i){assert(fputc(value&255,f)!=EOF);assert(fputc((value>>8)&255,f)!=EOF);}assert(!fclose(f));
}
// Waits for completion publication with a finite deadline and no concurrent input mutation.
static void drain(AppState* state) {
    uint64_t start=core_time_now_ns();
    while(daw_media_import_stats(state).jobs.admitted) {
        daw_media_import_poll(state); assert(core_time_now_ns()-start<UINT64_C(15000000000)); SDL_Delay(1);
    }
}
// Waits until decoding completes without allowing main-thread publication.
static void ready(AppState* state) {
    uint64_t start=core_time_now_ns();
    while(!daw_media_import_stats(state).jobs.ready) {assert(core_time_now_ns()-start<UINT64_C(15000000000));SDL_Delay(1);}
}
// Exercises real asynchronous insertion, project transactions, destination identity and saved recording outcomes.
int main(void) {
    SDL_setenv("SDL_AUDIODRIVER","dummy",1); assert(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER)==0);
    char directory[]="/tmp/daw_import_XXXXXX";assert(mkdtemp(directory));char path[512];snprintf(path,sizeof(path),"%s/source.wav",directory);fixture(path,44100,16384);
    AppState* state=calloc(1,sizeof(*state));assert(state);config_set_defaults(&state->runtime_cfg);
    state->runtime_cfg.sample_rate=48000;state->runtime_cfg.block_size=128;
    state->engine=engine_create(&state->runtime_cfg);assert(state->engine);
    state->tempo=tempo_state_default(48000);undo_manager_init(&state->undo);daw_audio_recording_init(&state->audio_recording);
    SDL_strlcpy(state->data_paths.input_root,directory,sizeof(state->data_paths.input_root));
    SDL_strlcpy(state->data_paths.output_root,directory,sizeof(state->data_paths.output_root));
    SDL_strlcpy(state->data_paths.library_copy_root,directory,sizeof(state->data_paths.library_copy_root));
    media_registry_init(&state->media_registry,NULL);assert(daw_media_import_init(state));
    int source_count=state->engine->audio_source_count;
    assert(!engine_has_cached_media(state->engine,"/missing-import-source.wav","missing"));
    assert(state->engine->audio_source_count==source_count); // Read-only admission cannot accumulate orphan sources.
    AudioMediaClip detached={0}; assert(audio_media_clip_load_wav(path,48000,&detached));
    unlink(path); int detached_index=-1;
    assert(engine_add_prepared_clip(state->engine,0,path,"detached",0,&detached,&detached_index));
    assert(!detached.samples && engine_remove_clip(state->engine,0,detached_index)); fixture(path,44100,16384);
    assert(daw_media_import_submit(state,path,"source",0,64,AUDIO_MEDIA_JOB_LIBRARY));
    assert(engine_get_tracks(state->engine)[0].clip_count==0);drain(state);
    assert(daw_media_import_stats(state).inserted==1);
    assert(engine_get_tracks(state->engine)[0].clips[0].media->frame_count==48000);
    assert(undo_manager_undo(&state->undo,state));assert(engine_get_tracks(state->engine)[0].clip_count==0);
    assert(undo_manager_redo(&state->undo,state));assert(engine_get_tracks(state->engine)[0].clip_count==1);
    // Warm admission allocates no result samples; retirement before adoption retries on the worker.
    uint64_t submitted=daw_media_import_stats(state).jobs.submitted;
    assert(daw_media_import_submit(state,path,"source",0,64,AUDIO_MEDIA_JOB_RECORDING));ready(state);
    assert(!daw_media_import_stats(state).jobs.ready_bytes);
    assert(engine_remove_clip(state->engine,0,0));drain(state);
    assert(daw_media_import_stats(state).jobs.submitted==submitted+2);
    assert(engine_get_tracks(state->engine)[0].clip_count==1);
    // A newly decoded source version coexists with already pinned media for existing clips.
    const AudioMediaClip* original=engine_get_tracks(state->engine)[0].clips[0].media;
    fixture(path,44100,-16384);
    assert(daw_media_import_submit(state,path,"source",0,96000,AUDIO_MEDIA_JOB_RECORDING));drain(state);
    const EngineTrack* tracks=engine_get_tracks(state->engine);
    assert(tracks[0].clip_count==2 && tracks[0].clips[1].media!=original);
    assert(original->samples[100]>.49f && tracks[0].clips[1].media->samples[100]<-.49f);
    int target=engine_add_track(state->engine);assert(target==1);
    uint64_t failed=daw_media_import_stats(state).failed;
    assert(daw_media_import_submit(state,path,"source",target,0,AUDIO_MEDIA_JOB_RECORDING));ready(state);
    assert(engine_remove_track(state->engine,target));assert(engine_add_track(state->engine)==target);drain(state);
    assert(daw_media_import_stats(state).failed==failed+1 && engine_get_tracks(state->engine)[target].clip_count==0);
    // Removing an earlier track shifts indices but preserves the pending target identity.
    assert(daw_media_import_submit(state,path,"source",target,0,AUDIO_MEDIA_JOB_RECORDING));ready(state);
    assert(engine_remove_track(state->engine,0));drain(state);assert(engine_get_tracks(state->engine)[0].clip_count==1);
    int count=engine_get_track_count(state->engine);
    assert(daw_media_import_submit(state,path,"source",count,0,AUDIO_MEDIA_JOB_BOUNCE));ready(state);daw_media_import_cancel(state);drain(state);
    assert(engine_get_track_count(state->engine)==count && access(path,F_OK)==0);
    assert(daw_media_import_submit(state,path,"source",count,0,AUDIO_MEDIA_JOB_BOUNCE));drain(state);
    assert(engine_get_track_count(state->engine)==count+1 && !strcmp(engine_get_tracks(state->engine)[count].name,"Bounce"));
    // Failed restore preserves admitted work; successful restore invalidates it before retiring the old engine.
    SessionDocument doc;session_document_init(&doc);assert(session_document_capture(state,&doc));
    assert(daw_media_import_submit(state,path,"source",0,192000,AUDIO_MEDIA_JOB_RECORDING));ready(state);
    uint64_t generation=daw_media_import_stats(state).generation;
    int old_rate=doc.engine.sample_rate; doc.engine.sample_rate=0;assert(!session_apply_document(state,&doc));doc.engine.sample_rate=old_rate;
    assert(daw_media_import_stats(state).generation==generation);
    assert(session_apply_document(state,&doc));assert(daw_media_import_stats(state).generation==generation+1);
    drain(state);assert(daw_media_import_stats(state).stale==1);session_document_free(&doc);
    // Library scan and warm rescan do not decode or resample and preserve completed version metadata.
    library_browser_init(&state->library,directory);library_browser_scan(&state->library,&state->media_registry);
    assert(state->library.count==1 && !state->library.items[0].metadata_requested);
    daw_media_import_poll(state);drain(state);assert(state->library.items[0].metadata_loaded && state->library.items[0].duration_seconds==1);
    library_browser_scan(&state->library,&state->media_registry);assert(state->library.items[0].metadata_loaded);
    // Production-style recording finalization saves the file first and defers insertion.
    AudioDeviceSpec spec={.sample_rate=48000,.channels=1,.block_size=128};
    assert(daw_audio_recording_begin_take(state,0,300000,&spec));
    float samples[128]={0}; assert(daw_audio_recording_enqueue_frames(&state->audio_recording,samples,128,1)==128);
    engine_transport_play(state->engine);daw_audio_recording_drain_if_transport_playing(state);
    DawAudioRecordingResult result={0};assert(daw_audio_recording_finish(state,&result));
    assert(result.saved && result.insertion_pending && !result.inserted && access(result.wav_path,F_OK)==0);
    daw_media_import_cancel(state);drain(state);assert(access(result.wav_path,F_OK)==0);
    daw_media_import_shutdown(state);daw_audio_recording_free(&state->audio_recording);undo_manager_free(&state->undo);
    engine_destroy(state->engine);tempo_map_free(&state->tempo_map);time_signature_map_free(&state->time_signature_map);
    media_registry_shutdown(&state->media_registry);free(state->effects_panel.eq_curve_tracks);free(state->pending_track_fx);
    free(state);unlink(result.wav_path);unlink(path);rmdir(directory);SDL_Quit();
    puts("media_import_test: success");return 0;
}
