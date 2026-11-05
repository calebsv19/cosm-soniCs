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

// All APIs are serialized on the control owner, never the audio callback.
// Initializes the lane and resets its aggregate accounting.
bool daw_trace_export_async_init(void);
// Drains and joins accepted exports, accounts their outcomes, then releases storage.
// Active file export has no hard stop deadline; stats survive shutdown until init.
void daw_trace_export_async_shutdown(void);
// Copies one request; at most 64 queued/running/unconsumed outcomes are admitted.
// The 32-pending pool can also refuse; false never changes earlier accepted work.
// External destination collision and artifact durability retain exporter semantics.
bool daw_trace_export_async_enqueue(const char* pack_path, const DawTraceDiagnostics* diagnostics);
// Drains up to max_items completions and returns processed completion count.
size_t daw_trace_export_async_drain(size_t max_items);
// Copies async trace-export stats.
void daw_trace_export_async_snapshot(DawTraceExportAsyncStats* out);

