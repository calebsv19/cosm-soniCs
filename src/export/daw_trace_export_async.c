#include "export/daw_trace_export_async.h"

#include "core_queue.h"
#include "core_workers.h"

#include <stdlib.h>
#include <string.h>

#define DAW_TRACE_ASYNC_WORKER_COUNT 1u
#define DAW_TRACE_ASYNC_TASK_CAPACITY 32u
#define DAW_TRACE_ASYNC_COMPLETION_CAPACITY 64u
#define DAW_TRACE_ASYNC_PATH_MAX 512u

// Retains admitted inputs and their outcome until the owner consumes them.
typedef struct DawTraceExportTask {
    char pack_path[DAW_TRACE_ASYNC_PATH_MAX];
    DawTraceDiagnostics diagnostics;
    bool ok;
} DawTraceExportTask;

static CoreWorkers g_workers;
static pthread_t g_worker_threads[DAW_TRACE_ASYNC_WORKER_COUNT];
static CoreWorkerTask g_worker_tasks[DAW_TRACE_ASYNC_TASK_CAPACITY];
static CoreQueueMutex g_completion_queue;
static void* g_completion_backing[DAW_TRACE_ASYNC_COMPLETION_CAPACITY];
static DawTraceExportAsyncStats g_stats;
static bool g_initialized = false;
static size_t g_outstanding;

// Publishes the same admitted allocation, without a fallible outcome allocation.
static void* daw_trace_export_worker_main(void* task_ctx) {
    DawTraceExportTask* task = (DawTraceExportTask*)task_ctx;
    task->ok = daw_trace_export_diagnostics(task->pack_path, &task->diagnostics);
    return task;
}

// Initializes a single-owner lane with bounded lifetime completion capacity.
bool daw_trace_export_async_init(void) {
    if (g_initialized) {
        return true;
    }

    memset(&g_workers, 0, sizeof(g_workers));
    memset(&g_stats, 0, sizeof(g_stats));
    g_outstanding = 0;
    if (!core_queue_mutex_init_ex(&g_completion_queue,
                                  g_completion_backing,
                                  DAW_TRACE_ASYNC_COMPLETION_CAPACITY,
                                  CORE_QUEUE_OVERFLOW_REJECT)) {
        return false;
    }
    if (!core_workers_init(&g_workers,
                           g_worker_threads,
                           DAW_TRACE_ASYNC_WORKER_COUNT,
                           g_worker_tasks,
                           DAW_TRACE_ASYNC_TASK_CAPACITY,
                           &g_completion_queue)) {
        core_queue_mutex_destroy(&g_completion_queue);
        return false;
    }

    g_initialized = true;
    return true;
}

// Joins producers, accounts every admitted outcome, then destroys the queue.
void daw_trace_export_async_shutdown(void) {
    if (!g_initialized) return;
    core_workers_shutdown_with_mode(&g_workers, CORE_WORKERS_SHUTDOWN_DRAIN);
    (void)daw_trace_export_async_drain(DAW_TRACE_ASYNC_COMPLETION_CAPACITY);
    core_queue_mutex_destroy(&g_completion_queue);
    g_initialized = false;
}

// Reserves an outcome slot before accepting copied inputs into the worker pool.
bool daw_trace_export_async_enqueue(const char* pack_path, const DawTraceDiagnostics* diagnostics) {
    DawTraceExportTask* task = NULL;
    size_t path_len = 0;
    if (!g_initialized || !pack_path || !diagnostics) {
        return false;
    }

    path_len = strnlen(pack_path, DAW_TRACE_ASYNC_PATH_MAX);
    if (path_len == 0 || path_len >= DAW_TRACE_ASYNC_PATH_MAX) {
        return false;
    }

    if (g_outstanding >= DAW_TRACE_ASYNC_COMPLETION_CAPACITY) {
        g_stats.rejected += 1u;
        return false;
    }

    task = (DawTraceExportTask*)calloc(1u, sizeof(*task));
    if (!task) {
        g_stats.rejected += 1u;
        return false;
    }
    memcpy(task->pack_path, pack_path, path_len);
    task->pack_path[path_len] = '\0';
    task->diagnostics = *diagnostics;

    if (!core_workers_submit(&g_workers, daw_trace_export_worker_main, task)) {
        free(task);
        g_stats.rejected += 1u;
        return false;
    }

    g_outstanding += 1u;
    g_stats.submitted += 1u;
    return true;
}

// Accounts terminal outcomes and releases their lifetime reservations.
size_t daw_trace_export_async_drain(size_t max_items) {
    size_t n = 0;
    void* item = NULL;
    if (!g_initialized || max_items == 0u) {
        return 0u;
    }

    while (n < max_items && core_queue_mutex_pop(&g_completion_queue, &item)) {
        DawTraceExportTask* completion = (DawTraceExportTask*)item;
        g_stats.completed += 1u;
        if (!completion || !completion->ok) {
            g_stats.failed += 1u;
        }
        free(completion);
        g_outstanding -= 1u;
        n += 1u;
    }
    return n;
}

// Reads owner-thread accounting, retained through shutdown until the next init.
void daw_trace_export_async_snapshot(DawTraceExportAsyncStats* out) {
    if (!out) {
        return;
    }
    *out = g_stats;
}
