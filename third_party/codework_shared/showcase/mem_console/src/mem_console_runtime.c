#include "mem_console_runtime.h"

#include <stdio.h>
#include <string.h>

#include "core_base.h"
#include "mem_console_db.h"

enum {
    MEM_CONSOLE_RUNTIME_DEFAULT_POLL_MS = 900,
    MEM_CONSOLE_RUNTIME_IDLE_MAX_WAIT_MS = 33,
    MEM_CONSOLE_RUNTIME_IDLE_ACTIVE_WAIT_MS = 8
};

typedef struct MemConsoleRefreshTask {
    char db_path[1024];
    char search_text[256];
    int64_t selected_item_id;
    int list_query_offset;
    int selected_project_count;
    char selected_project_keys[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64];
    char graph_kind_filter[32];
    int graph_edge_limit;
    int graph_hops;
    uint64_t request_id;
    CoreWake *wake;
} MemConsoleRefreshTask;

typedef struct MemConsoleRefreshCompletion {
    CoreResult result;
    char error_message[160];
    char search_text[256];
    int64_t selected_item_id;
    int list_query_offset;
    int selected_project_count;
    char selected_project_keys[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64];
    char graph_kind_filter[32];
    int graph_edge_limit;
    int graph_hops;
    uint64_t request_id;
    MemConsoleState refreshed_state;
} MemConsoleRefreshCompletion;

static void copy_selected_project_filters(char destination[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64],
                                          int *out_count,
                                          const char source[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64],
                                          int count) {
    int i;
    int clamped_count = count;

    if (!destination || !out_count || !source) {
        return;
    }

    if (clamped_count < 0) {
        clamped_count = 0;
    }
    if (clamped_count > MEM_CONSOLE_SCOPE_FILTER_LIMIT) {
        clamped_count = MEM_CONSOLE_SCOPE_FILTER_LIMIT;
    }

    for (i = 0; i < MEM_CONSOLE_SCOPE_FILTER_LIMIT; ++i) {
        if (i < clamped_count) {
            (void)snprintf(destination[i], 64, "%s", source[i]);
        } else {
            destination[i][0] = '\0';
        }
    }
    *out_count = clamped_count;
}

static int selected_project_filters_match(const char filters_a[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64],
                                          int count_a,
                                          const char filters_b[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64],
                                          int count_b) {
    int i;

    if (!filters_a || !filters_b) {
        return 0;
    }
    if (count_a != count_b) {
        return 0;
    }
    if (count_a < 0 || count_a > MEM_CONSOLE_SCOPE_FILTER_LIMIT) {
        return 0;
    }
    if (count_b < 0 || count_b > MEM_CONSOLE_SCOPE_FILTER_LIMIT) {
        return 0;
    }

    for (i = 0; i < count_a; ++i) {
        if (strcmp(filters_a[i], filters_b[i]) != 0) {
            return 0;
        }
    }
    return 1;
}

