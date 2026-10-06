#include "engine/engine_internal.h"

// Initializes stream atomics and producer storage before any threads can access it.
void engine_analysis_init(EngineAnalysisStream* stream) {
    if (!stream) return;
    atomic_init(&stream->target.revision, 0);
    atomic_init(&stream->target.track_id, 0);
    atomic_init(&stream->target.view, 0);
    atomic_init(&stream->target.effect_id, 0);
    atomic_init(&stream->target.enabled, false);
    atomic_init(&stream->queued, 0);
    atomic_init(&stream->dropped, 0);
    atomic_init(&stream->published, 0);
    atomic_init(&stream->consumed, 0);
    atomic_init(&stream->stale, 0);
    atomic_init(&stream->backlog_high_water, 0);
    atomic_init(&stream->computed, 0);
    atomic_init(&stream->compute_ticks, 0);
    atomic_init(&stream->compute_max_ticks, 0);
    stream->capture = (EngineAnalysisPacket){0};
    stream->filled = 0;
    stream->next_sequence = stream->sample_cursor = 0;
    stream->block_seen = stream->block_appended = false;
}

// Takes one bounded coherent read, rejecting an in-progress control update.
bool engine_analysis_selection(const EngineAnalysisStream* stream, EngineAnalysisSelection* selection) {
    if (!stream || !selection) return false;
    uint64_t revision = atomic_load(&stream->target.revision);
    if (revision & 1u) return false;
    *selection = (EngineAnalysisSelection){.revision = revision,
        .track_id = atomic_load(&stream->target.track_id), .view = atomic_load(&stream->target.view),
        .effect_id = atomic_load(&stream->target.effect_id), .enabled = atomic_load(&stream->target.enabled)};
    return revision == atomic_load(&stream->target.revision);
}

// Publishes a complete target from the sole control writer without making the producer spin.
bool engine_analysis_select(EngineAnalysisStream* stream, uint64_t track_id, int view, FxInstId effect_id, bool enabled) {
    EngineAnalysisSelection previous;
    if (!engine_analysis_selection(stream, &previous)) return false;
    if (previous.track_id == track_id && previous.view == view && previous.effect_id == effect_id &&
        previous.enabled == enabled) return false;
    atomic_fetch_add(&stream->target.revision, 1);
    atomic_store(&stream->target.track_id, track_id);
    atomic_store(&stream->target.view, view);
    atomic_store(&stream->target.effect_id, effect_id);
    atomic_store(&stream->target.enabled, enabled);
    atomic_fetch_add(&stream->target.revision, 1);
    return true;
}

// Advances the generation while preserving target fields and all live queue indices.
void engine_analysis_invalidate(EngineAnalysisStream* stream) {
    if (stream) atomic_fetch_add(&stream->target.revision, 2);
}

// Starts a capture block using producer-owned state and a coherent target snapshot.
bool engine_analysis_begin(EngineAnalysisStream* stream) {
    if (!stream) return false;
    if (stream->block_seen && !stream->block_appended) {
        stream->filled = 0;
        stream->sample_cursor = 0;
        ++stream->next_sequence; // A selected insert did not run in the preceding render block.
    }
    stream->block_seen = true;
    stream->block_appended = false;
    EngineAnalysisSelection selection;
    if (!engine_analysis_selection(stream, &selection)) { stream->filled = 0; return false; }
    if (selection.revision != stream->capture.selection.revision || !selection.enabled) {
        stream->filled = 0; stream->sample_cursor = 0;
    }
    stream->capture.selection = selection;
    return selection.enabled;
}

