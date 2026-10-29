#pragma once
#include "daw/composition.h"
#include "daw/mix_capture.h"
#define DAW_AB_BYTES UINT64_C(67108864)
// Owns two exact immutable passage captures and listening-only matched/original gains.
typedef struct DawAbComparison {
    uint64_t generation, revision;
    DawCompositionRender captures[2];
    DawMixSnapshot* provenance[2];
    double gains[4];
    bool ready, match_available;
    int selection;
} DawAbComparison;
// Reads the current ephemeral comparison revision without allocating or touching authored history.
uint64_t daw_ab_revision(const struct AppState*);
// Replaces one capture only after exact revision/configuration validation and complete private
// rendering.
DawActionStatus daw_ab_capture(struct AppState*, uint64_t revision, const DawMixSnapshot*, int side,
                               const DawCompositionRequest*);
// Computes symmetric attenuation-only RMS matching and shared sample-headroom reduction.
bool daw_ab_gains(const EngineBounceBuffer*, const EngineBounceBuffer*, double gains[4],
                  bool* match_available);
// Starts or selects immutable aligned audio without restarting an active owned comparison.
DawActionStatus daw_ab_listen(struct AppState*, uint64_t revision, int side, bool matched,
                              bool restart);
// Stops only this comparison and leaves unrelated audition owners intact.
DawActionStatus daw_ab_stop(struct AppState*, uint64_t revision);
// Releases ephemeral captures at explicit clear, project replacement or teardown.
void daw_ab_release(struct AppState*);
