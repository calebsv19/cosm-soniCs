#include "app_state.h"
#include "app/media_import.h"
#include "audio/wav_writer.h"
#include "core_time.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>

// Reads resident memory for this process without attributing allocator retention to live media ownership.
static uint64_t rss(void) {
    mach_task_basic_info_data_t info; mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(),MACH_TASK_BASIC_INFO,(task_info_t)&info,&count)==KERN_SUCCESS ? info.resident_size : 0;
}
// Reads monotonic elapsed milliseconds for main-thread admission and completion timing.
static double now_ms(void) { return core_time_now_ns()/1e6; }
// Builds distinct 30-second stereo files before measurement begins.
static void fixture(const char* path, int rate, int tone) {
    uint64_t frames=(uint64_t)rate*30; float* audio=malloc(frames*2*sizeof(float));assert(audio);
    for(uint64_t n=0;n<frames;++n) audio[n*2]=audio[n*2+1]=.01f*sinf((float)(6.283185307179586*(220+tone*80)*n/rate));
    assert(wav_write_f32(path,audio,frames,2,rate));free(audio);
}
// Waits for worker retirement and regular control collection before assessing settled cache ownership.
static void settle(AppState* state, uint64_t baseline) {
    double start=now_ms();
    while(engine_media_resident_bytes(state->engine)!=baseline) {
        daw_media_import_poll(state);daw_audio_recording_drain_if_transport_playing(state);
        assert(now_ms()-start<2000);SDL_Delay(4);
    }
}
// Runs four imports, retaining metrics for scheduling, publication, residency and captured-input continuity.
static void batch(AppState* state, char paths[4][512], const char* phase, int rate, uint64_t baseline) {
    double start=now_ms(), submit_max=0,poll_max=0;uint64_t peak=rss();size_t ready_peak=0,reserved_peak=0;
    uint64_t inserted=daw_media_import_stats(state).inserted;
    for(int i=0;i<4;++i) {
        double began=now_ms();assert(daw_media_import_submit(state,paths[i],NULL,i,48000*60,AUDIO_MEDIA_JOB_RECORDING));
        double ms=now_ms()-began;if(ms>submit_max)submit_max=ms;
    }
    // Leave ready results unconsumed for a representative main-thread stall without dropping input.
    SDL_Delay(200);
    while(daw_media_import_stats(state).jobs.admitted) {
        DawMediaImportStats stats=daw_media_import_stats(state);
        if(stats.jobs.ready_bytes>ready_peak)ready_peak=stats.jobs.ready_bytes;
        if(stats.jobs.reserved_bytes>reserved_peak)reserved_peak=stats.jobs.reserved_bytes;
        double began=now_ms();daw_media_import_poll(state);daw_audio_recording_drain_if_transport_playing(state);
        double ms=now_ms()-began;if(ms>poll_max)poll_max=ms;
        uint64_t memory=rss();if(memory>peak)peak=memory;
        assert(now_ms()-start<30000);SDL_Delay(4);
    }
    if (!strcmp(phase,"warm")) assert(ready_peak==0);
    DawMediaImportStats final=daw_media_import_stats(state);assert(final.inserted==inserted+4 && !final.failed);
    assert(!final.jobs.ready_bytes && !final.jobs.reserved_bytes);
    assert(final.pinned_bytes==baseline+4*30*48000*2*sizeof(float));
    printf("{\"phase\":\"%s\",\"source_rate\":%d,\"elapsed_ms\":%.6f,\"submit_max_ms\":%.6f,\"poll_max_ms\":%.6f,\"rss_peak\":%llu,\"ready_peak_bytes\":%zu,\"reserved_peak_bytes\":%zu,\"pinned_bytes\":%zu}\n",phase,rate,now_ms()-start,submit_max,poll_max,(unsigned long long)peak,ready_peak,reserved_peak,final.pinned_bytes);
}
// Measures distinct assets and resident reuse during real dummy capture/output and eight FX tracks.
int main(int argc,char** argv) {
    int rate=argc>1?atoi(argv[1]):44100;assert(rate==44100 || rate==48000);
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);assert(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER)==0);
    char root[]="/tmp/daw_media_bench_XXXXXX";assert(mkdtemp(root));char paths[4][512],bed[512];
    for(int i=0;i<4;++i){snprintf(paths[i],512,"%s/import%d.wav",root,i);fixture(paths[i],rate,i);}
    snprintf(bed,sizeof(bed),"%s/bed.wav",root);fixture(bed,48000,8);
    AppState* state=calloc(1,sizeof(*state));assert(state);config_set_defaults(&state->runtime_cfg);
    state->runtime_cfg.sample_rate=48000;state->runtime_cfg.block_size=128;
    const char* queue=getenv("DAW_BENCH_QUEUE_BLOCKS");if(queue)state->runtime_cfg.output_queue_blocks=atoi(queue);
    state->engine=engine_create(&state->runtime_cfg);assert(state->engine);
    for(int t=0;t<8;++t) {
        if(t)assert(engine_add_track(state->engine)==t);
        assert(engine_add_clip_to_track(state->engine,t,bed,0,NULL));assert(engine_track_set_gain(state->engine,t,.1f));
        assert(engine_fx_track_add(state->engine,t,1));assert(engine_fx_track_add(state->engine,t,20));
        assert(engine_fx_track_add(state->engine,t,50));assert(engine_fx_track_add(state->engine,t,21));
    }
    SDL_strlcpy(state->data_paths.input_root,root,sizeof(state->data_paths.input_root));
    SDL_strlcpy(state->data_paths.output_root,root,sizeof(state->data_paths.output_root));
    media_registry_init(&state->media_registry,NULL);undo_manager_init(&state->undo);daw_audio_recording_init(&state->audio_recording);
    assert(daw_media_import_init(state));assert(engine_start(state->engine)&&engine_transport_play(state->engine));SDL_Delay(600);
    state->selected_track_index=0;state->active_track_index=0;assert(daw_audio_recording_begin_timeline_capture(state));
    EngineDiagnostics before,after;assert(engine_get_diagnostics(state->engine,&before));uint64_t baseline=engine_media_resident_bytes(state->engine);
    uint64_t initial_rss=rss();
    library_browser_init(&state->library,root);
    double scan_start=now_ms();library_browser_scan(&state->library,&state->media_registry);double cold_scan=now_ms()-scan_start;
    daw_media_import_poll(state);
    while(daw_media_import_stats(state).jobs.admitted) {daw_media_import_poll(state);SDL_Delay(1);}
    for(int i=0;i<state->library.count;++i)assert(state->library.items[i].metadata_loaded);
    scan_start=now_ms();library_browser_scan(&state->library,&state->media_registry);double warm_scan=now_ms()-scan_start;
    printf("{\"phase\":\"scan\",\"source_rate\":%d,\"files\":%d,\"cold_main_ms\":%.6f,\"warm_main_ms\":%.6f}\n",rate,state->library.count,cold_scan,warm_scan);
    batch(state,paths,"cold",rate,baseline);
    // Existing media stays pinned while warm requests verify and reuse it with zero decoded ready bytes.
    uint64_t held=engine_media_resident_bytes(state->engine);batch(state,paths,"warm",rate,held-4*30*48000*2*sizeof(float));
    for(int cycle=0;cycle<3;++cycle) {
        for(int t=0;t<4;++t)while(engine_get_tracks(state->engine)[t].clip_count>1)assert(engine_remove_clip(state->engine,t,1));
        settle(state,baseline);
        batch(state,paths,"reload",rate,baseline);
    }
    for(int t=0;t<4;++t)while(engine_get_tracks(state->engine)[t].clip_count>1)assert(engine_remove_clip(state->engine,t,1));
    settle(state,baseline);
    daw_audio_recording_drain_if_transport_playing(state);
    audio_capture_device_stop(&state->audio_recording.capture_device);state->audio_recording.capture_device_started=false;
    assert(engine_get_diagnostics(state->engine,&after));
    uint64_t missing=atomic_load(&state->audio_recording.dropped_frames);
    printf("{\"phase\":\"retired\",\"source_rate\":%d,\"rss_before\":%llu,\"rss_after\":%llu,\"pinned_baseline_bytes\":%llu,\"pinned_after_bytes\":%zu,\"capture_missing_frames\":%llu,\"output_missing_frames\":%llu,\"worker_over_budget\":%llu}\n",rate,(unsigned long long)initial_rss,(unsigned long long)rss(),(unsigned long long)baseline,engine_media_resident_bytes(state->engine),(unsigned long long)missing,(unsigned long long)(after.underrun_frames-before.underrun_frames),(unsigned long long)(after.worker_over_budget-before.worker_over_budget));
    assert(missing==0 && after.underrun_frames==before.underrun_frames);
    daw_audio_recording_cancel(&state->audio_recording);daw_media_import_shutdown(state);
    daw_audio_recording_free(&state->audio_recording);undo_manager_free(&state->undo);media_registry_shutdown(&state->media_registry);
    engine_destroy(state->engine);free(state);for(int i=0;i<4;++i)unlink(paths[i]);unlink(bed);rmdir(root);SDL_Quit();return 0;
}