static void apply_refreshed_state(MemConsoleState *state, const MemConsoleState *refreshed) {
    if (!state || !refreshed) {
        return;
    }

    (void)snprintf(state->schema_version, sizeof(state->schema_version), "%s", refreshed->schema_version);
    state->active_count = refreshed->active_count;
    state->matching_count = refreshed->matching_count;
    state->visible_start_index = refreshed->visible_start_index;
    state->visible_count = refreshed->visible_count;
    state->project_filter_option_count = refreshed->project_filter_option_count;
    state->selected_project_count = refreshed->selected_project_count;
    memcpy(state->visible_items, refreshed->visible_items, sizeof(state->visible_items));
    memcpy(state->project_filter_labels, refreshed->project_filter_labels, sizeof(state->project_filter_labels));
    memcpy(state->project_filter_keys, refreshed->project_filter_keys, sizeof(state->project_filter_keys));
    memcpy(state->project_filter_counts, refreshed->project_filter_counts, sizeof(state->project_filter_counts));
    memcpy(state->selected_project_keys, refreshed->selected_project_keys, sizeof(state->selected_project_keys));
    (void)snprintf(state->project_filter_summary_line,
                   sizeof(state->project_filter_summary_line),
                   "%s",
                   refreshed->project_filter_summary_line);

    state->selected_item_id = refreshed->selected_item_id;
    state->selected_pinned = refreshed->selected_pinned;
    state->selected_canonical = refreshed->selected_canonical;
    (void)snprintf(state->selected_title, sizeof(state->selected_title), "%s", refreshed->selected_title);
    (void)snprintf(state->selected_body, sizeof(state->selected_body), "%s", refreshed->selected_body);

    state->graph_node_count = refreshed->graph_node_count;
    state->graph_edge_count = refreshed->graph_edge_count;
    memcpy(state->graph_nodes, refreshed->graph_nodes, sizeof(state->graph_nodes));
    memcpy(state->graph_edges, refreshed->graph_edges, sizeof(state->graph_edges));
    mem_console_graph_edge_limit_set(state, refreshed->graph_query_edge_limit);
    state->graph_query_hops = mem_console_graph_hops_clamp(refreshed->graph_query_hops);
}

static void publish_runtime_metrics(const MemConsoleRuntime *runtime, MemConsoleState *state) {
    uint64_t dropped_total;

    if (!runtime || !state) {
        return;
    }

    dropped_total = runtime->stats_refresh_dropped_stale +
                    runtime->stats_refresh_dropped_mismatch +
                    runtime->stats_refresh_dropped_editing;

    state->runtime_refresh_submitted = runtime->stats_refresh_submitted;
    state->runtime_refresh_applied = runtime->stats_refresh_applied;
    state->runtime_refresh_dropped = dropped_total;
    state->runtime_refresh_errors = runtime->stats_refresh_errors;
    state->runtime_refresh_coalesced = runtime->stats_refresh_coalesced;
    state->runtime_refresh_in_flight = runtime->refresh_in_flight;
    state->runtime_pending_intent = runtime->pending_intent_valid;

    (void)snprintf(state->runtime_summary_line,
                   sizeof(state->runtime_summary_line),
                   "Async s%llu a%llu d%llu e%llu c%llu | if=%d p=%d",
                   (unsigned long long)state->runtime_refresh_submitted,
                   (unsigned long long)state->runtime_refresh_applied,
                   (unsigned long long)state->runtime_refresh_dropped,
                   (unsigned long long)state->runtime_refresh_errors,
                   (unsigned long long)state->runtime_refresh_coalesced,
                   state->runtime_refresh_in_flight,
                   state->runtime_pending_intent);
}

static void capture_intent_from_state(const MemConsoleState *state,
                                      char *out_search_text,
                                      size_t out_search_cap,
                                      int64_t *out_selected_item_id,
                                      int *out_list_query_offset,
                                      char out_selected_project_keys[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64],
                                      int *out_selected_project_count,
                                      char *out_graph_kind_filter,
                                      size_t out_graph_kind_filter_cap,
                                      int *out_graph_edge_limit,
                                      int *out_graph_hops) {
    if (!state || !out_search_text || out_search_cap == 0u || !out_selected_item_id ||
        !out_list_query_offset || !out_selected_project_keys || !out_selected_project_count ||
        !out_graph_kind_filter || out_graph_kind_filter_cap == 0u ||
        !out_graph_edge_limit || !out_graph_hops) {
        return;
    }
    (void)snprintf(out_search_text, out_search_cap, "%s", state->search_text);
    *out_selected_item_id = state->selected_item_id;
    *out_list_query_offset = state->list_query_offset;
    copy_selected_project_filters(out_selected_project_keys,
                                  out_selected_project_count,
                                  state->selected_project_keys,
                                  state->selected_project_count);
    (void)snprintf(out_graph_kind_filter, out_graph_kind_filter_cap, "%s", state->graph_kind_filter);
    *out_graph_edge_limit = mem_console_graph_edge_limit_clamp(state->graph_query_edge_limit);
    *out_graph_hops = mem_console_graph_hops_clamp(state->graph_query_hops);
}

