#include "engine/engine_internal.h"
#include "core_time.h"
#include <stdio.h>

// Maps an intro followed by a half-open loop without overflowing repeated wrap arithmetic.
uint64_t engine_clock_advance(uint64_t origin, uint64_t frames, uint64_t start, uint64_t end) {
    if (end > start) {
        uint64_t length = end - start;
        if (origin >= end) origin = start + (origin - start) % length;
        if (origin < start) {
            uint64_t intro = start - origin;
            if (frames < intro) return origin + frames;
            frames -= intro;
            origin = start;
        }
        uint64_t offset = origin - start;
        uint64_t remainder = frames % length;
        return start + (remainder >= length - offset ? remainder - (length - offset) : offset + remainder);
    }
    return frames > UINT64_MAX - origin ? UINT64_MAX : origin + frames;
}

// Flushes only while the sole producer and the SDL callback cannot access the queue concurrently.
void engine_clock_discontinuity(Engine* engine, uint64_t frame, bool hold, bool advancing) {
    if (engine->device.is_open) SDL_LockAudioDevice(engine->device.device_id);
    atomic_store(&engine->clock_capture_epoch, 0);
    atomic_fetch_add(&engine->clock_render_seq, 1);
    atomic_fetch_add(&engine->clock_callback_seq, 1);
    if (hold) {
        uint64_t elapsed = atomic_load(&engine->clock_advancing) ? atomic_load(&engine->clock_consumed) : 0;
        frame = engine_clock_advance(atomic_load(&engine->clock_origin), elapsed,
                                    atomic_load(&engine->clock_loop_start), atomic_load(&engine->clock_loop_end));
    }
    uint64_t start = 0, end = 0;
    if (advancing && atomic_load(&engine->loop_enabled)) {
        start = atomic_load(&engine->loop_start_frame);
        end = atomic_load(&engine->loop_end_frame);
        frame = engine_clock_advance(frame, 0, start, end);
    }
    ringbuf_reset(&engine->output_queue.buffer);
    atomic_store(&engine->output_queue.flush_head, 0);
    atomic_store(&engine->clock_origin, frame);
    atomic_store(&engine->clock_fallback_frame, frame);
    atomic_store(&engine->clock_rendered, 0);
    atomic_store(&engine->clock_consumed, 0);
    atomic_store(&engine->clock_callback_start, 0);
    atomic_store(&engine->clock_callback_frames, 0);
    atomic_store(&engine->clock_callback_ns, 0);
    atomic_store(&engine->clock_loop_start, start);
    atomic_store(&engine->clock_loop_end, end);
    atomic_store(&engine->clock_advancing, advancing);
    atomic_store(&engine->transport_frame, frame);
    atomic_fetch_add(&engine->clock_epoch, 1);
    atomic_fetch_add(&engine->clock_callback_seq, 1);
    atomic_fetch_add(&engine->clock_render_seq, 1);
    if (advancing && end <= start) atomic_store(&engine->clock_capture_epoch, atomic_load(&engine->clock_epoch));
    if (engine->device.is_open) SDL_UnlockAudioDevice(engine->device.device_id);
}

// Publishes the prepared frame count and audio together for coherent clock readers.
size_t engine_clock_write(Engine* engine, const float* buffer, size_t frames) {
    atomic_fetch_add(&engine->clock_render_seq, 1);
    size_t written = audio_queue_write(&engine->output_queue, buffer, frames);
    atomic_fetch_add(&engine->clock_rendered, written);
    size_t queued = audio_queue_available_frames(&engine->output_queue);
    if (queued > atomic_load_explicit(&engine->diag_queue_high_water_frames, memory_order_relaxed))
        atomic_store_explicit(&engine->diag_queue_high_water_frames, queued, memory_order_relaxed);
    atomic_fetch_add(&engine->clock_render_seq, 1);
    return written;
}

