#include "export/daw_trace_export_async.h"

#include "core_queue.h"
#include "core_workers.h"

#include <stdlib.h>
#include <string.h>

#define DAW_TRACE_ASYNC_WORKER_COUNT 1u
#define DAW_TRACE_ASYNC_TASK_CAPACITY 32u
#define DAW_TRACE_ASYNC_COMPLETION_CAPACITY 64u
#define DAW_TRACE_ASYNC_PATH_MAX 512u

typedef struct DawTraceExportTask {
    char pack_path[DAW_TRACE_ASYNC_PATH_MAX];
    DawTraceDiagnostics diagnostics;
} DawTraceExportTask;

typedef struct DawTraceExportCompletion {
    bool ok;
} DawTraceExportCompletion;

static CoreWorkers g_workers;
static pthread_t g_worker_threads[DAW_TRACE_ASYNC_WORKER_COUNT];
static CoreWorkerTask g_worker_tasks[DAW_TRACE_ASYNC_TASK_CAPACITY];
static CoreQueueMutex g_completion_queue;
static void* g_completion_backing[DAW_TRACE_ASYNC_COMPLETION_CAPACITY];
static DawTraceExportAsyncStats g_stats;
static bool g_initialized = false;

static void* daw_trace_export_worker_main(void* task_ctx) {
    DawTraceExportTask* task = (DawTraceExportTask*)task_ctx;
    DawTraceExportCompletion* completion = NULL;
    bool ok = false;
    if (!task) {
        return NULL;
    }

    completion = (DawTraceExportCompletion*)calloc(1u, sizeof(*completion));
    if (!completion) {
        free(task);
        return NULL;
    }

    ok = daw_trace_export_diagnostics(task->pack_path, &task->diagnostics);
    completion->ok = ok;
    free(task);
    return completion;
}

bool daw_trace_export_async_init(void) {
    if (g_initialized) {
        return true;
    }

    memset(&g_workers, 0, sizeof(g_workers));
    memset(&g_stats, 0, sizeof(g_stats));
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

void daw_trace_export_async_shutdown(void) {
    void* item = NULL;
    if (!g_initialized) {
        return;
    }

    core_workers_shutdown_with_mode(&g_workers, CORE_WORKERS_SHUTDOWN_DRAIN);
    while (core_queue_mutex_pop(&g_completion_queue, &item)) {
        DawTraceExportCompletion* completion = (DawTraceExportCompletion*)item;
        free(completion);
    }
    core_queue_mutex_destroy(&g_completion_queue);
    g_initialized = false;
}

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

    g_stats.submitted += 1u;
    return true;
}

size_t daw_trace_export_async_drain(size_t max_items) {
    size_t n = 0;
    void* item = NULL;
    if (!g_initialized || max_items == 0u) {
        return 0u;
    }

    while (n < max_items && core_queue_mutex_pop(&g_completion_queue, &item)) {
        DawTraceExportCompletion* completion = (DawTraceExportCompletion*)item;
        g_stats.completed += 1u;
        if (!completion || !completion->ok) {
            g_stats.failed += 1u;
        }
        free(completion);
        n += 1u;
    }
    return n;
}

void daw_trace_export_async_snapshot(DawTraceExportAsyncStats* out) {
    if (!out) {
        return;
    }
    *out = g_stats;
}