static int intent_matches(const char *search_text_a,
                          int64_t selected_item_id_a,
                          int list_query_offset_a,
                          const char selected_project_keys_a[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64],
                          int selected_project_count_a,
                          const char *graph_kind_filter_a,
                          int graph_edge_limit_a,
                          int graph_hops_a,
                          const char *search_text_b,
                          int64_t selected_item_id_b,
                          int list_query_offset_b,
                          const char selected_project_keys_b[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64],
                          int selected_project_count_b,
                          const char *graph_kind_filter_b,
                          int graph_edge_limit_b,
                          int graph_hops_b) {
    if (!search_text_a || !search_text_b || !graph_kind_filter_a || !graph_kind_filter_b) {
        return 0;
    }
    return strcmp(search_text_a, search_text_b) == 0 &&
           selected_item_id_a == selected_item_id_b &&
           list_query_offset_a == list_query_offset_b &&
           strcmp(graph_kind_filter_a, graph_kind_filter_b) == 0 &&
           graph_edge_limit_a == graph_edge_limit_b &&
           graph_hops_a == graph_hops_b &&
           selected_project_filters_match(selected_project_keys_a,
                                          selected_project_count_a,
                                          selected_project_keys_b,
                                          selected_project_count_b);
}

static void *refresh_worker_task(void *task_ctx) {
    MemConsoleRefreshTask *task = (MemConsoleRefreshTask *)task_ctx;
    MemConsoleRefreshCompletion *completion = 0;
    MemConsoleState worker_state;
    CoreMemDb db = {0};
    CoreResult result = core_result_ok();

    if (!task) {
        return 0;
    }

    completion = (MemConsoleRefreshCompletion *)core_alloc(sizeof(*completion));
    if (!completion) {
        if (task->wake) {
            (void)core_wake_signal(task->wake);
        }
        core_free(task);
        return 0;
    }
    memset(completion, 0, sizeof(*completion));
    completion->request_id = task->request_id;
    completion->selected_item_id = task->selected_item_id;
    completion->list_query_offset = task->list_query_offset;
    (void)snprintf(completion->search_text, sizeof(completion->search_text), "%s", task->search_text);
    copy_selected_project_filters(completion->selected_project_keys,
                                  &completion->selected_project_count,
                                  task->selected_project_keys,
                                  task->selected_project_count);
    (void)snprintf(completion->graph_kind_filter,
                   sizeof(completion->graph_kind_filter),
                   "%s",
                   task->graph_kind_filter);
    completion->graph_edge_limit = task->graph_edge_limit;
    completion->graph_hops = task->graph_hops;

    seed_state(&worker_state, task->db_path);
    worker_state.selected_item_id = task->selected_item_id;
    worker_state.list_query_offset = task->list_query_offset;
    worker_state.list_scroll = 0.0f;
    (void)snprintf(worker_state.search_text, sizeof(worker_state.search_text), "%s", task->search_text);
    copy_selected_project_filters(worker_state.selected_project_keys,
                                  &worker_state.selected_project_count,
                                  task->selected_project_keys,
                                  task->selected_project_count);
    (void)snprintf(worker_state.graph_kind_filter,
                   sizeof(worker_state.graph_kind_filter),
                   "%s",
                   task->graph_kind_filter);
    mem_console_graph_edge_limit_set(&worker_state, task->graph_edge_limit);
    worker_state.graph_query_hops = mem_console_graph_hops_clamp(task->graph_hops);

    result = core_memdb_open(task->db_path, &db);
    if (result.code == CORE_OK) {
        result = refresh_state_from_db(&db, &worker_state);
    }
    (void)core_memdb_close(&db);

    completion->result.code = result.code;
    if (result.code == CORE_OK) {
        completion->result.message = "ok";
        completion->refreshed_state = worker_state;
    } else {
        const char *msg = result.message ? result.message : "background refresh failed";
        (void)snprintf(completion->error_message, sizeof(completion->error_message), "%s", msg);
        completion->result.message = completion->error_message;
    }

    if (task->wake) {
        (void)core_wake_signal(task->wake);
    }
    core_free(task);
    return completion;
}

