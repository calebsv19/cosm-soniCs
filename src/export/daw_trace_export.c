#include "export/daw_trace_export.h"

#include "core_trace.h"

#include <stddef.h>

static bool emit_sample(CoreTraceSession* session, const char* lane, double t_seconds, float value) {
    return core_trace_emit_sample_f32(session, lane, t_seconds, value).code == CORE_OK;
}

bool daw_trace_export_diagnostics(const char* pack_path, const DawTraceDiagnostics* diagnostics) {
    CoreTraceSession session = {0};
    CoreTraceConfig config = {0};
    double end_time = 0.0;

    if (!pack_path || !diagnostics) {
        return false;
    }

    config.sample_capacity = 16u;
    config.marker_capacity = 8u;
    if (core_trace_session_init(&session, &config).code != CORE_OK) {
        return false;
    }

    end_time = diagnostics->frame_dt_seconds > 0.0f ? (double)diagnostics->frame_dt_seconds : 0.001;

    if (!emit_sample(&session, "frame_dt", 0.0, diagnostics->frame_dt_seconds) ||
        !emit_sample(&session, "transport_frame", 0.0, (float)diagnostics->transport_frame) ||
        !emit_sample(&session, "sched_block_size", 0.0, (float)diagnostics->sched_block_size) ||
        !emit_sample(&session, "sample_rate", 0.0, (float)diagnostics->sample_rate) ||
        !emit_sample(&session, "tempo_event_count", 0.0, (float)diagnostics->tempo_event_count) ||
        !emit_sample(&session,
                     "time_signature_event_count",
                     0.0,
                     (float)diagnostics->time_signature_event_count) ||
        !emit_sample(&session, "loop_enabled", 0.0, diagnostics->loop_enabled ? 1.0f : 0.0f) ||
        !emit_sample(&session, "loop_start_frame", 0.0, (float)diagnostics->loop_start_frame) ||
        !emit_sample(&session, "loop_end_frame", 0.0, (float)diagnostics->loop_end_frame)) {
        core_trace_session_reset(&session);
        return false;
    }

    if (core_trace_emit_marker(&session, "events", 0.0, "trace_start").code != CORE_OK ||
        core_trace_emit_marker(&session, "events", end_time, "trace_end").code != CORE_OK) {
        core_trace_session_reset(&session);
        return false;
    }

    if (core_trace_finalize(&session).code != CORE_OK ||
        core_trace_export_pack(&session, pack_path).code != CORE_OK) {
        core_trace_session_reset(&session);
        return false;
    }

    core_trace_session_reset(&session);
    return true;
}

