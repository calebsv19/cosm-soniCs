#include "engine/graph.h"

#include <stdlib.h>
#include <string.h>

struct EngineGraph {
    int sample_rate;
    int channels;
    int max_block;
    EngineBufferPool pool;
    EngineGraphSourceFn source_fn;
    void* source_userdata;
};

EngineGraph* engine_graph_create(int sample_rate, int channels, int max_block) {
    if (sample_rate <= 0 || channels <= 0 || max_block <= 0) {
        return NULL;
    }
    EngineGraph* graph = (EngineGraph*)calloc(1, sizeof(EngineGraph));
    if (!graph) {
        return NULL;
    }
    graph->sample_rate = sample_rate;
    graph->channels = channels;
    graph->max_block = max_block;

    if (!engine_buffer_pool_init(&graph->pool, channels, max_block)) {
        free(graph);
        return NULL;
    }
    graph->source_fn = NULL;
    graph->source_userdata = NULL;
    return graph;
}

void engine_graph_destroy(EngineGraph* graph) {
    if (!graph) {
        return;
    }
    engine_buffer_pool_free(&graph->pool);
    free(graph);
}

int engine_graph_configure(EngineGraph* graph, int sample_rate, int channels, int max_block) {
    if (!graph || sample_rate <= 0 || channels <= 0 || max_block <= 0) {
        return -1;
    }
    if (graph->sample_rate == sample_rate && graph->channels == channels && graph->max_block == max_block) {
        return 0;
    }
    engine_buffer_pool_free(&graph->pool);
    if (!engine_buffer_pool_init(&graph->pool, channels, max_block)) {
        return -1;
    }
    graph->sample_rate = sample_rate;
    graph->channels = channels;
    graph->max_block = max_block;
    return 0;
}

void engine_graph_set_source(EngineGraph* graph, EngineGraphSourceFn fn, void* userdata) {
    if (!graph) {
        return;
    }
    graph->source_fn = fn;
    graph->source_userdata = userdata;
}

void engine_graph_render(EngineGraph* graph, float* interleaved_out, int frames) {
    if (!graph || !interleaved_out || frames <= 0) {
        return;
    }
    if (graph->source_fn) {
        graph->source_fn(graph->source_userdata, interleaved_out, frames);
    } else {
        memset(interleaved_out, 0, (size_t)frames * (size_t)graph->channels * sizeof(float));
    }
}

EngineBufferPool* engine_graph_get_pool(EngineGraph* graph) {
    if (!graph) {
        return NULL;
    }
    return &graph->pool;
}

int engine_graph_get_channels(const EngineGraph* graph) {
    return graph ? graph->channels : 0;
}

int engine_graph_get_sample_rate(const EngineGraph* graph) {
    return graph ? graph->sample_rate : 0;
}

int engine_graph_get_max_block(const EngineGraph* graph) {
    return graph ? graph->max_block : 0;
}