static CoreResult schedule_refresh(MemConsoleRuntime *runtime, const MemConsoleState *state) {
    MemConsoleRefreshTask *task = 0;

    if (!runtime || !state || !state->db_path) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid background refresh request" };
    }
    if (runtime->refresh_in_flight) {
        return core_result_ok();
    }

    task = (MemConsoleRefreshTask *)core_alloc(sizeof(*task));
    if (!task) {
        return (CoreResult){ CORE_ERR_OUT_OF_MEMORY, "failed to allocate refresh task" };
    }
    memset(task, 0, sizeof(*task));

    (void)snprintf(task->db_path, sizeof(task->db_path), "%s", state->db_path);
    capture_intent_from_state(state,
                              task->search_text,
                              sizeof(task->search_text),
                              &task->selected_item_id,
                              &task->list_query_offset,
                              task->selected_project_keys,
                              &task->selected_project_count,
                              task->graph_kind_filter,
                              sizeof(task->graph_kind_filter),
                              &task->graph_edge_limit,
                              &task->graph_hops);
    task->request_id = runtime->next_request_id++;
    task->wake = &runtime->wake;

    if (!core_workers_submit(&runtime->workers, refresh_worker_task, task)) {
        core_free(task);
        return (CoreResult){ CORE_ERR_IO, "failed to submit refresh task" };
    }

    runtime->refresh_in_flight = 1;
    runtime->stats_refresh_submitted += 1u;
    runtime->in_flight_request_id = task->request_id;
    (void)snprintf(runtime->in_flight_search_text,
                   sizeof(runtime->in_flight_search_text),
                   "%s",
                   task->search_text);
    runtime->in_flight_selected_item_id = task->selected_item_id;
    runtime->in_flight_list_query_offset = task->list_query_offset;
    copy_selected_project_filters(runtime->in_flight_selected_project_keys,
                                  &runtime->in_flight_selected_project_count,
                                  task->selected_project_keys,
                                  task->selected_project_count);
    (void)snprintf(runtime->in_flight_graph_kind_filter,
                   sizeof(runtime->in_flight_graph_kind_filter),
                   "%s",
                   task->graph_kind_filter);
    runtime->in_flight_graph_edge_limit = task->graph_edge_limit;
    runtime->in_flight_graph_hops = task->graph_hops;
    return core_result_ok();
}

CoreResult mem_console_runtime_init(MemConsoleRuntime *runtime, uint64_t now_ms) {
    if (!runtime) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "invalid runtime init" };
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->poll_interval_ms = MEM_CONSOLE_RUNTIME_DEFAULT_POLL_MS;
    runtime->next_request_id = 1u;
    runtime->next_poll_due_ms = now_ms + runtime->poll_interval_ms;

    if (!core_queue_mutex_init(&runtime->completion_queue,
                               runtime->completion_slots,
                               MEM_CONSOLE_RUNTIME_COMPLETION_CAPACITY)) {
        return (CoreResult){ CORE_ERR_IO, "completion queue init failed" };
    }
    runtime->queue_initialized = 1;

    if (!core_wake_init_cond(&runtime->wake)) {
        core_queue_mutex_destroy(&runtime->completion_queue);
        runtime->queue_initialized = 0;
        return (CoreResult){ CORE_ERR_IO, "wake init failed" };
    }
    runtime->wake_initialized = 1;

    if (!core_workers_init(&runtime->workers,
                           runtime->worker_threads,
                           MEM_CONSOLE_RUNTIME_WORKER_THREADS,
                           runtime->worker_tasks,
                           MEM_CONSOLE_RUNTIME_TASK_CAPACITY,
                           &runtime->completion_queue)) {
        core_wake_shutdown(&runtime->wake);
        runtime->wake_initialized = 0;
        core_queue_mutex_destroy(&runtime->completion_queue);
        runtime->queue_initialized = 0;
        return (CoreResult){ CORE_ERR_IO, "worker init failed" };
    }

    return core_result_ok();
}

