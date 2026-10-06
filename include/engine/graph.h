#pragma once

#include "engine/buffer_pool.h"
#include "effects/sample_ramp.h"
#include <stdint.h>

struct EngineGraph;
typedef struct EngineGraph EngineGraph;

// Supplies rendering and reset callbacks for a borrowed source.
typedef struct EngineGraphSourceOps {
    void (*render)(void* userdata, float* interleaved_out, int frames, uint64_t transport_frame);
    void (*reset)(void* userdata, int sample_rate, int channels);
} EngineGraphSourceOps;

// Stores one source with stable ordering, optional silent bounds, and its live gain ramp.
typedef struct EngineGraphSourceEntry {
    const EngineGraphSourceOps* ops;
    void* userdata;
    float gain;
    float clip_gain; // Authored multiplier retained for lightweight track gain changes.
    FxSampleRamp gain_ramp;
    uint64_t identity;
    bool identified;
    int track_index;
    int next_in_track;
    bool bounded;
    uint64_t start_frame, length_frames;
} EngineGraphSourceEntry;

EngineGraph* engine_graph_create(int sample_rate, int channels, int max_block);
void engine_graph_destroy(EngineGraph* graph);
int  engine_graph_configure(EngineGraph* graph, int sample_rate, int channels, int max_block);
void engine_graph_clear_sources(EngineGraph* graph);
bool engine_graph_add_source(EngineGraph* graph, const EngineGraphSourceOps* ops, void* userdata, float gain, int track_index);
void engine_graph_render(EngineGraph* graph, float* interleaved_out, int frames, uint64_t transport_frame);
void engine_graph_render_track(EngineGraph* graph, float* interleaved_out, int frames, uint64_t transport_frame, int track_index);
void engine_graph_reset(EngineGraph* graph);
EngineBufferPool* engine_graph_get_pool(EngineGraph* graph);
int engine_graph_get_channels(const EngineGraph* graph);
int engine_graph_get_sample_rate(const EngineGraph* graph);
int engine_graph_get_max_block(const EngineGraph* graph);

// Adds a source whose stable clip identity allows live gain ramps to survive prepared revisions.
bool engine_graph_add_source_identified(EngineGraph* graph, const EngineGraphSourceOps* ops,
                                       void* userdata, float gain, int track, uint64_t identity);
// Transfers compatible audible gains at a worker boundary and ramps changed targets over 5 ms.
void engine_graph_transfer_gains(EngineGraph* next, const EngineGraph* old, const int* old_tracks, int count);

// Snaps gain controls at an explicit discontinuity while ordinary source resets preserve live ramps.
void engine_graph_reset_control_ramps(EngineGraph* graph);

// Declares the last source stateless and silent outside its timeline span; unknown sources remain unbounded.
void engine_graph_bound_last_source(EngineGraph* graph, uint64_t start, uint64_t length);

// Records the authored clip multiplier separately from its current track gain.
void engine_graph_set_last_clip_gain(EngineGraph* graph, float gain);
// Retargets a prepared track without rebuilding its sources or touching their DSP state.
void engine_graph_set_track_gain(EngineGraph* graph, int track, float gain, uint32_t ramp_frames);

// Builds control-side identity lookup used by allocation-free revision adoption.
bool engine_graph_prepare_identity_lookup(EngineGraph* graph);
