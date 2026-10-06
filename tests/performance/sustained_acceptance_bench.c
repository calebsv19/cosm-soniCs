// Reuses the measured production fixtures without duplicating their audio/FX construction policy.
#define main daw_runtime_bench_reference_entry
#include "runtime_workload_bench.c"
#undef main
#include "app/media_import.h"
#include "core/loop/daw_mainthread_messages.h"
#include "core/loop/daw_mainthread_wake.h"
#include <mach/mach_time.h>
#include <malloc/malloc.h>

// Owns a test-only wall-paced consumer after the SDL dummy endpoint has been paused.
typedef struct PacedConsumer {
    Engine* engine;
    atomic_bool running;
    uint64_t late_periods, max_lateness_ns, callbacks;
    double checksum;
} PacedConsumer;

// Consumes the real callback at nominal cadence; late wakeups are recorded without catch-up bursts.
static int paced_consume(void* user) {
    PacedConsumer* consumer = user;
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    int block = consumer->engine->config.block_size, rate = consumer->engine->config.sample_rate;
    double period = (double)block * 1e9 / rate;
    uint64_t origin = mach_absolute_time(), sequence = 1;
    float output[2048];
    while (atomic_load(&consumer->running)) {
        uint64_t deadline = origin + (uint64_t)(sequence * period * timebase.denom / timebase.numer);
        mach_wait_until(deadline);
        uint64_t now = mach_absolute_time();
        uint64_t late = (now > deadline ? now - deadline : 0) * timebase.numer / timebase.denom;
        if (late > consumer->max_lateness_ns) consumer->max_lateness_ns = late;
        if (late > period) { ++consumer->late_periods; origin = now; sequence = 0; }
        engine_audio_callback(output, block, 2, consumer->engine);
        for (int i = 0; i < block * 2; ++i) { assert(isfinite(output[i])); consumer->checksum += fabs(output[i]); }
        ++consumer->callbacks;
        ++sequence;
    }
    return 0;
}

// Polls the production control-side import and recording owners during mixed activity.
static void service(AppState* app) {
    if (getenv("DAW_BENCH_UI")) {
        DawMainThreadMessage messages[64];
        (void)daw_mainthread_message_queue_drain(messages, 64);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
    }
    daw_media_import_poll(app);
    daw_audio_recording_drain_if_transport_playing(app);
}

// Keeps capture draining during a synchronous streamed export without editing its captured project.
static bool export_progress(uint64_t done, uint64_t total, void* user) {
    (void)done; (void)total;
    daw_audio_recording_drain_if_transport_playing(user);
    return true;
}

// Accumulates exported signal energy on the control thread to reject silent/nonfinite fixtures.
static double export_energy;
static void observe_export(const float* samples, uint64_t first, uint32_t frames, int channels, void* user) {
    (void)first; (void)user;
    for (size_t i=0;i<(size_t)frames*channels;++i) { assert(isfinite(samples[i])); export_energy+=fabs(samples[i]); }
}

// Hashes the bounded export artifact to prove cancellation preserves its actual bytes.
static uint64_t file_digest(const char* path) {
    FILE* file=fopen(path,"rb");assert(file);uint64_t hash=1469598103934665603ULL;
    unsigned char bytes[4096];size_t n;
    while((n=fread(bytes,1,sizeof(bytes),file))) for(size_t i=0;i<n;++i) {hash^=bytes[i];hash*=1099511628211ULL;}
    assert(!ferror(file));fclose(file);return hash;
}

// Cancels after actual export progress to exercise cleanup while preserving the previous destination.
static bool cancel_export(uint64_t done, uint64_t total, void* user) {
    export_progress(done, total, user);
    return done == 0;
}

// Samples all malloc zones only in the separately identified heap-attribution workload.
static void heap_observation(const char* phase, double elapsed) {
    if (!getenv("DAW_BENCH_HEAP")) return;
    malloc_statistics_t stats={0};malloc_zone_statistics(NULL,&stats);
    printf("{\"type\":\"heap\",\"phase\":\"%s\",\"elapsed_s\":%.3f,\"in_use_bytes\":%zu,"
           "\"reserved_bytes\":%zu,\"blocks\":%u,\"rss_bytes\":%llu}\n",phase,elapsed,
           stats.size_in_use,stats.size_allocated,stats.blocks_in_use,(unsigned long long)resident_bytes());
}