void mem_console_runtime_shutdown(MemConsoleRuntime *runtime) {
    void *msg = 0;

    if (!runtime) {
        return;
    }

    if (runtime->workers.initialized) {
        core_workers_shutdown_with_mode(&runtime->workers, CORE_WORKERS_SHUTDOWN_CANCEL);
    }
    if (runtime->queue_initialized) {
        while (core_queue_mutex_pop(&runtime->completion_queue, &msg)) {
            core_free(msg);
        }
    }
    if (runtime->wake_initialized) {
        core_wake_shutdown(&runtime->wake);
    }
    if (runtime->queue_initialized) {
        core_queue_mutex_destroy(&runtime->completion_queue);
    }
    memset(runtime, 0, sizeof(*runtime));
}

void mem_console_runtime_note_local_write(MemConsoleRuntime *runtime, uint64_t now_ms) {
    if (!runtime) {
        return;
    }

    runtime->next_poll_due_ms = now_ms;
    (void)core_wake_signal(&runtime->wake);
}

void mem_console_runtime_tick(MemConsoleRuntime *runtime,
                              MemConsoleState *state,
                              uint64_t now_ms) {
    void *msg = 0;
    char current_search_text[256];
    int64_t current_selected_item_id = 0;
    int current_list_query_offset = 0;
    char current_selected_project_keys[MEM_CONSOLE_SCOPE_FILTER_LIMIT][64];
    int current_selected_project_count = 0;
    char current_graph_kind_filter[32];
    int current_graph_edge_limit = MEM_CONSOLE_GRAPH_EDGE_LIMIT;
    int current_graph_hops = MEM_CONSOLE_GRAPH_HOPS_MIN;
    int can_schedule = 0;

    if (!runtime || !state) {
        return;
    }
    if (!runtime->queue_initialized || !runtime->wake_initialized || !runtime->workers.initialized) {
        publish_runtime_metrics(runtime, state);
        return;
    }

    publish_runtime_metrics(runtime, state);

    capture_intent_from_state(state,
                              current_search_text,
                              sizeof(current_search_text),
                              &current_selected_item_id,
                              &current_list_query_offset,
                              current_selected_project_keys,
                              &current_selected_project_count,
                              current_graph_kind_filter,
                              sizeof(current_graph_kind_filter),
                              &current_graph_edge_limit,
                              &current_graph_hops);

    can_schedule = !state->title_edit_mode &&
                   !state->body_edit_mode &&
                   !state->search_refresh_pending;

    if (runtime->refresh_in_flight &&
        !intent_matches(current_search_text,
                        current_selected_item_id,
                        current_list_query_offset,
                        current_selected_project_keys,
                        current_selected_project_count,
                        current_graph_kind_filter,
                        current_graph_edge_limit,
                        current_graph_hops,
                        runtime->in_flight_search_text,
                        runtime->in_flight_selected_item_id,
                        runtime->in_flight_list_query_offset,
                        runtime->in_flight_selected_project_keys,
                        runtime->in_flight_selected_project_count,
                        runtime->in_flight_graph_kind_filter,
                        runtime->in_flight_graph_edge_limit,
                        runtime->in_flight_graph_hops)) {
        if (!runtime->pending_intent_valid ||
            !intent_matches(current_search_text,
                            current_selected_item_id,
                            current_list_query_offset,
                            current_selected_project_keys,
                            current_selected_project_count,
                            current_graph_kind_filter,
                            current_graph_edge_limit,
                            current_graph_hops,
                            runtime->pending_search_text,
                            runtime->pending_selected_item_id,
                            runtime->pending_list_query_offset,
                            runtime->pending_selected_project_keys,
                            runtime->pending_selected_project_count,
                            runtime->pending_graph_kind_filter,
                            runtime->pending_graph_edge_limit,
                            runtime->pending_graph_hops)) {
            runtime->stats_refresh_coalesced += 1u;
            runtime->pending_intent_valid = 1;
            (void)snprintf(runtime->pending_search_text,
                           sizeof(runtime->pending_search_text),
                           "%s",
                           current_search_text);
            runtime->pending_selected_item_id = current_selected_item_id;
            runtime->pending_list_query_offset = current_list_query_offset;
            copy_selected_project_filters(runtime->pending_selected_project_keys,
                                          &runtime->pending_selected_project_count,
                                          current_selected_project_keys,
                                          current_selected_project_count);
            (void)snprintf(runtime->pending_graph_kind_filter,
                           sizeof(runtime->pending_graph_kind_filter),
                           "%s",
                           current_graph_kind_filter);
            runtime->pending_graph_edge_limit = current_graph_edge_limit;
            runtime->pending_graph_hops = current_graph_hops;
        }
    }

    if (!runtime->refresh_in_flight &&
        can_schedule &&
        now_ms >= runtime->next_poll_due_ms) {
        CoreResult result = schedule_refresh(runtime, state);
        if (result.code == CORE_OK) {
            runtime->next_poll_due_ms = now_ms + runtime->poll_interval_ms;
        } else {
            const char *msg_text = result.message ? result.message : "background refresh scheduling failed";
            (void)snprintf(state->status_line,
                           sizeof(state->status_line),
                           "Async refresh: %s",
                           msg_text);
            runtime->next_poll_due_ms = now_ms + runtime->poll_interval_ms;
        }
    }

    (void)core_wake_wait(&runtime->wake, 0u);

    while (core_queue_mutex_pop(&runtime->completion_queue, &msg)) {
        MemConsoleRefreshCompletion *completion = (MemConsoleRefreshCompletion *)msg;
        runtime->refresh_in_flight = 0;

        if (!completion) {
            continue;
        }
        if (completion->request_id < runtime->last_applied_request_id ||
            completion->request_id < runtime->in_flight_request_id) {
            runtime->stats_refresh_dropped_stale += 1u;
            core_free(completion);
            continue;
        }
        if (completion->selected_item_id != state->selected_item_id ||
            completion->list_query_offset != state->list_query_offset ||
            strcmp(completion->search_text, state->search_text) != 0 ||
            strcmp(completion->graph_kind_filter, state->graph_kind_filter) != 0 ||
            completion->graph_edge_limit != state->graph_query_edge_limit ||
            completion->graph_hops != state->graph_query_hops ||
            !selected_project_filters_match(completion->selected_project_keys,
                                            completion->selected_project_count,
                                            state->selected_project_keys,
                                            state->selected_project_count)) {
            runtime->stats_refresh_dropped_mismatch += 1u;
            core_free(completion);
            continue;
        }
        if (state->title_edit_mode || state->body_edit_mode || state->search_refresh_pending) {
            runtime->stats_refresh_dropped_editing += 1u;
            core_free(completion);
            continue;
        }

        runtime->last_applied_request_id = completion->request_id;
        if (completion->result.code != CORE_OK) {
            const char *error_text = completion->result.message ? completion->result.message : "background refresh failed";
            (void)snprintf(state->status_line,
                           sizeof(state->status_line),
                           "Async refresh failed: %s",
                           error_text);
            runtime->stats_refresh_errors += 1u;
            core_free(completion);
            continue;
        }

        apply_refreshed_state(state, &completion->refreshed_state);
        sync_edit_buffers_from_selection(state);
        runtime->stats_refresh_applied += 1u;
        core_free(completion);
    }

    if (!runtime->refresh_in_flight &&
        runtime->pending_intent_valid &&
        can_schedule) {
        MemConsoleState pending_state = *state;
        (void)snprintf(pending_state.search_text,
                       sizeof(pending_state.search_text),
                       "%s",
                       runtime->pending_search_text);
        pending_state.selected_item_id = runtime->pending_selected_item_id;
        pending_state.list_query_offset = runtime->pending_list_query_offset;
        copy_selected_project_filters(pending_state.selected_project_keys,
                                      &pending_state.selected_project_count,
                                      runtime->pending_selected_project_keys,
                                      runtime->pending_selected_project_count);
        (void)snprintf(pending_state.graph_kind_filter,
                       sizeof(pending_state.graph_kind_filter),
                       "%s",
                       runtime->pending_graph_kind_filter);
        mem_console_graph_edge_limit_set(&pending_state, runtime->pending_graph_edge_limit);
        pending_state.graph_query_hops = mem_console_graph_hops_clamp(runtime->pending_graph_hops);

        if (intent_matches(pending_state.search_text,
                           pending_state.selected_item_id,
                           pending_state.list_query_offset,
                           pending_state.selected_project_keys,
                           pending_state.selected_project_count,
                           pending_state.graph_kind_filter,
                           pending_state.graph_query_edge_limit,
                           pending_state.graph_query_hops,
                           state->search_text,
                           state->selected_item_id,
                           state->list_query_offset,
                           state->selected_project_keys,
                           state->selected_project_count,
                           state->graph_kind_filter,
                           state->graph_query_edge_limit,
                           state->graph_query_hops)) {
            CoreResult result = schedule_refresh(runtime, &pending_state);
            if (result.code == CORE_OK) {
                runtime->pending_intent_valid = 0;
                runtime->next_poll_due_ms = now_ms + runtime->poll_interval_ms;
            }
        } else {
            runtime->pending_intent_valid = 0;
        }
    }

    publish_runtime_metrics(runtime, state);
}

