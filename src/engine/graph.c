#include "engine/graph.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct EngineGraph {
    int sample_rate;
    int channels;
    int max_block;
    EngineBufferPool pool;
    EngineGraphSourceEntry* sources;
    int source_count;
    int source_capacity;
    float* mix_buffer;
    size_t mix_capacity;
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
    graph->sources = NULL;
    graph->source_count = 0;
    graph->source_capacity = 0;
    graph->mix_buffer = NULL;
    graph->mix_capacity = 0;
    return graph;
}

void engine_graph_destroy(EngineGraph* graph) {
    if (!graph) {
        return;
    }
    engine_buffer_pool_free(&graph->pool);
    free(graph->sources);
    free(graph->mix_buffer);
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
    engine_graph_reset(graph);
    return 0;
}

void engine_graph_clear_sources(EngineGraph* graph) {
    if (!graph) {
        return;
    }
    graph->source_count = 0;
}

bool engine_graph_add_source(EngineGraph* graph, const EngineGraphSourceOps* ops, void* userdata, float gain) {
    if (!graph || !ops || !ops->render) {
        return false;
    }
    if (graph->source_count == graph->source_capacity) {
        int new_cap = graph->source_capacity == 0 ? 4 : graph->source_capacity * 2;
        EngineGraphSourceEntry* new_entries = (EngineGraphSourceEntry*)realloc(graph->sources, sizeof(EngineGraphSourceEntry) * (size_t)new_cap);
        if (!new_entries) {
            return false;
        }
        graph->sources = new_entries;
        graph->source_capacity = new_cap;
    }
    graph->sources[graph->source_count++] = (EngineGraphSourceEntry){
        .ops = ops,
        .userdata = userdata,
        .gain = gain,
    };
    if (ops->reset) {
        ops->reset(userdata, graph->sample_rate, graph->channels);
    }
    return true;
}

static bool ensure_mix_capacity(EngineGraph* graph, size_t samples) {
    if (graph->mix_capacity >= samples) {
        return true;
    }
    float* new_buf = (float*)realloc(graph->mix_buffer, samples * sizeof(float));
    if (!new_buf) {
        return false;
    }
    graph->mix_buffer = new_buf;
    graph->mix_capacity = samples;
    return true;
}

void engine_graph_render(EngineGraph* graph, float* interleaved_out, int frames, uint64_t transport_frame) {
    if (!graph || !interleaved_out || frames <= 0) {
        return;
    }
    size_t total_samples = (size_t)frames * (size_t)graph->channels;
    memset(interleaved_out, 0, total_samples * sizeof(float));

    if (graph->source_count == 0) {
        return;
    }

    if (!ensure_mix_capacity(graph, total_samples)) {
        return;
    }

    for (int i = 0; i < graph->source_count; ++i) {
        EngineGraphSourceEntry* entry = &graph->sources[i];
        memset(graph->mix_buffer, 0, total_samples * sizeof(float));
        entry->ops->render(entry->userdata, graph->mix_buffer, frames, transport_frame);
        float gain = entry->gain;
        if (gain == 0.0f) {
            continue;
        }
        for (size_t s = 0; s < total_samples; ++s) {
            interleaved_out[s] += graph->mix_buffer[s] * gain;
        }
    }
}

void engine_graph_reset(EngineGraph* graph) {
    if (!graph) {
        return;
    }
    for (int i = 0; i < graph->source_count; ++i) {
        EngineGraphSourceEntry* entry = &graph->sources[i];
        if (entry->ops && entry->ops->reset) {
            entry->ops->reset(entry->userdata, graph->sample_rate, graph->channels);
        }
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
