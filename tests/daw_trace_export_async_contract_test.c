#include "export/daw_trace_export_async.h"

#include "core_trace.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fail(const char* msg) {
    fprintf(stderr, "daw_trace_export_async_contract_test: %s\n", msg);
    exit(1);
}

static void expect(bool cond, const char* msg) {
    if (!cond) {
        fail(msg);
    }
}

static bool marker_present(const CoreTraceSession* session, const char* lane, const char* label) {
    const CoreTraceMarker* markers = core_trace_markers(session);
    size_t count = core_trace_marker_count(session);
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
    const char* path = "/tmp/daw_trace_export_async_contract_test.pack";
    DawTraceDiagnostics diagnostics = {0};
    DawTraceExportAsyncStats stats = {0};
    CoreTraceSession loaded = {0};
    CoreResult r;
    struct timespec ts = {0};
    int i;
    bool done = false;

    diagnostics.frame_dt_seconds = 0.016f;
    diagnostics.transport_frame = 1200u;
    diagnostics.sched_block_size = 256u;
    diagnostics.sample_rate = 48000u;
    diagnostics.tempo_event_count = 2u;
    diagnostics.time_signature_event_count = 1u;
    diagnostics.loop_enabled = false;
    diagnostics.loop_start_frame = 0u;
    diagnostics.loop_end_frame = 0u;

    expect(daw_trace_export_async_init(), "daw_trace_export_async_init failed");
    expect(daw_trace_export_async_enqueue(path, &diagnostics), "daw_trace_export_async_enqueue failed");

    ts.tv_sec = 0;
    ts.tv_nsec = 10 * 1000 * 1000;
    for (i = 0; i < 300; ++i) {
        if (daw_trace_export_async_drain(8u) > 0u) {
            done = true;
            break;
        }
        nanosleep(&ts, NULL);
    }
    expect(done, "timed out waiting for async completion");

    daw_trace_export_async_snapshot(&stats);
    expect(stats.submitted == 1u, "unexpected submitted count");
    expect(stats.completed == 1u, "unexpected completed count");
    expect(stats.failed == 0u, "unexpected failed count");

    r = core_trace_import_pack(path, &loaded);
    expect(r.code == CORE_OK, "core_trace_import_pack failed");
    expect(core_trace_sample_count(&loaded) == 9u, "unexpected sample count");
    expect(core_trace_marker_count(&loaded) == 2u, "unexpected marker count");
    expect(marker_present(&loaded, "events", "trace_start"), "missing trace_start marker");
    expect(marker_present(&loaded, "events", "trace_end"), "missing trace_end marker");

    core_trace_session_reset(&loaded);
    daw_trace_export_async_shutdown();
    remove(path);
    puts("daw_trace_export_async_contract_test: success");
    return 0;
}