uint32_t mem_console_runtime_idle_wait_ms(const MemConsoleRuntime *runtime,
                                          const MemConsoleState *state,
                                          uint64_t now_ms) {
    uint64_t remaining_ms = 0;

    if (!state) {
        return 0u;
    }

    if (state->search_refresh_pending) {
        uint64_t elapsed_ms = 0;
        if (now_ms > state->search_last_input_ms) {
            elapsed_ms = now_ms - state->search_last_input_ms;
        }
        if (elapsed_ms >= 150u) {
            return 0u;
        }
        remaining_ms = 150u - elapsed_ms;
        if (remaining_ms > MEM_CONSOLE_RUNTIME_IDLE_MAX_WAIT_MS) {
            remaining_ms = MEM_CONSOLE_RUNTIME_IDLE_MAX_WAIT_MS;
        }
        return (uint32_t)remaining_ms;
    }

    if (!runtime ||
        !runtime->workers.initialized ||
        !runtime->queue_initialized ||
        !runtime->wake_initialized) {
        return MEM_CONSOLE_RUNTIME_IDLE_ACTIVE_WAIT_MS;
    }

    if (runtime->refresh_in_flight) {
        return MEM_CONSOLE_RUNTIME_IDLE_ACTIVE_WAIT_MS;
    }

    if (now_ms >= runtime->next_poll_due_ms) {
        return 0u;
    }

    remaining_ms = runtime->next_poll_due_ms - now_ms;
    if (remaining_ms > MEM_CONSOLE_RUNTIME_IDLE_MAX_WAIT_MS) {
        remaining_ms = MEM_CONSOLE_RUNTIME_IDLE_MAX_WAIT_MS;
    }
    if (remaining_ms == 0u) {
        return 0u;
    }
    return (uint32_t)remaining_ms;
}
