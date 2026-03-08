#ifndef MEM_CONSOLE_RUNTIME_H
#define MEM_CONSOLE_RUNTIME_H

#include <stdint.h>

#include "core_queue.h"
#include "core_wake.h"
#include "core_workers.h"
#include "mem_console_state.h"

enum {
    MEM_CONSOLE_RUNTIME_WORKER_THREADS = 1,
    MEM_CONSOLE_RUNTIME_TASK_CAPACITY = 16,
    MEM_CONSOLE_RUNTIME_COMPLETION_CAPACITY = 32
};

typedef struct MemConsoleRuntime {
    CoreWorkers workers;
    pthread_t worker_threads[MEM_CONSOLE_RUNTIME_WORKER_THREADS];
    CoreWorkerTask worker_tasks[MEM_CONSOLE_RUNTIME_TASK_CAPACITY];

    CoreQueueMutex completion_queue;
    void *completion_slots[MEM_CONSOLE_RUNTIME_COMPLETION_CAPACITY];

    CoreWake wake;

    uint64_t next_request_id;
    uint64_t last_applied_request_id;
    uint64_t next_poll_due_ms;
    uint32_t poll_interval_ms;
    int refresh_in_flight;
    uint64_t in_flight_request_id;
    char in_flight_search_text[256];
    int64_t in_flight_selected_item_id;
    int in_flight_list_query_offset;
    int in_flight_selected_project_count;
    char in_flight_selected_project_keys[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64];
    char in_flight_graph_kind_filter[32];
    int in_flight_graph_edge_limit;
    int in_flight_graph_hops;
    int pending_intent_valid;
    char pending_search_text[256];
    int64_t pending_selected_item_id;
    int pending_list_query_offset;
    int pending_selected_project_count;
    char pending_selected_project_keys[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64];
    char pending_graph_kind_filter[32];
    int pending_graph_edge_limit;
    int pending_graph_hops;
    uint64_t stats_refresh_submitted;
    uint64_t stats_refresh_applied;
    uint64_t stats_refresh_dropped_stale;
    uint64_t stats_refresh_dropped_mismatch;
    uint64_t stats_refresh_dropped_editing;
    uint64_t stats_refresh_errors;
    uint64_t stats_refresh_coalesced;
    int queue_initialized;
    int wake_initialized;
} MemConsoleRuntime;

CoreResult mem_console_runtime_init(MemConsoleRuntime *runtime, uint64_t now_ms);
void mem_console_runtime_shutdown(MemConsoleRuntime *runtime);

void mem_console_runtime_note_local_write(MemConsoleRuntime *runtime, uint64_t now_ms);
void mem_console_runtime_tick(MemConsoleRuntime *runtime,
                              MemConsoleState *state,
                              uint64_t now_ms);
uint32_t mem_console_runtime_idle_wait_ms(const MemConsoleRuntime *runtime,
                                          const MemConsoleState *state,
                                          uint64_t now_ms);

#endif
