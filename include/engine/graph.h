#pragma once

#include "engine/buffer_pool.h"
#include <stdint.h>

struct EngineGraph;
typedef struct EngineGraph EngineGraph;

typedef struct EngineGraphSourceOps {
    void (*render)(void* userdata, float* interleaved_out, int frames, uint64_t transport_frame);
    void (*reset)(void* userdata, int sample_rate, int channels);
} EngineGraphSourceOps;

typedef struct EngineGraphSourceEntry {
    const EngineGraphSourceOps* ops;
    void* userdata;
    float gain;
    int track_index;
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
