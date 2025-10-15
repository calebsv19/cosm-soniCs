#pragma once

#include "engine/buffer_pool.h"

struct EngineGraph;
typedef struct EngineGraph EngineGraph;

typedef void (*EngineGraphSourceFn)(void* userdata, float* interleaved_out, int frames);

EngineGraph* engine_graph_create(int sample_rate, int channels, int max_block);
void engine_graph_destroy(EngineGraph* graph);
void engine_graph_set_source(EngineGraph* graph, EngineGraphSourceFn fn, void* userdata);
void engine_graph_render(EngineGraph* graph, float* interleaved_out, int frames);
EngineBufferPool* engine_graph_get_pool(EngineGraph* graph);
int engine_graph_get_channels(const EngineGraph* graph);
int engine_graph_get_sample_rate(const EngineGraph* graph);
int engine_graph_get_max_block(const EngineGraph* graph);
int engine_graph_configure(EngineGraph* graph, int sample_rate, int channels, int max_block);
