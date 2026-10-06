#include "engine/graph.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

// Indexes each track's source chain without changing source summation order.
typedef struct EngineGraphTrack {
    int track, head, tail;
} EngineGraphTrack;

// Keys a source by track and stable identity while retaining original duplicate order.
typedef struct EngineGraphIdentity {
    uint64_t identity;
    int track, source;
} EngineGraphIdentity;

// Owns prepared source metadata and bounded render scratch for a graph revision.
struct EngineGraph {
    int sample_rate;
    int channels;
    int max_block;
    EngineBufferPool pool;
    EngineGraphSourceEntry* sources;
    int source_count;
    int source_capacity;
    EngineGraphIdentity* identities;
    int identity_count;
    EngineGraphTrack* tracks;
    int track_count, track_capacity;
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
    graph->mix_capacity = (size_t)max_block * (size_t)channels;
    graph->mix_buffer = calloc(graph->mix_capacity, sizeof(float));
    if (!graph->mix_buffer) {
        engine_buffer_pool_free(&graph->pool);
        free(graph);
        return NULL;
    }
    return graph;
}

void engine_graph_destroy(EngineGraph* graph) {
    if (!graph) {
        return;
    }
    engine_buffer_pool_free(&graph->pool);
    free(graph->identities);
    free(graph->tracks);
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
    graph->identity_count = 0;
    graph->track_count = 0;
}

// Finds a track bucket or its sorted insertion position using prepared metadata only.
static int graph_track_position(const EngineGraph* graph, int track) {
    int first = 0, last = graph->track_count;
    while (first < last) {
        int middle = first + (last - first) / 2;
        if (graph->tracks[middle].track < track) first = middle + 1;
        else last = middle;
    }
    return first;
}

// Adds source and track index storage on the control side, preserving original insertion order.
bool engine_graph_add_source(EngineGraph* graph, const EngineGraphSourceOps* ops, void* userdata, float gain, int track_index) {
    if (!graph || !ops || !ops->render) {
        return false;
    }
    int bucket = graph_track_position(graph, track_index);
    bool new_track = bucket == graph->track_count || graph->tracks[bucket].track != track_index;
    if (new_track && graph->track_count == graph->track_capacity) {
        if (graph->track_capacity > INT_MAX / 2) return false;
        int capacity = graph->track_capacity ? graph->track_capacity * 2 : 4;
        EngineGraphTrack* tracks = realloc(graph->tracks, (size_t)capacity * sizeof(*tracks));
        if (!tracks) return false;
        graph->tracks = tracks;
        graph->track_capacity = capacity;
    }
    if (graph->source_count == graph->source_capacity) {
        if (graph->source_capacity > INT_MAX / 2) return false;
        int new_cap = graph->source_capacity == 0 ? 4 : graph->source_capacity * 2;
        EngineGraphSourceEntry* new_entries = (EngineGraphSourceEntry*)realloc(graph->sources, sizeof(EngineGraphSourceEntry) * (size_t)new_cap);
        if (!new_entries) {
            return false;
        }
        graph->sources = new_entries;
        graph->source_capacity = new_cap;
    }
    if (new_track) {
        memmove(graph->tracks + bucket + 1, graph->tracks + bucket,
                (size_t)(graph->track_count - bucket) * sizeof(*graph->tracks));
        graph->tracks[bucket] = (EngineGraphTrack){track_index, -1, -1};
        ++graph->track_count;
    }
    EngineGraphTrack* chain = &graph->tracks[bucket];
    if (chain->tail >= 0) graph->sources[chain->tail].next_in_track = graph->source_count;
    else chain->head = graph->source_count;
    chain->tail = graph->source_count;
    graph->identity_count = 0;
    graph->sources[graph->source_count++] = (EngineGraphSourceEntry){
        .ops = ops,
        .userdata = userdata,
        .gain = gain,
        .clip_gain = gain,
        .track_index = track_index,
        .next_in_track = -1,
    };
    fx_sample_ramp_reset(&graph->sources[graph->source_count - 1].gain_ramp, gain);
    if (ops->reset) {
        ops->reset(userdata, graph->sample_rate, graph->channels);
    }
    return true;
}

