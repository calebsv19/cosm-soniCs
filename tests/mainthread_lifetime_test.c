#include <SDL2/SDL.h>
#include "core_queue.h"
#include "core_time.h"
#include "core/loop/daw_mainthread_wake.h"
#include "core/loop/daw_mainthread_jobs.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
static atomic_bool race, pushed, consumer_lock_seen;
static _Thread_local bool consumer;
static _Thread_local unsigned locks;
static atomic_int live;
static bool fail_alloc, fail_wake, real_wake;
// Counts only notification nodes, not SDL or worker internals.
static void* node_alloc(size_t n,size_t z){if(fail_alloc)return NULL;void* p=calloc(n,z);if(p)atomic_fetch_add(&live,1);return p;}
// Releases exactly the notification-owned allocation.
static void node_free(void* p){if(p)atomic_fetch_sub(&live,1);free(p);}
// Makes wake failure observable without opening a window.
static bool wake(void){return real_wake?daw_mainthread_wake_push():!fail_wake;}
// Forces pop to reach the stats lock while the producer has published its queue entry.
static int lock(SDL_mutex* m){
    if(consumer && atomic_load(&race)){
        int result=SDL_TryLockMutex(m);assert(result==0 || result==SDL_MUTEX_TIMEDOUT);
        atomic_store(&consumer_lock_seen,true);
        int locked=result==0?0:SDL_LockMutex(m);if(!locked)++locks;return locked;
    }
    int locked=SDL_LockMutex(m);if(!locked)++locks;return locked;
}
// Checks that all failure paths release only an acquired owner lock.
static int unlock(SDL_mutex* m){assert(locks);--locks;return SDL_UnlockMutex(m);}
// Holds the producer after real queue publication until the consumer reaches owner serialization.
static bool push(CoreQueueMutex* q,void* p){
    bool ok=core_queue_mutex_push(q,p);
    if(ok && atomic_load(&race)){
        atomic_store(&pushed,true);uint64_t end=core_time_now_ns()+UINT64_C(10000000000);
        while(!atomic_load(&consumer_lock_seen)){assert(core_time_now_ns()<end);SDL_Delay(1);}
    }
    return ok;
}
#define calloc node_alloc
#define free node_free
#define SDL_LockMutex lock
#define SDL_UnlockMutex unlock
#define core_queue_mutex_push push
#define daw_mainthread_wake_push wake
#include "../src/core/loop/daw_mainthread_messages.c"
#undef calloc
#undef free
#undef SDL_LockMutex
#undef SDL_UnlockMutex
#undef core_queue_mutex_push
#undef daw_mainthread_wake_push
// Posts through the real producer path.
static void* producer(void* unused){(void)unused;assert(daw_mainthread_message_post(DAW_MAINTHREAD_MSG_ENGINE_TRANSPORT,7,NULL));return NULL;}
// Pops through the real consumer path at the forced publication interleaving.
static void* reader(void* unused){(void)unused;consumer=true;DawMainThreadMessage m;assert(daw_mainthread_message_queue_pop(&m)&&m.user_u64==7);return NULL;}
static unsigned executed;
// Verifies borrowed main-thread contexts remain ordered until synchronous execution.
static void job(void* p){assert(*(unsigned*)p==executed++);}
// Exercises publication/pop coalescing, capacity/refusal, wake failure, reset and borrowed jobs.
int main(void){
    assert(!SDL_Init(SDL_INIT_TIMER|SDL_INIT_EVENTS));daw_mainthread_message_queue_init();
    atomic_store(&race,true);pthread_t a,b;assert(!pthread_create(&a,NULL,producer,NULL));
    uint64_t end=core_time_now_ns()+UINT64_C(10000000000);
    while(!atomic_load(&pushed)){assert(core_time_now_ns()<end);SDL_Delay(1);}
    assert(!pthread_create(&b,NULL,reader,NULL));assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));
    atomic_store(&race,false);assert(!atomic_load(&live));
    assert(daw_mainthread_message_post(DAW_MAINTHREAD_MSG_ENGINE_TRANSPORT,8,NULL));
    DawMainThreadMessage m;assert(daw_mainthread_message_queue_pop(&m)&&m.user_u64==8);
    fail_alloc=true;assert(!daw_mainthread_message_post(DAW_MAINTHREAD_MSG_USER,0,NULL));fail_alloc=false;
    fail_wake=true;assert(daw_mainthread_message_post(DAW_MAINTHREAD_MSG_USER,0,NULL));assert(daw_mainthread_message_queue_has_pending());fail_wake=false;
    for(unsigned i=1;i<1024;++i)assert(daw_mainthread_message_post(DAW_MAINTHREAD_MSG_USER,i,NULL));
    assert(!daw_mainthread_message_post(DAW_MAINTHREAD_MSG_USER,1024,NULL));
    for(unsigned i=0;i<1024;++i){assert(daw_mainthread_message_queue_pop(&m));assert(m.user_u64==i);}
    assert(!atomic_load(&live));
    assert(daw_mainthread_message_post(DAW_MAINTHREAD_MSG_ENGINE_FX_METER,1,NULL));
    assert(daw_mainthread_message_post(DAW_MAINTHREAD_MSG_ENGINE_FX_METER,2,NULL));assert(atomic_load(&live)==1);
    daw_mainthread_message_queue_shutdown();assert(!atomic_load(&live));assert(!daw_mainthread_message_post(DAW_MAINTHREAD_MSG_USER,1,NULL));
    daw_mainthread_message_queue_init();assert(daw_mainthread_message_post(DAW_MAINTHREAD_MSG_ENGINE_FX_METER,3,NULL));daw_mainthread_message_queue_shutdown();assert(!atomic_load(&live));
    real_wake=true;assert(daw_mainthread_wake_init());daw_mainthread_message_queue_init();
    assert(daw_mainthread_message_post(DAW_MAINTHREAD_MSG_USER,42,NULL));SDL_Event event;assert(SDL_WaitEventTimeout(&event,1000));assert(daw_mainthread_wake_is_event(&event));
    assert(daw_mainthread_message_queue_pop(&m)&&m.user_u64==42);daw_mainthread_message_queue_shutdown();daw_mainthread_wake_shutdown();
    unsigned ids[256];daw_mainthread_jobs_init();for(unsigned i=0;i<256;++i){ids[i]=i;assert(daw_mainthread_jobs_enqueue(job,&ids[i]));}
    assert(!daw_mainthread_jobs_enqueue(job,&ids[0]));assert(daw_mainthread_jobs_run_n(256)==256 && executed==256);
    assert(daw_mainthread_jobs_enqueue(job,&ids[0]));daw_mainthread_jobs_shutdown();assert(!daw_mainthread_jobs_run_n(1));
    daw_mainthread_jobs_init();DawMainThreadJobsStats stats;daw_mainthread_jobs_snapshot(&stats);assert(!stats.pending);daw_mainthread_jobs_shutdown();
    SDL_Quit();puts("mainthread_lifetime_test: passed (publish/pop race, coalescing, allocation/capacity/wake refusal, shutdown and borrowed jobs)");return 0;
}
