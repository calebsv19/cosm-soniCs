#define _POSIX_C_SOURCE 200809L
#include "test_temp_path.h"
#include "app_state.h"
#include "engine/engine_internal.h"
#include "engine/export_stream.h"
#include "core_workers.h"
#include "core_time.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static atomic_int gate_mode;
static atomic_bool reached, released, joining;
static bool reject_submit;
static unsigned prepared, retired;
// Pauses at an observed producer boundary; deadlines diagnose deadlocks, not timing guarantees.
static void barrier(void) {
    atomic_store(&reached, true);
    uint64_t end = core_time_now_ns() + UINT64_C(20000000000);
    while (!atomic_load(&released)) { assert(core_time_now_ns() < end); SDL_Delay(1); }
}
// Carries the real production callbacks through a deterministic publication-boundary probe.
typedef struct Probe { const EngineBounceStreamCallbacks* real; } Probe;
// Stops before or after the real final cancellation/publication compare-exchange.
static bool probe_progress(uint64_t done, uint64_t total, void* user) {
    Probe* p = user;
    int mode = atomic_load(&gate_mode);
    if ((mode == 1 && done == 0) || (mode == 2 && done == total)) barrier();
    bool ok = p->real->progress(done, total, p->real->user);
    if (mode == 3 && done == total) barrier();
    return ok;
}
// Runs actual rendering and WAV publication, optionally holding terminal delivery afterward.
static DawSaveResult probe_run(EngineExportSnapshot* s, const char* path,
                              const EngineBounceStreamCallbacks* cb) {
    Probe p = {cb};
    EngineBounceStreamCallbacks wrapped = {.progress = probe_progress, .user = &p};
    DawSaveResult result = engine_export_run(s, path, &wrapped);
    if (atomic_load(&gate_mode) == 4) barrier();
    return result;
}
// Injects a rejected submission without changing the production queue on successful paths.
static bool probe_submit(CoreWorkers* w, CoreWorkerTaskFn fn, void* context) {
    return !reject_submit && core_workers_submit(w, fn, context);
}
// Counts captured source plans using the real engine/cache owner.
static EngineExportSnapshot* probe_prepare(Engine* e, uint64_t a, uint64_t b,
                const EngineBounceOptions* options, EngineBounceWavFormat format, bool no_replace) {
    EngineExportSnapshot* p = engine_export_prepare(e,a,b,options,format,no_replace);
    if (p) ++prepared;
    return p;
}
// Verifies control-side retirement occurs only after the worker releases its captured plan.
static void probe_free(EngineExportSnapshot* p) {
    if (p) ++retired;
    engine_export_free(p);
}
// Exposes shutdown entry to a release-only helper without moving app ownership off control.
static void probe_shutdown(CoreWorkers* w, CoreWorkersShutdownMode mode) {
    atomic_store(&joining,true);
    core_workers_shutdown_with_mode(w,mode);
}
#define engine_export_run probe_run
#define engine_export_prepare probe_prepare
#define engine_export_free probe_free
#define core_workers_submit probe_submit
#define core_workers_shutdown_with_mode probe_shutdown
#include "../src/app/song_export.c"
#undef engine_export_run
#undef engine_export_prepare
#undef engine_export_free
#undef core_workers_submit
#undef core_workers_shutdown_with_mode