// Reports one observation without printing or allocating from audio/analysis worker threads.
static void observation(AppState* app, const EngineDiagnostics* baseline, double elapsed) {
    EngineDiagnostics d; EngineAnalysisDiagnostics spectrum, gram;
    assert(engine_get_diagnostics(app->engine, &d));
    assert(engine_get_analysis_diagnostics(app->engine, false, &spectrum));
    assert(engine_get_analysis_diagnostics(app->engine, true, &gram));
    DawMediaImportStats jobs = daw_media_import_stats(app);
    printf("{\"type\":\"sample\",\"elapsed_s\":%.3f,\"rss_bytes\":%llu,\"pinned_bytes\":%zu,"
           "\"queued_frames\":%llu,\"missing_output\":%llu,\"worker_over_budget\":%llu,"
           "\"spectrum_pending\":%llu,\"spectrogram_pending\":%llu,\"job_reserved_bytes\":%zu,"
           "\"capture_missing\":%llu}\n", elapsed, (unsigned long long)resident_bytes(),
           engine_media_resident_bytes(app->engine), (unsigned long long)d.queued_frames,
           (unsigned long long)(d.underrun_frames-baseline->underrun_frames),
           (unsigned long long)(d.worker_over_budget-baseline->worker_over_budget),
           (unsigned long long)spectrum.pending, (unsigned long long)gram.pending, jobs.jobs.reserved_bytes,
           (unsigned long long)atomic_load(&app->audio_recording.dropped_frames));
    heap_observation("steady",elapsed);
    fflush(stdout);
}

#ifndef DAW_SUSTAINED_ENTRY
#define DAW_SUSTAINED_ENTRY main
#endif