// Reads stable producer/consumer generations and interpolates no farther than delivered audio.
bool engine_get_clock_snapshot(const Engine* engine, EngineClockSnapshot* out) {
    if (!engine || !out) return false;
    for (int attempt = 0; attempt < 64; ++attempt) {
        uint64_t producer = atomic_load(&engine->clock_render_seq);
        uint64_t consumer = atomic_load(&engine->clock_callback_seq);
        if ((producer | consumer) & 1) continue;
        EngineClockSnapshot snapshot = {0};
        snapshot.epoch = atomic_load(&engine->clock_epoch);
        uint64_t origin = atomic_load(&engine->clock_origin);
        uint64_t start = atomic_load(&engine->clock_loop_start), end = atomic_load(&engine->clock_loop_end);
        snapshot.playing = atomic_load(&engine->clock_advancing);
        snapshot.rendered_frames = atomic_load(&engine->clock_rendered);
        snapshot.consumed_frames = atomic_load(&engine->clock_consumed);
        uint64_t callback_start = atomic_load(&engine->clock_callback_start);
        uint64_t callback_frames = atomic_load(&engine->clock_callback_frames);
        uint64_t callback_ns = atomic_load(&engine->clock_callback_ns);
        snapshot.requested_serial = atomic_load(&engine->transport_requested_serial);
        snapshot.applied_serial = atomic_load(&engine->transport_applied_serial);
        uint64_t now = core_time_now_ns();
        snapshot.observed_ns = now;
        snapshot.loop_enabled = end > start;
        if (producer != atomic_load(&engine->clock_render_seq) || consumer != atomic_load(&engine->clock_callback_seq)) continue;
        if (snapshot.consumed_frames > snapshot.rendered_frames) continue;
        snapshot.queued_frames = snapshot.rendered_frames - snapshot.consumed_frames;
        if (snapshot.requested_serial < snapshot.applied_serial) snapshot.requested_serial = snapshot.applied_serial;
        snapshot.pending = snapshot.requested_serial > snapshot.applied_serial;
        snapshot.rendered_frame = engine_clock_advance(origin, snapshot.playing ? snapshot.rendered_frames : 0, start, end);
        snapshot.consumed_frame = engine_clock_advance(origin, snapshot.playing ? snapshot.consumed_frames : 0, start, end);
        uint64_t presented = snapshot.consumed_frames;
        if (callback_ns && snapshot.playing) {
            double elapsed = now > callback_ns ? (double)(now - callback_ns) * engine->config.sample_rate / 1e9 : 0;
            uint64_t offset = elapsed >= (double)callback_frames ? callback_frames : (uint64_t)elapsed;
            presented = callback_start + offset;
        }
        snapshot.presentation_frame = engine_clock_advance(origin, snapshot.playing ? presented : 0, start, end);
        *out = snapshot;
        return true;
    }
    return false;
}

// Falls back to the last callback-delivered position if concurrent publication exhausts the bounded read.
uint64_t engine_get_presentation_frame(const Engine* engine) {
    EngineClockSnapshot snapshot;
    if (engine_get_clock_snapshot(engine, &snapshot)) return snapshot.presentation_frame;
    return engine ? atomic_load(&engine->clock_fallback_frame) : 0;
}

// Records whole-block render cost; the worker is the sole writer of these counters.
void engine_diagnostics_render(Engine* engine, uint64_t elapsed_ns, size_t frames) {
    if (!engine || !frames || engine->config.sample_rate <= 0) return;
    atomic_fetch_add_explicit(&engine->diag_render_blocks, 1, memory_order_relaxed);
    if (elapsed_ns > atomic_load_explicit(&engine->diag_render_max_ns, memory_order_relaxed))
        atomic_store_explicit(&engine->diag_render_max_ns, elapsed_ns, memory_order_relaxed);
    if ((double)elapsed_ns > (double)frames * 1e9 / engine->config.sample_rate)
        atomic_fetch_add_explicit(&engine->diag_render_over_budget, 1, memory_order_relaxed);
}

// Collects atomic telemetry without imposing a global snapshot barrier.
bool engine_get_diagnostics(const Engine* engine, EngineDiagnostics* out) {
    if (!engine || !out) return false;
    out->processing_latency_frames = atomic_load(&engine->diag_processing_latency);
    out->callback_count = atomic_load_explicit(&engine->diag_callback_count, memory_order_relaxed);
    out->underrun_callbacks = atomic_load_explicit(&engine->diag_underrun_callbacks, memory_order_relaxed);
    out->underrun_frames = atomic_load_explicit(&engine->diag_underrun_frames, memory_order_relaxed);
    out->worker_priority_status = atomic_load_explicit(&engine->diag_worker_priority_status, memory_order_relaxed);
    out->worker_cycles = atomic_load_explicit(&engine->diag_worker_cycles, memory_order_relaxed);
    out->worker_render_cycles = atomic_load_explicit(&engine->diag_worker_render_cycles, memory_order_relaxed);
    out->worker_over_budget = atomic_load_explicit(&engine->diag_worker_over_budget, memory_order_relaxed);
    out->worker_max_ns = atomic_load_explicit(&engine->diag_worker_max_ns, memory_order_relaxed);
    out->service_max_ns = atomic_load_explicit(&engine->diag_service_max_ns, memory_order_relaxed);
    out->queue_target_frames = atomic_load_explicit(&engine->output_target_frames, memory_order_relaxed);
    out->render_blocks = atomic_load_explicit(&engine->diag_render_blocks, memory_order_relaxed);
    out->render_over_budget = atomic_load_explicit(&engine->diag_render_over_budget, memory_order_relaxed);
    out->render_max_ns = atomic_load_explicit(&engine->diag_render_max_ns, memory_order_relaxed);
    out->command_max_age_ns = atomic_load_explicit(&engine->diag_command_max_age_ns, memory_order_relaxed);
    out->command_last_age_ns = atomic_load_explicit(&engine->diag_command_last_age_ns, memory_order_relaxed);
    out->queue_high_water_frames = atomic_load_explicit(&engine->diag_queue_high_water_frames, memory_order_relaxed);
    EngineClockSnapshot clock;
    out->queued_frames = engine_get_clock_snapshot(engine, &clock) ? clock.queued_frames : 0;
    size_t pending = ringbuf_available_read(&engine->command_queue);
    out->pending_commands = (pending <= engine->command_queue.capacity ? pending : 0) / sizeof(EngineCommand);
    engine_get_command_stats(engine, &out->commands);
    return true;
}

