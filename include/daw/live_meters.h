#pragma once
#include "daw/app_actions.h"
#include "engine/live_meter.h"
// Copies qualified observation/freshness and exact listening-mode admission independently of music.
typedef struct DawLiveMeterInfo {
    EngineLiveMeter meter;
    uint64_t generation, track_id, age_ns;
    bool fresh, playing, output_available, mono;
    char expected_monitor[128];
} DawLiveMeterInfo;
// Reads one current child/group/return or master window and the listening mode without changing
// DSP.
DawActionStatus daw_live_meters_get(struct AppState*, const DawActionTarget*, DawLiveMeterInfo*);
// Selects an ephemeral output-only mono fold against exact endpoint/generation/revision guards.
DawActionStatus daw_monitor_set(struct AppState*, const char* expected, bool mono);