// Arms one worker boundary before admission.
static void arm(int mode) {
    atomic_store(&gate_mode,mode); atomic_store(&reached,false);
    atomic_store(&released,false); atomic_store(&joining,false);
}
// Waits for actual worker entry without borrowing its mutable terminal record.
static void entered(void) {
    uint64_t end = core_time_now_ns() + UINT64_C(20000000000);
    while (!atomic_load(&reached)) { assert(core_time_now_ns() < end); SDL_Delay(1); }
}
// Admits against the actual current immutable mix guard.
static DawSongExportInfo start(AppState* s) {
    DawMixSnapshot* g=NULL; assert(daw_action_get_mix_snapshot(s,&g)==DAW_ACTION_UNCHANGED);
    DawSongExportInfo out;
    assert(daw_song_export_start(s,g,ENGINE_BOUNCE_WAV_FLOAT32,0,false,&out)==DAW_ACTION_APPLIED);
    daw_mix_snapshot_free(g); return out;
}
// Reads terminal metadata through the real completion fence and control retirement path.
static DawSongExportInfo finish(AppState* s, const char* id) {
    uint64_t end = core_time_now_ns() + UINT64_C(20000000000);
    DawSongExportInfo out;
    for (;;) {
        assert(daw_song_export_get(s,id,&out));
        if (!strcmp(out.state,"completed") || !strcmp(out.state,"cancelled") || !strcmp(out.state,"failed")) return out;
        assert(core_time_now_ns()<end); SDL_Delay(1);
    }
}
// Releases a held worker only once real owner shutdown has begun joining it.
static void* release_at_join(void* unused) {
    (void)unused;
    uint64_t end=core_time_now_ns()+UINT64_C(20000000000);
    while (!atomic_load(&joining)) { assert(core_time_now_ns()<end); SDL_Delay(1); }
    atomic_store(&released,true); return NULL;
}
// Checks immutable delivered audio through an independent decoder after owner retirement.
static void verify_file(const DawSongExportInfo* info) {
    AudioMediaClip clip={0}; assert(audio_media_clip_load_wav(info->path,info->rate,&clip));
    assert(clip.frame_count==2048 && clip.samples[100]==.25f && clip.samples[101]==-.125f);
    audio_media_clip_free(&clip);
}
// Covers bounded result retention, failed admission, cancellation races and real join ownership.
int main(void) {
    SDL_setenv("SDL_AUDIODRIVER","dummy",1); assert(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER)==0);
    AppState* s=calloc(1,sizeof(*s));assert(s); config_set_defaults(&s->runtime_cfg);
    s->runtime_cfg.block_size=128;s->runtime_cfg.default_fade_in_ms=s->runtime_cfg.default_fade_out_ms=0;
    s->tempo=tempo_state_default(s->runtime_cfg.sample_rate);
    tempo_map_init(&s->tempo_map,s->runtime_cfg.sample_rate);time_signature_map_init(&s->time_signature_map);
    undo_manager_init(&s->undo);s->engine=engine_create(&s->runtime_cfg);assert(s->engine);
    DAW_TEST_TEMP_PATH(root,"song-lifetime-XXXXXX");assert(mkdtemp(root));strcpy(s->data_paths.output_root,root);
    char input[512];snprintf(input,sizeof(input),"%s/input.wav",root);
    float pcm[4096];for(int i=0;i<2048;++i){pcm[2*i]=.25f;pcm[2*i+1]=-.125f;}
    EngineBounceBuffer b={pcm,2048,2,s->runtime_cfg.sample_rate};assert(engine_bounce_write_wav(&b,input,ENGINE_BOUNCE_WAV_FLOAT32));
    assert(engine_add_clip_to_track(s->engine,0,input,0,NULL));
    DawSongExportInfo held[8];
    for(int i=0;i<8;++i){arm(0);DawSongExportInfo job=start(s);held[i]=finish(s,job.id);assert(held[i].verified);}
    assert(prepared==retired);
    // Failed ninth admission must not evict the oldest of eight accepted terminal records.
    DawMixSnapshot* g=NULL;assert(daw_action_get_mix_snapshot(s,&g)==DAW_ACTION_UNCHANGED);
    DawSongExportInfo untouched={0};strcpy(untouched.id,"unchanged");reject_submit=true;
    assert(daw_song_export_start(s,g,ENGINE_BOUNCE_WAV_FLOAT32,0,false,&untouched)==DAW_ACTION_ENGINE_REJECTED);
    reject_submit=false;daw_mix_snapshot_free(g);assert(!strcmp(untouched.id,"unchanged"));
    assert(prepared==retired);
    for(int i=0;i<8;++i){DawSongExportInfo copy;assert(daw_song_export_get(s,held[i].id,&copy));assert(!memcmp(&copy,&held[i],sizeof(copy)));}
    DawSongExportInfo latest;assert(daw_song_export_get(s,NULL,&latest));assert(!strcmp(latest.id,held[7].id));
    // Exercise both sides of the last-progress publication boundary.
    for(int mode=1;mode<=4;++mode){
        arm(mode);DawSongExportInfo job=start(s);entered();
        if(mode==1){DawSongExportInfo expired;assert(!daw_song_export_get(s,held[0].id,&expired));}
        DawSongExportInfo pending;assert(daw_song_export_get(s,job.id,&pending));
        assert(!pending.published && prepared==retired+1);
        const char* reply=daw_song_export_cancel(s,job.id);
        assert(!strcmp(reply,mode<=2?"cancel_requested":"publication_started"));
        atomic_store(&released,true);DawSongExportInfo done=finish(s,job.id);
        assert(prepared==retired);
        if(mode<=2){assert(!strcmp(done.state,"cancelled") && !done.published && access(done.path,F_OK));}
        else {assert(done.verified && done.published);verify_file(&done);}
    }
    // Shutdown cannot free a captured plan until rendering/publication and terminal delivery finish.
    for(int mode=1;mode<=4;++mode){
        arm(mode);DawSongExportInfo job=start(s);entered();assert(prepared==retired+1);
        pthread_t helper;assert(!pthread_create(&helper,NULL,release_at_join,NULL));
        daw_song_export_shutdown(s);assert(!pthread_join(helper,NULL));assert(!s->exports && prepared==retired);
        if(mode<=2)assert(access(job.path,F_OK));else verify_file(&job);
        for(int i=0;i<8;++i)verify_file(&held[i]);
    }
    // Rejection on a fresh owner cannot fabricate latest or addressed terminal readback.
    g=NULL;assert(daw_action_get_mix_snapshot(s,&g)==DAW_ACTION_UNCHANGED);reject_submit=true;
    assert(daw_song_export_start(s,g,ENGINE_BOUNCE_WAV_FLOAT32,0,false,&untouched)==DAW_ACTION_ENGINE_REJECTED);
    reject_submit=false;daw_mix_snapshot_free(g);assert(!daw_song_export_get(s,NULL,&latest));
    assert(!s->exports->slots[0].info.id[0] && !atomic_load(&s->exports->slots[0].phase));
    daw_song_export_shutdown(s);assert(prepared==retired);
    engine_destroy(s->engine);undo_manager_free(&s->undo);tempo_map_free(&s->tempo_map);time_signature_map_free(&s->time_signature_map);free(s);SDL_Quit();
    puts("song_export_lifetime_test: passed (eight retained outcomes, submit rollback, cancel/publication fences, joined pins, copied results)");return 0;
}
