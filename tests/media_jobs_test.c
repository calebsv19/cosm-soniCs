#include "audio/media_jobs.h"
#include "audio/wav_writer.h"
#include "core_time.h"
#include <SDL2/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// Cancels at a selected checkpoint to exercise source decode and resample retirement.
static bool cancel_after(void* user) { int* count=user; return --*count <= 0; }
// Waits for a terminal owned result with a finite test deadline.
static AudioMediaResult wait_result(AudioMediaJobs* jobs) {
    AudioMediaResult result={0}; uint64_t start=core_time_now_ns();
    while (!audio_media_jobs_poll(jobs,&result)) { assert(core_time_now_ns()-start<UINT64_C(10000000000)); SDL_Delay(1); }
    return result;
}
// Writes a valid sparse mono PCM16 WAV without allocating the sample payload.
static void fixture(const char* path, unsigned frames) {
    unsigned char h[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
        0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',0,0,0,0};
    unsigned size=frames*2; for (int i=0;i<4;++i) { h[4+i]=(size+36)>>(8*i); h[40+i]=size>>(8*i); }
    FILE* f=fopen(path,"wb"); assert(f); assert(fwrite(h,1,44,f)==44); assert(!fclose(f)); assert(!truncate(path,44+size));
}
// Proves fixed admission, ready ownership, source changes, cancellation phases and shutdown cleanup.
int main(int argc, char** argv) {
    char path[]="/tmp/daw_jobs_XXXXXX"; int fd=mkstemp(path); assert(fd>=0); close(fd); fixture(path,44100);
    AudioMediaJobs* jobs=audio_media_jobs_create(1024*1024); assert(jobs);
    AudioMediaRequest request={.generation=1,.sample_rate=48000,.kind=AUDIO_MEDIA_JOB_LIBRARY}; strcpy(request.path,path);
    for (int i=0;i<4;++i) assert(audio_media_jobs_submit(jobs,&request));
    assert(!audio_media_jobs_submit(jobs,&request));
    while (audio_media_jobs_stats(jobs).ready!=4) SDL_Delay(1);
    AudioMediaJobStats stats=audio_media_jobs_stats(jobs); assert(stats.admitted==4 && stats.ready_bytes==4*48000*4);
    audio_media_jobs_cancel(jobs,2);
    int ok=0,cancelled=0; for(int i=0;i<4;++i) { AudioMediaResult r=wait_result(jobs); ok+=r.ok; cancelled+=r.cancelled; audio_media_clip_free(&r.clip); }
    assert(ok==3 && cancelled==1 && !audio_media_jobs_stats(jobs).admitted);
    assert(audio_media_jobs_submit(jobs,&request)); while(audio_media_jobs_stats(jobs).ready!=1) SDL_Delay(1);
    fixture(path,44101); AudioMediaResult r=wait_result(jobs); assert(!r.ok && r.source_changed && !r.clip.samples);
    fixture(path,441000);
    assert(audio_media_jobs_submit(jobs,&request)); r=wait_result(jobs); assert(!r.ok && !r.clip.samples); // Byte admission.
    audio_media_jobs_destroy(jobs);
    AudioMediaClip clip={0};
    for (int checkpoint=1;checkpoint<=1600;checkpoint+=100) {
        int count=checkpoint; AudioMediaLoadControl control={cancel_after,&count,8*1024*1024};
        bool loaded=audio_media_clip_load_controlled(path,48000,&clip,&control);
        if (loaded) audio_media_clip_free(&clip); else assert(!clip.samples);
    }
    jobs=audio_media_jobs_create(8*1024*1024); assert(jobs);
    for (int i=0;i<4;++i) assert(audio_media_jobs_submit(jobs,&request));
    audio_media_jobs_cancel(jobs,0);
    for (int i=0;i<4;++i) { r=wait_result(jobs); assert(!r.ok && r.cancelled && !r.clip.samples); }
    for (int i=0;i<4;++i) assert(audio_media_jobs_submit(jobs,&request));
    uint64_t start=core_time_now_ns(); audio_media_jobs_destroy(jobs);
    assert(core_time_now_ns()-start<UINT64_C(2000000000)); unlink(path);
    if (argc>1) {
        AudioMediaClip ordinary={0}, controlled={0};
        assert(audio_media_clip_load(argv[1],48000,&ordinary));
        AudioMediaLoadControl control={.max_sample_bytes=8*1024*1024};
        assert(audio_media_clip_load_controlled(argv[1],48000,&controlled,&control));
        assert(ordinary.frame_count==controlled.frame_count && ordinary.channels==controlled.channels);
        assert(!memcmp(ordinary.samples,controlled.samples,ordinary.frame_count*ordinary.channels*sizeof(float)));
        audio_media_clip_free(&ordinary);audio_media_clip_free(&controlled);
        control.max_sample_bytes=16;assert(!audio_media_clip_load_controlled(argv[1],48000,&controlled,&control));
        for(int checkpoint=1;checkpoint<1300;checkpoint+=100) {
            int count=checkpoint; control=(AudioMediaLoadControl){cancel_after,&count,8*1024*1024};
            bool ok=audio_media_clip_load_controlled(argv[1],96000,&controlled,&control);
            if (ok) audio_media_clip_free(&controlled);else assert(!controlled.samples);
        }
        puts("media_jobs_test: native MP3 parity, budget and cancellation passed");
    }
    puts("media_jobs_test: success"); return 0;
}