// Builds contiguous windows and drops whole windows when the SPSC queue is saturated.
void engine_analysis_append(EngineAnalysisStream* stream, RingBuffer* queue, const float* input,
                            int frames, int channels, int window_frames) {
    if (!stream || !queue || !input || frames <= 0 || channels <= 0 || window_frames <= 0 ||
        window_frames > ENGINE_SPECTRUM_FFT_SIZE || !stream->capture.selection.enabled) return;
    stream->block_appended = true;
    for (int i = 0; i < frames; ++i) {
        float sample = input[(size_t)i * channels];
        if (channels > 1) sample = 0.5f * (sample + input[(size_t)i * channels + 1]);
        if (!stream->filled) stream->capture.first_sample = stream->sample_cursor;
        ++stream->sample_cursor;
        stream->capture.samples[stream->filled++] = sample;
        if (stream->filled == window_frames) {
            stream->capture.sequence = stream->next_sequence++;
            if (ringbuf_write_exact(queue, &stream->capture, sizeof(stream->capture))) atomic_fetch_add(&stream->queued, 1);
            else atomic_fetch_add(&stream->dropped, 1);
            stream->filled = 0;
        }
    }
}

// Rejects obsolete packets before analysis and again before publishing the result.
bool engine_analysis_current(const EngineAnalysisStream* stream, const EngineAnalysisPacket* packet) {
    EngineAnalysisSelection selection;
    return packet && engine_analysis_selection(stream, &selection) && selection.enabled &&
           selection.revision == packet->selection.revision;
}

// Discards only producer-owned partial storage when a transport operation changes continuity.
bool engine_analysis_begin_at(EngineAnalysisStream* stream, uint64_t epoch) {
    if (!stream) return false;
    if (stream->capture.epoch != epoch) {
        stream->filled = 0; stream->sample_cursor = 0;
        stream->capture.epoch = epoch;
    }
    return engine_analysis_begin(stream);
}

// Receives one complete packet and records consumer-observed backlog including that packet.
bool engine_analysis_receive(EngineAnalysisStream* stream, RingBuffer* queue, EngineAnalysisPacket* packet) {
    uint64_t pending = ringbuf_available_read(queue) / sizeof(*packet);
    if (!ringbuf_read_exact(queue, packet, sizeof(*packet))) return false;
    if (pending == 0) pending = 1;
    atomic_fetch_add(&stream->consumed, 1);
    if (pending > atomic_load(&stream->backlog_high_water))
        atomic_store(&stream->backlog_high_water, pending);
    return true;
}

// Records transform duration with a single writer and no additional render-thread work.
void engine_analysis_computed(EngineAnalysisStream* stream, uint64_t began) {
    uint64_t ticks = SDL_GetPerformanceCounter() - began;
    atomic_fetch_add(&stream->compute_ticks, ticks);
    if (ticks > atomic_load(&stream->compute_max_ticks)) atomic_store(&stream->compute_max_ticks, ticks);
    atomic_fetch_add(&stream->computed, 1);
}

// Reads lifetime counters independently; pending excludes in-flight work and may vary during sampling.
bool engine_get_analysis_diagnostics(const Engine* engine, bool spectrogram, EngineAnalysisDiagnostics* out) {
    if (!engine || !out) return false;
    const EngineAnalysisStream* stream = spectrogram ? &engine->spectrogram_stream : &engine->spectrum_stream;
    *out = (EngineAnalysisDiagnostics){
        .queued = atomic_load(&stream->queued), .consumed = atomic_load(&stream->consumed),
        .published = atomic_load(&stream->published), .dropped = atomic_load(&stream->dropped),
        .stale = atomic_load(&stream->stale), .backlog_high_water = atomic_load(&stream->backlog_high_water),
        .computed = atomic_load(&stream->computed),
        .compute_total_ms = 1000.0 * atomic_load(&stream->compute_ticks) / SDL_GetPerformanceFrequency(),
        .compute_max_ms = 1000.0 * atomic_load(&stream->compute_max_ticks) / SDL_GetPerformanceFrequency()};
    const RingBuffer* queue = spectrogram ? &engine->spectrogram_queue : &engine->spectrum_queue;
    size_t tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
    size_t head = atomic_load_explicit(&queue->head, memory_order_acquire);
    size_t bytes = head - tail;
    out->pending = bytes <= queue->capacity ? bytes / sizeof(EngineAnalysisPacket) : 0;
    return true;
}
