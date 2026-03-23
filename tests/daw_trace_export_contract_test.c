#include "export/daw_trace_export.h"

#include "core_trace.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char* msg) {
    fprintf(stderr, "daw_trace_export_contract_test: %s\n", msg);
    exit(1);
}

static void expect(bool cond, const char* msg) {
    if (!cond) {
        fail(msg);
    }
}

static bool lane_present(const CoreTraceSession* session, const char* lane) {
    size_t count = core_trace_sample_count(session);
    const CoreTraceSampleF32* samples = core_trace_samples(session);
    size_t i;
    if (!samples) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (strcmp(samples[i].lane, lane) == 0) {
            return true;
        }
    }
    return false;
}

static bool marker_present(const CoreTraceSession* session, const char* lane, const char* label) {
    size_t count = core_trace_marker_count(session);
    const CoreTraceMarker* markers = core_trace_markers(session);
    size_t i;
    if (!markers) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (strcmp(markers[i].lane, lane) == 0 && strcmp(markers[i].label, label) == 0) {
            return true;
        }
    }
    return false;
}

int main(void) {
    const char* path = "/tmp/daw_trace_export_contract_test.pack";
    DawTraceDiagnostics diagnostics = {0};
    CoreTraceSession loaded = {0};
    CoreResult r;

    diagnostics.frame_dt_seconds = 0.016f;
    diagnostics.transport_frame = 960u;
    diagnostics.sched_block_size = 512u;
    diagnostics.sample_rate = 48000u;
    diagnostics.tempo_event_count = 3u;
    diagnostics.time_signature_event_count = 2u;
    diagnostics.loop_enabled = true;
    diagnostics.loop_start_frame = 256u;
    diagnostics.loop_end_frame = 2048u;

    expect(daw_trace_export_diagnostics(path, &diagnostics), "daw_trace_export_diagnostics failed");

    r = core_trace_import_pack(path, &loaded);
    expect(r.code == CORE_OK, "core_trace_import_pack failed");

    expect(core_trace_sample_count(&loaded) == 9u, "unexpected sample count");
    expect(core_trace_marker_count(&loaded) == 2u, "unexpected marker count");

    expect(lane_present(&loaded, "frame_dt"), "missing frame_dt lane");
    expect(lane_present(&loaded, "transport_frame"), "missing transport_frame lane");
    expect(lane_present(&loaded, "sched_block_size"), "missing sched_block_size lane");
    expect(lane_present(&loaded, "sample_rate"), "missing sample_rate lane");
    expect(lane_present(&loaded, "tempo_event_count"), "missing tempo_event_count lane");
    expect(lane_present(&loaded, "time_signature_event_count"), "missing time_signature_event_count lane");
    expect(lane_present(&loaded, "loop_enabled"), "missing loop_enabled lane");
    expect(lane_present(&loaded, "loop_start_frame"), "missing loop_start_frame lane");
    expect(lane_present(&loaded, "loop_end_frame"), "missing loop_end_frame lane");

    expect(marker_present(&loaded, "events", "trace_start"), "missing trace_start marker");
    expect(marker_present(&loaded, "events", "trace_end"), "missing trace_end marker");

    core_trace_session_reset(&loaded);
    remove(path);
    puts("daw_trace_export_contract_test: success");
    return 0;
}