// Exercises sustained playback or mixed control workflows and preserves failed timing observations.
int DAW_SUSTAINED_ENTRY(int argc, char** argv) {
    assert(argc == 8);
    const char* mode = argv[1]; int rate = atoi(argv[2]), block = atoi(argv[3]);
    int tracks = atoi(argv[4]), seconds = atoi(argv[5]), analysis = atoi(argv[6]), queue = atoi(argv[7]);
    bool mixed = !strcmp(mode, "mixed"), paced = !strcmp(mode, "paced");
    assert(mixed || paced || !strcmp(mode, "dummy"));
    assert((rate==48000 || rate==96000) && (block==128 || block==512));
    assert(tracks>=1 && tracks<=32 && seconds>=5 && seconds<=600 && (queue==4 || queue==32));
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER)==0);
    if (getenv("DAW_BENCH_UI")) {
        assert(daw_mainthread_wake_init());
        daw_mainthread_message_queue_init();
    }
    heap_observation("before_fixture",0);
    char queue_text[16]; snprintf(queue_text,sizeof(queue_text),"%d",queue);
    SDL_setenv("DAW_BENCH_QUEUE_BLOCKS",queue_text,1);
    char root[]="/tmp/daw-s49-XXXXXX", path[512], imported[512], exported[512];
    assert(mkdtemp(root));
    fprintf(stderr,"sustained_root=%s\n",root);
    snprintf(path,sizeof(path),"%s/bed.wav",root); fixture(path,rate,8);
    snprintf(imported,sizeof(imported),"%s/import.wav",root); fixture(imported,44100,6);
    snprintf(exported,sizeof(exported),"%s/export.wav",root);
    Workload workload={analysis ? "live_analysis" : "live",tracks,1,0,1,block,rate,1,8};
    FxInstId scope=0;
    AppState* app=calloc(1,sizeof(*app)); assert(app);
    app->engine=project(&workload,path,&scope); app->runtime_cfg=*engine_get_config(app->engine);
    app->tempo=tempo_state_default(rate); app->timeline_visible_seconds=8; app->timeline_vertical_scale=1;
    SDL_strlcpy(app->data_paths.input_root,root,sizeof(app->data_paths.input_root));
    SDL_strlcpy(app->data_paths.output_root,root,sizeof(app->data_paths.output_root));
    SDL_strlcpy(app->data_paths.library_copy_root,root,sizeof(app->data_paths.library_copy_root));
    media_registry_init(&app->media_registry,NULL); undo_manager_init(&app->undo);
    daw_audio_recording_init(&app->audio_recording); assert(daw_media_import_init(app));
    int original_clips = mixed ? (seconds * 2 + 15) / 8 : 1;
    if(mixed) {
        for(int t=0;t<tracks;++t) for(int c=1;c<original_clips;++c)
            assert(engine_add_clip_to_track(app->engine,t,path,(uint64_t)c*8*rate,NULL));
        int midi=engine_add_track(app->engine); assert(midi==tracks);
        assert(engine_add_midi_clip_to_track(app->engine,midi,0,(uint64_t)rate*seconds*2,NULL));
        EngineMidiNote note={0,(uint64_t)rate*seconds*2,60,.15f};
        assert(engine_clip_midi_add_note(app->engine,midi,0,note,NULL));
    } else assert(engine_transport_set_loop(app->engine,true,0,(uint64_t)rate*4));
    assert(engine_start(app->engine));
    PacedConsumer consumer={.engine=app->engine}; atomic_init(&consumer.running,false);
    SDL_Thread* consumer_thread=NULL;
    if(paced) { audio_device_stop(&app->engine->device); atomic_store(&consumer.running,true);
        consumer_thread=SDL_CreateThread(paced_consume,"paced-proof",&consumer); assert(consumer_thread); }
    assert(engine_transport_play(app->engine)); SDL_Delay(600);
    if(mixed) assert(daw_audio_recording_begin_timeline_capture(app));
    EngineDiagnostics before, after; assert(engine_get_diagnostics(app->engine,&before));
    EngineAnalysisDiagnostics s0,g0,s1,g1;
    engine_get_analysis_diagnostics(app->engine,false,&s0);engine_get_analysis_diagnostics(app->engine,true,&g0);
    heap_observation("before_interval",0);
    uint64_t baseline=engine_media_resident_bytes(app->engine), rss_before=resident_bytes();
    double started=now_ms(), cpu=cpu_ms(), next_sample=0,next_edit=0,next_import=1,next_export=3,next_structure=7;
    double control_max=0; int edits=0,imports=0,exports=0,structures=0; bool import_active=false; int retirement_checks=0;
    printf("{\"type\":\"configuration\",\"mode\":\"%s\",\"rate\":%d,\"block\":%d,\"tracks\":%d,"
           "\"seconds\":%d,\"analysis\":%d,\"queue_blocks\":%d,\"pinned_baseline\":%llu}\n",
           mode,rate,block,tracks,seconds,analysis,queue,(unsigned long long)baseline);
    while(now_ms()-started < seconds*1000.0) {
        double elapsed=(now_ms()-started)/1000, began=now_ms();
        service(app);
        if(mixed && elapsed>=next_edit) {
            assert(engine_track_set_gain(app->engine,0,edits%2 ? .08f : .12f));
            assert(engine_track_set_pan(app->engine,0,edits%2 ? -.2f : .2f));
            ++edits;next_edit=elapsed+.25;
        }
        if(mixed && elapsed>=next_import && !import_active) {
            assert(engine_media_resident_bytes(app->engine)==baseline);++retirement_checks;
            assert(daw_media_import_submit(app,imported,NULL,0,(uint64_t)rate*(seconds*2+60),AUDIO_MEDIA_JOB_RECORDING));
            import_active=true;next_import=elapsed+5;
        }
        if(import_active && !daw_media_import_stats(app).jobs.admitted) {
            assert(!daw_media_import_stats(app).failed);
            assert(engine_get_tracks(app->engine)[0].clip_count==original_clips+1);
            assert(engine_remove_clip(app->engine,0,original_clips));++imports;import_active=false;
        }
        if(mixed && elapsed>=next_structure) {
            int added=engine_add_track(app->engine);assert(added==tracks+1);
            assert(engine_remove_track(app->engine,added));++structures;next_structure=elapsed+10;
        }
        if(mixed && elapsed>=next_export) {
            EngineBounceOptions options=engine_bounce_options_default();
            EngineBounceStreamCallbacks cb={.progress=export_progress,.samples=observe_export,.user=app};
            assert(engine_bounce_range_to_wav(app->engine,0,rate*2,&options,exported,ENGINE_BOUNCE_WAV_FLOAT32,&cb)==DAW_SAVE_SYNCED);
            AudioMediaInfo info;assert(audio_media_probe(exported,&info) && info.frame_count==(uint64_t)rate*2);
            ++exports;next_export=elapsed+10;
        }
        double cost=now_ms()-began;if(cost>control_max)control_max=cost;
        if(elapsed>=next_sample) {observation(app,&before,elapsed);next_sample=elapsed+1;}
        SDL_Delay(4);
    }
    double elapsed_ms=now_ms()-started, used_cpu=cpu_ms()-cpu;
    uint64_t captured=0,capture_missing=0,finalized=0; bool roundtrip=false,cancel_preserved=false;
    if(mixed) {
        audio_capture_device_stop(&app->audio_recording.capture_device);app->audio_recording.capture_device_started=false;
        captured=atomic_load(&app->audio_recording.captured_frames);capture_missing=atomic_load(&app->audio_recording.dropped_frames);
    }
    assert(engine_get_diagnostics(app->engine,&after));
    engine_get_analysis_diagnostics(app->engine,false,&s1);engine_get_analysis_diagnostics(app->engine,true,&g1);
    printf("{\"type\":\"scheduling\",\"priority_status\":%d}\n", after.worker_priority_status);
    uint64_t steady_rss=resident_bytes();
    if(mixed) {
        double settle=now_ms();
        while(daw_media_import_stats(app).jobs.admitted) {service(app);assert(now_ms()-settle<30000);SDL_Delay(4);}
        if(import_active) {assert(engine_remove_clip(app->engine,0,original_clips));++imports;}
        while(engine_media_resident_bytes(app->engine)!=baseline) {service(app);assert(now_ms()-settle<30000);SDL_Delay(4);}
        ++retirement_checks;
    }
    uint64_t settled_pinned=engine_media_resident_bytes(app->engine);
    heap_observation("retired_before_finalize",elapsed_ms/1000);
    if(mixed) {
        DawAudioRecordingResult result={0};assert(daw_audio_recording_finish(app,&result));
        finalized=result.frame_count;assert(result.saved && finalized==captured);
        double wait=now_ms();while(daw_media_import_stats(app).jobs.admitted) {service(app);assert(now_ms()-wait<30000);SDL_Delay(4);}
        assert(!daw_media_import_stats(app).failed);
        engine_transport_stop(app->engine);
        EngineBounceOptions options=engine_bounce_options_default();
        uint64_t previous_export=file_digest(exported);assert(export_energy>0);
        EngineBounceStreamCallbacks cb={.progress=cancel_export,.user=app};
        assert(engine_bounce_range_to_wav(app->engine,0,rate*2,&options,exported,ENGINE_BOUNCE_WAV_FLOAT32,&cb)==DAW_SAVE_FAILED);
        AudioMediaInfo info;cancel_preserved=audio_media_probe(exported,&info) && info.frame_count==(uint64_t)rate*2 && file_digest(exported)==previous_export;assert(cancel_preserved);
        char session[512];snprintf(session,sizeof(session),"%s/project.json",root);
        SessionDocument saved,loaded;session_document_init(&saved);session_document_init(&loaded);
        assert(session_document_capture(app,&saved));assert(session_document_write_file(&saved,session));
        assert(session_document_read_file(session,&loaded));assert(session_apply_document(app,&loaded));
        roundtrip=engine_get_track_count(app->engine)==tracks+1;assert(roundtrip);
        SessionDocument restored;session_document_init(&restored);assert(session_document_capture(app,&restored));
        assert(restored.track_count==saved.track_count);
        for(int t=0;t<saved.track_count;++t) {
            assert(restored.tracks[t].clip_count==saved.tracks[t].clip_count);
            for(int c=0;c<saved.tracks[t].clip_count;++c) {
                SessionClip* a=&saved.tracks[t].clips[c];SessionClip* b=&restored.tracks[t].clips[c];
                assert(a->kind==b->kind && a->start_frame==b->start_frame && a->duration_frames==b->duration_frames &&
                       a->midi_note_count==b->midi_note_count && !strcmp(a->media_path,b->media_path));
            }
        }
        session_document_free(&restored);session_document_free(&saved);session_document_free(&loaded);
    }
    if(paced) {atomic_store(&consumer.running,false);SDL_WaitThread(consumer_thread,NULL);assert(consumer.checksum>0);}
    printf("{\"type\":\"summary\",\"elapsed_ms\":%.3f,\"cpu_ms\":%.3f,\"callback_frames_per_second\":%.3f,"
           "\"output_missing_frames\":%llu,\"worker_over_budget\":%llu,\"worker_max_ms\":%.6f,\"service_max_ms\":%.6f,"
           "\"spectrum_dropped\":%llu,\"spectrogram_dropped\":%llu,\"spectrum_published\":%llu,\"spectrogram_published\":%llu,"
           "\"rss_before\":%llu,\"rss_steady_end\":%llu,\"pinned_baseline\":%llu,\"control_max_ms\":%.3f,"
           "\"edits\":%d,\"imports\":%d,\"exports\":%d,\"structural_pairs\":%d,\"captured_frames\":%llu,"
           "\"capture_missing_frames\":%llu,\"finalized_frames\":%llu,\"project_roundtrip\":%s,\"cancel_preserved\":%s,"
           "\"paced_late_periods\":%llu,\"paced_max_lateness_ms\":%.6f}\n",
           elapsed_ms,used_cpu,(after.callback_count-before.callback_count)*block*1000.0/elapsed_ms,
           (unsigned long long)(after.underrun_frames-before.underrun_frames),(unsigned long long)(after.worker_over_budget-before.worker_over_budget),
           after.worker_max_ns/1e6,after.service_max_ns/1e6,(unsigned long long)(s1.dropped-s0.dropped),(unsigned long long)(g1.dropped-g0.dropped),
           (unsigned long long)(s1.published-s0.published),(unsigned long long)(g1.published-g0.published),
           (unsigned long long)rss_before,(unsigned long long)steady_rss,(unsigned long long)baseline,control_max,edits,imports,exports,structures,
           (unsigned long long)captured,(unsigned long long)capture_missing,(unsigned long long)finalized,
           roundtrip?"true":"false",cancel_preserved?"true":"false",(unsigned long long)consumer.late_periods,consumer.max_lateness_ns/1e6);
    printf("{\"type\":\"ownership\",\"retirement_checks\":%d,\"settled_pinned_bytes\":%llu,\"export_energy\":%.9f}\n",
           retirement_checks,(unsigned long long)settled_pinned,export_energy);
    fflush(stdout);
    daw_media_import_shutdown(app);daw_audio_recording_free(&app->audio_recording);undo_manager_free(&app->undo);
    engine_destroy(app->engine);media_registry_shutdown(&app->media_registry);
    tempo_map_free(&app->tempo_map);time_signature_map_free(&app->time_signature_map);
    free(app->effects_panel.eq_curve_tracks);free(app->effects_panel.last_open_track_fx_ids);free(app->pending_track_fx);free(app);
    if (getenv("DAW_BENCH_UI")) {
        DawMainThreadMessageQueueStats stats;
        daw_mainthread_message_queue_snapshot(&stats);
        printf("{\"type\":\"ui_messages\",\"pushed\":%llu,\"popped\":%llu,\"high_water\":%u,\"max_latency_ms\":%.3f}\n",
            (unsigned long long)stats.pushed, (unsigned long long)stats.popped, stats.high_watermark,
            stats.drain_latency_max_ns / 1e6);
        assert(stats.pushed && stats.popped && stats.high_watermark <= 1024);
        daw_mainthread_message_queue_shutdown();
        daw_mainthread_wake_shutdown();
    }
    SDL_Quit();heap_observation("after_cleanup",elapsed_ms/1000);return 0;
}