// Marks only sources whose callbacks have no required state evolution during silent intervals.
void engine_graph_bound_last_source(EngineGraph* graph, uint64_t start, uint64_t length) {
    if (!graph || !graph->source_count) return;
    EngineGraphSourceEntry* source = &graph->sources[graph->source_count - 1];
    source->bounded = true;
    source->start_frame = start;
    source->length_frames = length;
}

// Rejects whole silent blocks while preserving legacy callbacks at uint64 timeline wrap.
static bool graph_source_silent(const EngineGraphSourceEntry* source, uint64_t start, int frames) {
    if (!source->bounded || start > UINT64_MAX - (uint64_t)(frames - 1)) return false;
    if (!source->length_frames) return true;
    if (start < source->start_frame) return source->start_frame - start >= (uint64_t)frames;
    return start - source->start_frame >= source->length_frames;
}

// Tags a prepared source with its stable creation identity without changing its render ownership.
bool engine_graph_add_source_identified(EngineGraph* graph, const EngineGraphSourceOps* ops,
                                       void* userdata, float gain, int track, uint64_t identity) {
    if (!engine_graph_add_source(graph, ops, userdata, gain, track)) return false;
    graph->sources[graph->source_count - 1].identity = identity;
    graph->sources[graph->source_count - 1].identified = true;
    return true;
}

// Orders duplicate identities by insertion index to preserve the original first-match rule.
static int graph_identity_compare(const void* a, const void* b) {
    const EngineGraphIdentity* x = a;
    const EngineGraphIdentity* y = b;
    if (x->track != y->track) return x->track < y->track ? -1 : 1;
    if (x->identity != y->identity) return x->identity < y->identity ? -1 : 1;
    return (x->source > y->source) - (x->source < y->source);
}

// Allocates and sorts lookup metadata before any worker can adopt this graph.
bool engine_graph_prepare_identity_lookup(EngineGraph* graph) {
    if (!graph) return false;
    EngineGraphIdentity* entries = graph->source_count ? malloc((size_t)graph->source_count * sizeof(*entries)) : NULL;
    if (graph->source_count && !entries) return false;
    int count = 0;
    for (int i = 0; i < graph->source_count; ++i) {
        const EngineGraphSourceEntry* entry = &graph->sources[i];
        if (entry->identified) entries[count++] = (EngineGraphIdentity){entry->identity, entry->track_index, i};
    }
    if (count > 1) qsort(entries, (size_t)count, sizeof(*entries), graph_identity_compare);
    free(graph->identities);
    graph->identities = entries;
    graph->identity_count = count;
    return true;
}

// Finds compatible history in logarithmic time for prepared graphs, with a track-local fallback.
static const EngineGraphSourceEntry* graph_gain_source(const EngineGraph* graph, int track,
                                                       const EngineGraphSourceEntry* target) {
    if (graph->identity_count) {
        int lo = 0, hi = graph->identity_count;
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            const EngineGraphIdentity* key = &graph->identities[mid];
            if (key->track < track || (key->track == track && key->identity < target->identity)) lo = mid + 1;
            else hi = mid;
        }
        for (int i = lo; i < graph->identity_count; ++i) {
            const EngineGraphIdentity* key = &graph->identities[i];
            if (key->track != track || key->identity != target->identity) break;
            const EngineGraphSourceEntry* source = &graph->sources[key->source];
            if (source->ops == target->ops) return source;
        }
    } else {
        int bucket = graph_track_position(graph, track);
        if (bucket == graph->track_count || graph->tracks[bucket].track != track) return NULL;
        for (int i = graph->tracks[bucket].head; i >= 0; i = graph->sources[i].next_in_track) {
            const EngineGraphSourceEntry* source = &graph->sources[i];
            if (source->identified && source->identity == target->identity && source->ops == target->ops) return source;
        }
    }
    return NULL;
}

