#include "test_temp_path.h"
#include "app_state.h"
#include "app/media_import.h"
#include "engine/engine_internal.h"
#include "core_time.h"
#include "session.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static atomic_bool gate, saw_cancel, timed_out;
static atomic_uint entries;

// Stops the real worker at decode until its owner requests cancellation.
static bool gated_load(const char* path, int rate, AudioMediaClip* out,
                       const AudioMediaLoadControl* control) {
    if (atomic_load(&gate)) {
        atomic_fetch_add(&entries, 1);
        uint64_t deadline=core_time_now_ns()+UINT64_C(20000000000);
        while (!control->cancelled(control->user) && core_time_now_ns()<deadline) SDL_Delay(1);
        if (control->cancelled(control->user)) atomic_store(&saw_cancel,true);
        else atomic_store(&timed_out,true);
    }
    return audio_media_clip_load_controlled(path,rate,out,control);
}
/* Only this fixture substitutes the decoder boundary; all ownership code is real. */
#define audio_media_clip_load_controlled gated_load
#include "../src/audio/media_jobs.c"
#undef audio_media_clip_load_controlled

// Writes signed mono input for actual decode/resample and source-identity checks.
static void fixture(const char* path, short value) {
    unsigned char h[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
        0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',0,0,0,0};
    unsigned frames=4410,size=frames*2;
    for(int i=0;i<4;i++){h[4+i]=(size+36)>>(8*i);h[40+i]=size>>(8*i);}
    FILE* f=fopen(path,"wb");assert(f);assert(fwrite(h,1,44,f)==44);
    for(unsigned i=0;i<frames;i++){assert(fputc(value&255,f)!=EOF);assert(fputc((value>>8)&255,f)!=EOF);}
    assert(!fclose(f));
}
// Waits for a release-published slot without transferring its result.
static void ready_jobs(AudioMediaJobs* jobs, size_t count) {
    uint64_t deadline=core_time_now_ns()+UINT64_C(20000000000);
    while(audio_media_jobs_stats(jobs).ready!=count){assert(core_time_now_ns()<deadline);SDL_Delay(1);}
}
// Takes exactly one published result through the real transfer boundary.
static AudioMediaResult take(AudioMediaJobs* jobs) {
    AudioMediaResult result={0};uint64_t deadline=core_time_now_ns()+UINT64_C(20000000000);
    while(!audio_media_jobs_poll(jobs,&result)){assert(core_time_now_ns()<deadline);SDL_Delay(1);}
    return result;
}
// Arms the barrier before admitting a running decoder and queued successors.
static void arm(void) {
    atomic_store(&entries,0);atomic_store(&saw_cancel,false);atomic_store(&timed_out,false);atomic_store(&gate,true);
}
// Observes entry rather than sleeping to guess that a job is running.
static void entered(void) {
    uint64_t deadline=core_time_now_ns()+UINT64_C(20000000000);
    while(!atomic_load(&entries)){assert(core_time_now_ns()<deadline);SDL_Delay(1);}
}
// Fills all producer slots with one ready, one decoder-held and two queued jobs.
static void fill(AudioMediaJobs* jobs, const AudioMediaRequest* request, uint64_t ids[4]) {
    atomic_store(&gate,false); ids[0]=audio_media_jobs_submit(jobs,request);assert(ids[0]);ready_jobs(jobs,1);
    arm();for(int i=1;i<4;i++){ids[i]=audio_media_jobs_submit(jobs,request);assert(ids[i]);}
    entered(); AudioMediaJobStats s=audio_media_jobs_stats(jobs);
    assert(s.admitted==4 && s.ready==1 && s.running==1 && s.queued==2);
    assert(!audio_media_jobs_submit(jobs,request));
}
// Verifies transferred PCM is independent of slot reuse, cancellation and producer lifetime.
static AudioMediaResult held_result(const char* path) {
    fixture(path,16384);AudioMediaJobs* jobs=audio_media_jobs_create(1024*1024);assert(jobs);
    AudioMediaRequest request={.generation=20,.sample_rate=48000,.kind=AUDIO_MEDIA_JOB_LIBRARY};strcpy(request.path,path);
    assert(audio_media_jobs_submit(jobs,&request));AudioMediaResult held=take(jobs);
    assert(held.ok && held.clip.frame_count==4800 && held.clip.samples[100]>.49f);
    size_t bytes=held.clip.frame_count*held.clip.channels*sizeof(float);
    float* saved=malloc(bytes);assert(saved);memcpy(saved,held.clip.samples,bytes);
    assert(!audio_media_jobs_stats(jobs).admitted); /* Transfer ends producer ownership. */
    fixture(path,-16384);request.generation=21;
    uint64_t ids[4];fill(jobs,&request,ids);audio_media_jobs_cancel(jobs,0);
    unsigned seen=0;
    for(int i=0;i<4;i++) {
        AudioMediaResult r=take(jobs);assert(!r.ok && r.cancelled && !r.clip.samples);
        for(int j=0;j<4;j++) if(r.id==ids[j]){assert(!(seen&(1u<<j)));seen|=1u<<j;}
        audio_media_clip_free(&r.clip);
    }
    assert(seen==15 && atomic_load(&saw_cancel) && !atomic_load(&timed_out) && atomic_load(&entries)==1);
    assert(held.ok && !held.cancelled && held.request.generation==20 && !memcmp(saved,held.clip.samples,bytes));
    fill(jobs,&request,ids);audio_media_jobs_destroy(jobs);
    assert(atomic_load(&saw_cancel) && !atomic_load(&timed_out) && atomic_load(&entries)==1);
    assert(!memcmp(saved,held.clip.samples,bytes));
    atomic_store(&gate,false);jobs=audio_media_jobs_create(1024*1024);assert(jobs);request.generation=22;
    for(int i=0;i<4;i++)assert(audio_media_jobs_submit(jobs,&request));
    assert(!audio_media_jobs_submit(jobs,&request));ready_jobs(jobs,4);
    for(int i=0;i<4;i++){AudioMediaResult r=take(jobs);assert(r.ok && r.clip.samples[100]<-.49f);audio_media_clip_free(&r.clip);}
    audio_media_jobs_destroy(jobs);assert(!memcmp(saved,held.clip.samples,bytes));
    free(saved);return held;
}
// Drains current app results through the real acceptance and history owner.
static void drain_app(AppState* state) {
    uint64_t deadline=core_time_now_ns()+UINT64_C(20000000000);
    while(daw_media_import_stats(state).jobs.admitted){daw_media_import_poll(state);assert(core_time_now_ns()<deadline);SDL_Delay(1);}
}
// Waits for app-owned completion without allowing publication into the engine.
static void ready_app(AppState* state) {
    uint64_t deadline=core_time_now_ns()+UINT64_C(20000000000);
    while(daw_media_import_stats(state).jobs.ready!=1){assert(core_time_now_ns()<deadline);SDL_Delay(1);}
}
// Exercises successful/failed project replacement with all four import slots occupied.
static void project_replacement(const char* path, const char* directory) {
    fixture(path,16384); AppState* state=calloc(1,sizeof(*state));assert(state);config_set_defaults(&state->runtime_cfg);
    state->runtime_cfg.sample_rate=48000;state->runtime_cfg.block_size=128;
    state->engine=engine_create(&state->runtime_cfg);assert(state->engine);state->tempo=tempo_state_default(48000);
    undo_manager_init(&state->undo);daw_audio_recording_init(&state->audio_recording);
    SDL_strlcpy(state->data_paths.input_root,directory,sizeof(state->data_paths.input_root));
    SDL_strlcpy(state->data_paths.output_root,directory,sizeof(state->data_paths.output_root));
    SDL_strlcpy(state->data_paths.library_copy_root,directory,sizeof(state->data_paths.library_copy_root));
    media_registry_init(&state->media_registry,NULL);assert(daw_media_import_init(state));
    assert(daw_media_import_submit(state,path,NULL,0,0,AUDIO_MEDIA_JOB_RECORDING));ready_app(state);
    arm(); for(int i=0;i<3;i++)assert(daw_media_import_submit(state,path,NULL,0,4800*(i+1),AUDIO_MEDIA_JOB_RECORDING));
    entered();DawMediaImportStats before=daw_media_import_stats(state);
    assert(before.jobs.ready==1 && before.jobs.running==1 && before.jobs.queued==2);
    assert(!daw_media_import_submit(state,path,NULL,0,24000,AUDIO_MEDIA_JOB_RECORDING));
    SessionDocument doc;session_document_init(&doc);assert(session_document_capture(state,&doc));
    int rate=doc.engine.sample_rate;doc.engine.sample_rate=0;assert(!session_apply_document(state,&doc));doc.engine.sample_rate=rate;
    assert(daw_media_import_stats(state).generation==before.generation && !atomic_load(&saw_cancel));
    assert(session_apply_document(state,&doc));assert(daw_media_import_stats(state).generation==before.generation+1);
    int registry_count=state->media_registry.count, undo_count=state->undo.undo_count, redo_count=state->undo.redo_count;
    char status[sizeof(state->library.status_line)];memcpy(status,state->library.status_line,sizeof(status));
    uint64_t deadline=core_time_now_ns()+UINT64_C(20000000000);
    while(daw_media_import_stats(state).stale<before.stale+4) {
        daw_media_import_poll(state);assert(core_time_now_ns()<deadline);SDL_Delay(1);
    }
    DawMediaImportStats after=daw_media_import_stats(state);
    assert(after.stale==before.stale+4 && !after.inserted && !after.failed && !after.cancelled);
    assert(state->media_registry.count==registry_count && state->undo.undo_count==undo_count && state->undo.redo_count==redo_count);
    assert(!memcmp(status,state->library.status_line,sizeof(status)) && !engine_get_tracks(state->engine)[0].clip_count);
    assert(atomic_load(&saw_cancel) && !atomic_load(&timed_out));atomic_store(&gate,false);
    /* The last stale retirement may admit a new-generation library probe.
     * Observe the old prefix first, then permit legitimate current metadata. */
    drain_app(state);registry_count=state->media_registry.count;
    assert(daw_media_import_submit(state,path,NULL,0,0,AUDIO_MEDIA_JOB_RECORDING));ready_app(state);
    daw_media_import_cancel(state);drain_app(state);assert(daw_media_import_stats(state).cancelled==1);
    assert(!engine_get_tracks(state->engine)[0].clip_count && state->media_registry.count==registry_count);
    assert(daw_media_import_submit(state,path,NULL,0,0,AUDIO_MEDIA_JOB_RECORDING));drain_app(state);
    assert(daw_media_import_stats(state).inserted==1 && engine_get_tracks(state->engine)[0].clip_count==1);
    assert(undo_manager_undo(&state->undo,state));assert(!engine_get_tracks(state->engine)[0].clip_count);
    assert(undo_manager_redo(&state->undo,state));assert(engine_get_tracks(state->engine)[0].clip_count==1);
    session_document_free(&doc);daw_media_import_shutdown(state);daw_audio_recording_free(&state->audio_recording);
    undo_manager_free(&state->undo);engine_destroy(state->engine);tempo_map_free(&state->tempo_map);time_signature_map_free(&state->time_signature_map);
    media_registry_shutdown(&state->media_registry);free(state->effects_panel.eq_curve_tracks);free(state->pending_track_fx);free(state);
}
// Runs real media and owner transitions under the dummy audio route without user assets.
int main(void) {
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);assert(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER)==0);
    DAW_TEST_TEMP_PATH(directory,"daw_media_lifetime_XXXXXX");assert(mkdtemp(directory));
    char path[512];snprintf(path,sizeof(path),"%s/source.wav",directory);
    AudioMediaResult held=held_result(path);
    size_t bytes=held.clip.frame_count*held.clip.channels*sizeof(float);
    float* saved=malloc(bytes);assert(saved);memcpy(saved,held.clip.samples,bytes);
    project_replacement(path,directory);
    assert(held.ok && held.request.generation==20 && !memcmp(saved,held.clip.samples,bytes));
    audio_media_clip_free(&held.clip);assert(!held.clip.samples);free(saved);
    assert(!unlink(path));assert(!rmdir(directory));SDL_Quit();
    puts("media_jobs_lifetime_test: passed (held PCM, four-slot cancel/join/reuse, real project replacement and current-owner history)");return 0;
}
