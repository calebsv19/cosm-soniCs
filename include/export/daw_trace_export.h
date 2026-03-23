#pragma once

#include <stdbool.h>
#include <stdint.h>

// Carries a compact DAW diagnostics snapshot for core_trace export.
typedef struct {
    float frame_dt_seconds;
    uint64_t transport_frame;
    uint32_t sched_block_size;
    uint32_t sample_rate;
    uint32_t tempo_event_count;
    uint32_t time_signature_event_count;
    bool loop_enabled;
    uint64_t loop_start_frame;
    uint64_t loop_end_frame;
} DawTraceDiagnostics;

// Exports a deterministic DAW diagnostics trace pack via shared core_trace.
bool daw_trace_export_diagnostics(const char* pack_path, const DawTraceDiagnostics* diagnostics);