// Retains an in-flight gain transition by stable source identity and surviving track mapping.
void engine_graph_transfer_gains(EngineGraph* next, const EngineGraph* old, const int* old_tracks, int count) {
    if (!next || !old || !old_tracks || next->sample_rate != old->sample_rate) return;
    uint32_t duration = (uint32_t)(next->sample_rate / 200);
    if (!duration) duration = 1;
    for (int i = 0; i < next->source_count; ++i) {
        EngineGraphSourceEntry* entry = &next->sources[i];
        if (!entry->identified || entry->track_index < 0 || entry->track_index >= count) continue;
        int previous = old_tracks[entry->track_index];
        const EngineGraphSourceEntry* source = previous < 0 ? NULL : graph_gain_source(old, previous, entry);
        if (source) {
            entry->gain_ramp = source->gain_ramp;
            fx_sample_ramp_target(&entry->gain_ramp, entry->gain, duration);
        }
    }
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

static void engine_graph_render_internal(EngineGraph* graph, float* interleaved_out, int frames, uint64_t transport_frame, int track_filter) {
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

    int first = 0;
    if (track_filter >= 0) {
        int bucket = graph_track_position(graph, track_filter);
        if (bucket == graph->track_count || graph->tracks[bucket].track != track_filter) return;
        first = graph->tracks[bucket].head;
    }
    for (int i = first; i >= 0 && i < graph->source_count;
         i = track_filter >= 0 ? graph->sources[i].next_in_track : i + 1) {
        EngineGraphSourceEntry* entry = &graph->sources[i];
        if (entry->gain_ramp.current == 0 && !entry->gain_ramp.remaining) continue;
        if (graph_source_silent(entry, transport_frame, frames)) {
            // Retain exact repeated-add ramp arithmetic even when the sampler is skipped.
            for (int n = 0; n < frames && entry->gain_ramp.remaining; ++n)
                (void)fx_sample_ramp_next(&entry->gain_ramp);
            continue;
        }
        memset(graph->mix_buffer, 0, total_samples * sizeof(float));
        entry->ops->render(entry->userdata, graph->mix_buffer, frames, transport_frame);
        for (int frame = 0; frame < frames; ++frame) {
            float gain = fx_sample_ramp_next(&entry->gain_ramp);
            for (int ch = 0; ch < graph->channels; ++ch) {
                size_t sample = (size_t)frame * graph->channels + ch;
                interleaved_out[sample] += graph->mix_buffer[sample] * gain;
            }
        }
    }
}

void engine_graph_render(EngineGraph* graph, float* interleaved_out, int frames, uint64_t transport_frame) {
    engine_graph_render_internal(graph, interleaved_out, frames, transport_frame, -1);
}

void engine_graph_render_track(EngineGraph* graph, float* interleaved_out, int frames, uint64_t transport_frame, int track_index) {
    engine_graph_render_internal(graph, interleaved_out, frames, transport_frame, track_index);
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

// Snaps authored gain targets only at explicit transport discontinuities, not ordinary loop wraps.
void engine_graph_reset_control_ramps(EngineGraph* graph) {
    if (!graph) return;
    for (int i = 0; i < graph->source_count; ++i)
        fx_sample_ramp_reset(&graph->sources[i].gain_ramp, graph->sources[i].gain);
}

// Records immutable clip gain during control-side graph preparation.
void engine_graph_set_last_clip_gain(EngineGraph* graph, float gain) {
    if (graph && graph->source_count) graph->sources[graph->source_count - 1].clip_gain = gain;
}

// Updates only gain targets along the prepared track-local chain.
void engine_graph_set_track_gain(EngineGraph* graph, int track, float gain, uint32_t ramp_frames) {
    if (!graph) return;
    int bucket = graph_track_position(graph, track);
    if (bucket == graph->track_count || graph->tracks[bucket].track != track) return;
    for (int i = graph->tracks[bucket].head; i >= 0; i = graph->sources[i].next_in_track) {
        EngineGraphSourceEntry* entry = &graph->sources[i];
        entry->gain = gain * entry->clip_gain;
        if (ramp_frames) fx_sample_ramp_target(&entry->gain_ramp, entry->gain, ramp_frames);
        else fx_sample_ramp_reset(&entry->gain_ramp, entry->gain);
    }
}