// Summarizes engine-lifetime failures and current queue depth without real-time-thread formatting.
bool engine_format_diagnostics(const Engine* engine, char* text, size_t capacity) {
    if (!text || !capacity) return false;
    EngineDiagnostics d;
    if (!engine_get_diagnostics(engine, &d)) { text[0] = 0; return false; }
    double queue_ms = engine->config.sample_rate > 0 ? 1000.0 * d.queued_frames / engine->config.sample_rate : 0;
    snprintf(text, capacity, "Audio totals: gaps %llu (%llu frames), slow blocks %llu worker %llu | queue %.1f ms, DSP %.1f ms | cmd max %.1f ms, rejected %llu, safety %llu",
        (unsigned long long)d.underrun_callbacks, (unsigned long long)d.underrun_frames,
        (unsigned long long)d.render_over_budget, (unsigned long long)d.worker_over_budget, queue_ms, engine->config.sample_rate > 0 ? 1000.0 * d.processing_latency_frames / engine->config.sample_rate : 0, d.command_max_age_ns / 1e6,
        (unsigned long long)d.commands.rejected, (unsigned long long)d.commands.safety_fallbacks);
    return d.underrun_callbacks || d.worker_over_budget || d.render_over_budget || d.commands.rejected || d.commands.safety_fallbacks;
}

// Counts service-only and rendered iterations against one nominal DSP block duration.
void engine_diagnostics_worker(Engine* engine, uint64_t elapsed_ns, uint64_t service_ns, bool rendered) {
    if (!engine || engine->config.sample_rate <= 0 || engine->config.block_size <= 0) return;
    atomic_fetch_add_explicit(&engine->diag_worker_cycles, 1, memory_order_relaxed);
    if (rendered) atomic_fetch_add_explicit(&engine->diag_worker_render_cycles, 1, memory_order_relaxed);
    if (elapsed_ns > atomic_load_explicit(&engine->diag_worker_max_ns, memory_order_relaxed))
        atomic_store_explicit(&engine->diag_worker_max_ns, elapsed_ns, memory_order_relaxed);
    if (service_ns > atomic_load_explicit(&engine->diag_service_max_ns, memory_order_relaxed))
        atomic_store_explicit(&engine->diag_service_max_ns, service_ns, memory_order_relaxed);
    if ((double)elapsed_ns > (double)engine->config.block_size * 1e9 / engine->config.sample_rate)
        atomic_fetch_add_explicit(&engine->diag_worker_over_budget, 1, memory_order_relaxed);
}

// Keeps target arithmetic independent of ring capacity and rounds callback headroom to DSP blocks.
size_t engine_output_target_frames(int block, int callback, int requested_blocks) {
    if (block <= 0 || callback <= 0) return 0;
    size_t blocks = requested_blocks >= 2 && requested_blocks <= 32 ? (size_t)requested_blocks : 32;
    size_t callback_blocks = (2u * (size_t)callback + (size_t)block - 1) / (size_t)block;
    if (blocks < callback_blocks) blocks = callback_blocks;
    return blocks * (size_t)block;
}

// Confirms an anchored linear take using one atomic observation independent of busy diagnostic publications.
bool engine_capture_epoch_is_current(const Engine* engine, uint64_t epoch) {
    return engine && epoch && atomic_load(&engine->clock_capture_epoch) == epoch;
}
