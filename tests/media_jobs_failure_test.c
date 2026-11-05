#include "test_temp_path.h"
#include "audio/media_jobs.h"
#include "core_workers.h"
#include "core_time.h"
#include <SDL2/SDL.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static atomic_int fail_after=-1, calls, live;
static bool reject_init,reject_submit;
// Injects each real decoder/resampler allocation and counts outstanding ownership.
static void* allocate(size_t n){atomic_fetch_add(&calls,1);int f=atomic_load(&fail_after);if(f==0)return NULL;if(f>0)atomic_fetch_sub(&fail_after,1);void* p=malloc(n);if(p)atomic_fetch_add(&live,1);return p;}
// Allocates the fixed media owner through the same failure counter.
static void* zero_allocate(size_t n,size_t z){void* p=allocate(n*z);if(p)memset(p,0,n*z);return p;}
// Counts release of source buffers, resampling scratch, results and the owner.
static void release(void* p){if(p)atomic_fetch_sub(&live,1);free(p);}
// Reallocation preserves allocation accounting on refusal and success.
static void* resize(void* p,size_t n){if(!p)return allocate(n);atomic_fetch_add(&calls,1);int f=atomic_load(&fail_after);if(f==0)return NULL;if(f>0)atomic_fetch_sub(&fail_after,1);return realloc(p,n);}
// Injects pool initialization failure without starting a partial fake worker.
static bool init(CoreWorkers* w,pthread_t* t,size_t nt,CoreWorkerTask* tasks,size_t n,CoreQueueMutex* q){return !reject_init && core_workers_init(w,t,nt,tasks,n,q);}
// Rejects only the selected admission; successful work uses the actual shared pool.
static bool submit(CoreWorkers* w,CoreWorkerTaskFn fn,void* c){return !reject_submit && core_workers_submit(w,fn,c);}
#define malloc allocate
#define calloc zero_allocate
#define realloc resize
#define free release
#include "../src/audio/resample.c"
#include "../src/audio/media_clip.c"
#define core_workers_init init
#define core_workers_submit submit
#include "../src/audio/media_jobs.c"
#undef core_workers_init
#undef core_workers_submit
#undef malloc
#undef calloc
#undef realloc
#undef free
// Waits on real ready publication, with a watchdog only for deadlock detection.
static void ready(AudioMediaJobs* j,size_t n){uint64_t end=core_time_now_ns()+UINT64_C(10000000000);while(audio_media_jobs_stats(j).ready!=n){assert(core_time_now_ns()<end);SDL_Delay(1);}}
// Writes exact PCM that requires the real resampling allocation path.
static void fixture(const char* path){unsigned char h[44]={'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',4,0,0,0};FILE* f=fopen(path,"wb");assert(f);assert(fwrite(h,1,44,f)==44);unsigned char p[]={0,64,0,192};assert(fwrite(p,1,4,f)==4);assert(!fclose(f));}
// Preserves an accepted predecessor and consumer-held PCM across each real allocation/refusal edge.
int main(void){
    DAW_TEST_TEMP_PATH(path,"media-failure-XXXXXX");int fd=mkstemp(path);assert(fd>=0);close(fd);fixture(path);
    atomic_store(&fail_after,0);assert(!audio_media_jobs_create(1024*1024));assert(!atomic_load(&live));
    atomic_store(&fail_after,-1);reject_init=true;assert(!audio_media_jobs_create(1024*1024));reject_init=false;assert(!atomic_load(&live));
    AudioMediaJobs* j=audio_media_jobs_create(1024*1024);assert(j);
    AudioMediaRequest r={.generation=1,.sample_rate=48000,.kind=AUDIO_MEDIA_JOB_LIBRARY};strcpy(r.path,path);
    atomic_store(&calls,0);assert(audio_media_jobs_submit(j,&r));ready(j,1);int boundaries=atomic_load(&calls);assert(boundaries>=3);
    AudioMediaResult held;assert(audio_media_jobs_poll(j,&held)&&held.ok);float saved[2];assert(held.clip.frame_count==2);memcpy(saved,held.clip.samples,sizeof(saved));
    for(int fail=0;fail<boundaries;++fail){
        uint64_t first=audio_media_jobs_submit(j,&r);assert(first);ready(j,1);
        atomic_store(&fail_after,fail);uint64_t bad=audio_media_jobs_submit(j,&r);assert(bad);ready(j,2);atomic_store(&fail_after,-1);
        AudioMediaJobStats before=audio_media_jobs_stats(j);reject_submit=true;assert(!audio_media_jobs_submit(j,&r));reject_submit=false;
        AudioMediaJobStats after=audio_media_jobs_stats(j);assert(after.admitted==before.admitted && after.submitted==before.submitted && after.rejected==before.rejected+1);
        AudioMediaResult out;assert(audio_media_jobs_poll(j,&out)&&out.id==first&&out.ok);audio_media_clip_free(&out.clip);
        assert(audio_media_jobs_poll(j,&out)&&out.id==bad&&!out.ok&&!out.clip.samples);audio_media_clip_free(&out.clip);
        assert(atomic_load(&live)==2 && !memcmp(saved,held.clip.samples,sizeof(saved)));
    }
    assert(audio_media_jobs_submit(j,&r));ready(j,1);AudioMediaResult out;assert(audio_media_jobs_poll(j,&out)&&out.ok);audio_media_clip_free(&out.clip);
    j->next_id=UINT64_MAX;assert(!audio_media_jobs_submit(j,&r));audio_media_jobs_destroy(j);
    assert(atomic_load(&live)==1 && !memcmp(saved,held.clip.samples,sizeof(saved)));audio_media_clip_free(&held.clip);assert(!atomic_load(&live));assert(!unlink(path));
    printf("media_jobs_failure_test: passed (%d decoder/resampler allocation boundaries, owner/pool/submit refusal, retained predecessor and held PCM, ID exhaustion)\n",boundaries);return 0;
}
