/* Exercise the actual app owner and shared pool with a controlled export boundary. */
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "export/daw_trace_export_async.h"
#include "core_queue.h"
#include "core_workers.h"

static atomic_bool fail_allocation, forbid_worker_allocation, hold, entered;
static atomic_uint allocations, frees, exports, worker_allocations;
static pthread_t owner;
static char observed_path[512];
static uint64_t observed_frame;

/* Count owner allocations and inject refusal without changing shared allocators. */
static void *proof_calloc(size_t n, size_t size) {
    if (!pthread_equal(owner, pthread_self())) {
        atomic_fetch_add(&worker_allocations, 1);
        if (atomic_load(&forbid_worker_allocation)) return NULL;
    }
    if (atomic_exchange(&fail_allocation, false)) return NULL;
    void *p = calloc(n, size);
    if (p) atomic_fetch_add(&allocations, 1);
    return p;
}
/* Count each retained task/result release. */
static void proof_free(void *p) {
    if (p) atomic_fetch_add(&frees, 1);
    free(p);
}
/* Hold actual worker execution until the owner establishes the requested state. */
static bool proof_export(const char *path, const DawTraceDiagnostics *diagnostics) {
    atomic_store(&entered, true);
    while (atomic_load(&hold)) { struct timespec t = {0,1000000}; nanosleep(&t, NULL); }
    snprintf(observed_path, sizeof(observed_path), "%s", path);
    observed_frame = diagnostics->transport_frame;
    atomic_fetch_add(&exports, 1);
    return diagnostics->sample_rate != 0;
}
#define calloc proof_calloc
#define free proof_free
#define daw_trace_export_diagnostics proof_export
#include "../src/export/daw_trace_export_async.c"
#undef calloc
#undef free
#undef daw_trace_export_diagnostics

/* A synchronized queue predicate establishes publication; time is only a watchdog. */
static void wait_queue(size_t count) {
    for (unsigned i=0; i<5000; i++) {
        if (core_queue_mutex_size(&g_completion_queue)==count) return;
        struct timespec t={0,1000000}; nanosleep(&t,NULL);
    }
    fprintf(stderr,"completion publication timed out at %zu\n",count); abort();
}
/* Wait until the worker has entered the deliberately blocked exporter. */
static void wait_entered(void) {
    for (unsigned i=0; i<5000; i++) {
        if (atomic_load(&entered)) return;
        struct timespec t={0,1000000}; nanosleep(&t,NULL);
    }
    abort();
}
/* Release the producer only after shutdown has closed pool admission. */
static void *release_on_stop(void *unused) {
    (void)unused;
    for (unsigned i=0; i<5000; i++) {
        pthread_mutex_lock(&g_workers.mu); bool stop=g_workers.stop; pthread_mutex_unlock(&g_workers.mu);
        if (stop) { atomic_store(&hold,false); return NULL; }
        struct timespec t={0,1000000}; nanosleep(&t,NULL);
    }
    abort();
}
/* Verify lifetime capacity, partial drain, copied input, refusal and reuse. */
static void pressure(void) {
    DawTraceDiagnostics d={.sample_rate=48000,.transport_frame=42};
    assert(daw_trace_export_async_init());
    char path[32]="original.pack";
    atomic_store(&hold,true);
    assert(daw_trace_export_async_enqueue(path,&d)); wait_entered();
    strcpy(path,"overwritten"); d.transport_frame=99;
    atomic_store(&hold,false); wait_queue(1);
    assert(!strcmp(observed_path,"original.pack") && observed_frame==42);
    for (size_t i=1;i<64;i++) {
        assert(daw_trace_export_async_enqueue("test.pack",&d)); wait_queue(i+1);
    }
    assert(!daw_trace_export_async_enqueue("overflow.pack",&d));
    assert(core_queue_mutex_stats(&g_completion_queue).push_fail==0);
    assert(daw_trace_export_async_drain(0)==0);
    assert(daw_trace_export_async_drain(7)==7);
    for (size_t i=0;i<7;i++) { assert(daw_trace_export_async_enqueue("reuse.pack",&d)); wait_queue(58+i); }
    assert(!daw_trace_export_async_enqueue("overflow.pack",&d));
    assert(daw_trace_export_async_drain(100)==64);
    DawTraceExportAsyncStats s; daw_trace_export_async_snapshot(&s);
    assert(s.submitted==71 && s.completed==71 && s.rejected==2 && !s.failed);
    daw_trace_export_async_shutdown();
}
/* Admission owns outcome storage so workers need no late allocation to report failure. */
static void allocation_failure(void) {
    DawTraceDiagnostics d={.sample_rate=48000};
    assert(daw_trace_export_async_init());
    atomic_store(&fail_allocation,true);
    assert(!daw_trace_export_async_enqueue("refused.pack",&d));
    atomic_store(&forbid_worker_allocation,true);
    assert(daw_trace_export_async_enqueue("success.pack",&d)); wait_queue(1);
    d.sample_rate=0;
    assert(daw_trace_export_async_enqueue("failure.pack",&d)); wait_queue(2);
    assert(daw_trace_export_async_drain(9)==2);
    DawTraceExportAsyncStats s; daw_trace_export_async_snapshot(&s);
    assert(s.submitted==2 && s.completed==2 && s.failed==1 && s.rejected==1);
    assert(!atomic_load(&worker_allocations));
    daw_trace_export_async_shutdown();
}
/* Drain shutdown accounts for completed, running and pending tasks before freeing storage. */
static void shutdown_pressure(void) {
    DawTraceDiagnostics d={.sample_rate=48000};
    for (size_t prefix=0; prefix<=31; prefix+=31) {
    assert(daw_trace_export_async_init());
    for (size_t i=0;i<prefix;i++) { assert(daw_trace_export_async_enqueue("done.pack",&d)); wait_queue(i+1); }
    atomic_store(&hold,true); atomic_store(&entered,false);
    d.sample_rate=0;
    assert(daw_trace_export_async_enqueue("running.pack",&d)); wait_entered();
    for (size_t i=0;i<32;i++) assert(daw_trace_export_async_enqueue("pending.pack",&d));
    assert(!daw_trace_export_async_enqueue("overflow.pack",&d));
    pthread_t release; assert(!pthread_create(&release,NULL,release_on_stop,NULL));
    daw_trace_export_async_shutdown(); assert(!pthread_join(release,NULL));
    DawTraceExportAsyncStats s; daw_trace_export_async_snapshot(&s);
    assert(s.submitted==prefix+33 && s.completed==prefix+33 && s.failed==33 && s.rejected==1);
    assert(!daw_trace_export_async_enqueue("closed.pack",&d));
    daw_trace_export_async_shutdown();
    assert(daw_trace_export_async_init()); d.sample_rate=48000;
    assert(daw_trace_export_async_enqueue("restart.pack",&d)); wait_queue(1);
    daw_trace_export_async_shutdown(); daw_trace_export_async_snapshot(&s);
    assert(s.submitted==1 && s.completed==1 && !s.failed);
    }
}
/* Select independently reproducible cases and verify every owned allocation was released. */
int main(int argc,char **argv) {
    owner=pthread_self(); assert(argc==2);
    if (!strcmp(argv[1],"pressure")) pressure();
    else if (!strcmp(argv[1],"allocation")) allocation_failure();
    else if (!strcmp(argv[1],"shutdown")) shutdown_pressure();
    else abort();
    assert(atomic_load(&allocations)==atomic_load(&frees));
    printf("trace pressure %s passed; allocations=%u frees=%u exports=%u\n",argv[1],atomic_load(&allocations),atomic_load(&frees),atomic_load(&exports));
    return 0;
}
