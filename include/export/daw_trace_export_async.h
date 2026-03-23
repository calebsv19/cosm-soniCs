#pragma once

#include "export/daw_trace_export.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Captures async trace-export worker diagnostics.
typedef struct {
    uint64_t submitted;
    uint64_t completed;
    uint64_t rejected;
    uint64_t failed;
} DawTraceExportAsyncStats;

// Initializes optional async trace-export worker lane.
bool daw_trace_export_async_init(void);
// Shuts down async trace-export worker lane.
void daw_trace_export_async_shutdown(void);
// Enqueues one async diagnostics trace export request.
bool daw_trace_export_async_enqueue(const char* pack_path, const DawTraceDiagnostics* diagnostics);
// Drains up to max_items completions and returns processed completion count.
size_t daw_trace_export_async_drain(size_t max_items);
// Copies async trace-export stats.
void daw_trace_export_async_snapshot(DawTraceExportAsyncStats* out);

