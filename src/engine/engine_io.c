#include "audio/wav_writer.h"
#include "engine/engine_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Adds a WAV-backed clip through the existing transactional project edit path.
bool engine_load_wav(Engine* engine, const char* path) {
    return engine && path && engine_add_clip(engine, path, 0);
}

// Releases an owned offline buffer and clears its public metadata.
void engine_bounce_buffer_free(EngineBounceBuffer* buffer) {
    if (!buffer)
        return;
    free(buffer->data);
    *buffer = (EngineBounceBuffer){0};
}

// Preserves the existing exact-range behavior while making its normalization policy explicit.
EngineBounceOptions engine_bounce_options_default(void) {
    return (EngineBounceOptions){.normalize_if_clipping = true};
}

// Renders a private authored snapshot without stopping playback or altering its DSP/transport/telemetry.
bool engine_bounce_range_to_buffer_with_options(Engine* engine, uint64_t start_frame, uint64_t end_frame,
                                                const EngineBounceOptions* options,
                                                void (*progress_cb)(uint64_t done_frames,
                                                                    uint64_t total_frames, void* user),
                                                void* user, EngineBounceBuffer* out_buffer) {
    if (!engine_is_control_thread(engine) || !out_buffer || out_buffer->data || end_frame <= start_frame)
        return false;
    const EngineBounceOptions policy = options ? *options : engine_bounce_options_default();
    const int channels = engine_graph_get_channels(engine->graph);
    const int rate = engine->config.sample_rate;
    const int block = engine->config.block_size;
    if (channels <= 0 || rate <= 0 || block <= 0 || policy.preroll_frames > (uint64_t)rate * 60 ||
        policy.tail_frames > (uint64_t)rate * 60 || policy.tail_frames > UINT64_MAX - end_frame)
        return false;
    const uint64_t total = end_frame - start_frame + policy.tail_frames;
    if (total > SIZE_MAX / sizeof(float) / (size_t)channels ||
        (size_t)block > SIZE_MAX / sizeof(float) / (size_t)channels)
        return false;
    const uint64_t preroll = policy.preroll_frames < start_frame ? policy.preroll_frames : start_frame;
    const uint64_t origin = start_frame - preroll;
    EngineSourcePlan* plan = engine_source_plan_prepare_export(engine, end_frame, policy.tail_frames);
    if (!plan)
        return false;
    const uint64_t latency = engine_source_plan_export_latency(plan);
    float* output = NULL;
    float* mixed = NULL;
    float* scratch = NULL;
    bool ok = false;
    if (latency > UINT64_MAX - end_frame - policy.tail_frames || preroll > UINT64_MAX - latency ||
        total > UINT64_MAX - preroll - latency)
        goto done;
    const uint64_t discard = preroll + latency;
    const uint64_t render_frames = discard + total;
    output = calloc((size_t)total * channels, sizeof(float));
    mixed = calloc((size_t)block * channels, sizeof(float));
    scratch = calloc((size_t)block * channels, sizeof(float));
    if (!output || !mixed || !scratch)
        goto done;
    if (progress_cb)
        progress_cb(0, render_frames, user);
    for (uint64_t rendered = 0; rendered < render_frames;) {
        uint64_t remaining = render_frames - rendered;
        int chunk = remaining < (uint64_t)block ? (int)remaining : block;
        if (!engine_source_plan_render_export(plan, origin + rendered, chunk, mixed, scratch, channels))
            goto done;
        uint64_t first = rendered < discard ? discard : rendered;
        uint64_t last = rendered + (uint64_t)chunk;
        if (last > first)
            memcpy(output + (size_t)(first - discard) * channels,
                   mixed + (size_t)(first - rendered) * channels,
                   (size_t)(last - first) * channels * sizeof(float));
        rendered = last;
        if (progress_cb)
            progress_cb(rendered, render_frames, user);
    }
    if (policy.normalize_if_clipping) {
        float peak = 0;
        for (size_t i = 0; i < (size_t)total * channels; ++i)
            peak = fmaxf(peak, fabsf(output[i]));
        if (peak > 1)
            for (size_t i = 0; i < (size_t)total * channels; ++i)
                output[i] /= peak;
    }
    *out_buffer =
        (EngineBounceBuffer){.data = output, .frame_count = total, .channels = channels, .sample_rate = rate};
    output = NULL;
    ok = true;
done:
    free(output);
    free(mixed);
    free(scratch);
    engine_source_plan_discard(engine, plan);
    return ok;
}

// Delegates the original bounce API to its documented default policy.
bool engine_bounce_range_to_buffer(Engine* engine, uint64_t start_frame, uint64_t end_frame,
                                   void (*progress_cb)(uint64_t done_frames, uint64_t total_frames,
                                                       void* user),
                                   void* user, EngineBounceBuffer* out_buffer) {
    return engine_bounce_range_to_buffer_with_options(engine, start_frame, end_frame, NULL, progress_cb, user,
                                                      out_buffer);
}

// Uses the durable writer and a fixed seed so identical PCM16 bounces produce identical bytes.
bool engine_bounce_write_wav(const EngineBounceBuffer* buffer, const char* path,
                             EngineBounceWavFormat format) {
    if (!buffer || !buffer->data || !path)
        return false;
    if (format == ENGINE_BOUNCE_WAV_FLOAT32)
        return wav_write_f32(path, buffer->data, buffer->frame_count, buffer->channels, buffer->sample_rate);
    if (format != ENGINE_BOUNCE_WAV_PCM16)
        return false;
    return wav_write_pcm16_dithered(path, buffer->data, buffer->frame_count, buffer->channels,
                                    buffer->sample_rate, UINT32_C(0x534f4e49));
}

// Streams a private render once to disk, then normalizes and converts bounded chunks before publication.
DawSaveResult engine_bounce_range_to_wav(Engine* engine, uint64_t start, uint64_t end,
                                        const EngineBounceOptions* options, const char* path,
                                        EngineBounceWavFormat format, const EngineBounceStreamCallbacks* callbacks) {
    if (!engine_is_control_thread(engine) || !path || end <= start ||
        (format != ENGINE_BOUNCE_WAV_PCM16 && format != ENGINE_BOUNCE_WAV_FLOAT32)) return DAW_SAVE_FAILED;
    EngineBounceOptions policy = options ? *options : engine_bounce_options_default();
    int channels = engine_graph_get_channels(engine->graph), rate = engine->config.sample_rate;
    int block = engine->config.block_size;
    if (channels <= 0 || rate <= 0 || block <= 0 || policy.tail_frames > UINT64_MAX - end ||
        policy.preroll_frames > (uint64_t)rate * 60 || policy.tail_frames > (uint64_t)rate * 60 ||
        (size_t)block > SIZE_MAX / sizeof(float) / channels) return DAW_SAVE_FAILED;
    uint64_t total = end - start + policy.tail_frames;
    unsigned width = format == ENGINE_BOUNCE_WAV_FLOAT32 ? 4 : 2;
    if (total > (UINT32_MAX - 36u) / ((uint64_t)channels * width)) return DAW_SAVE_FAILED;
    EngineSourcePlan* plan = engine_source_plan_prepare_export(engine, end, policy.tail_frames);
    if (!plan) return DAW_SAVE_FAILED;
    FILE* spool = NULL;
    float* mixed = NULL;
    float* scratch = NULL;
    WavStreamWriter writer = {0};
    DawSaveResult result = DAW_SAVE_FAILED;
    uint64_t latency = engine_source_plan_export_latency(plan);
    uint64_t preroll = policy.preroll_frames < start ? policy.preroll_frames : start;
    if (latency > UINT64_MAX - end - policy.tail_frames || preroll > UINT64_MAX - latency ||
        total > (UINT64_MAX - preroll - latency) / 2) goto done;
    uint64_t discard = preroll + latency, render_frames = discard + total;
    uint64_t work = render_frames + total;
    mixed = calloc((size_t)block * channels, sizeof(float));
    scratch = calloc((size_t)block * channels, sizeof(float));
    spool = tmpfile();
    if (!mixed || !scratch || !spool) goto done;
    if (callbacks && callbacks->progress && !callbacks->progress(0, work, callbacks->user)) goto done;
    float peak = 0;
    for (uint64_t rendered = 0; rendered < render_frames;) {
        int count = render_frames - rendered < (uint64_t)block ? (int)(render_frames - rendered) : block;
        if (!engine_source_plan_render_export(plan, start - preroll + rendered, count, mixed, scratch, channels)) goto done;
        uint64_t first = rendered < discard ? discard : rendered;
        uint64_t last = rendered + count;
        if (last > first) {
            float* selected = mixed + (first - rendered) * channels;
            size_t samples = (size_t)(last - first) * channels;
            for (size_t i = 0; i < samples; ++i) {
                if (!isfinite(selected[i])) goto done;
                peak = fmaxf(peak, fabsf(selected[i]));
            }
            if (fwrite(selected, sizeof(float), samples, spool) != samples) goto done;
        }
        rendered = last;
        if (callbacks && callbacks->progress && !callbacks->progress(rendered, work, callbacks->user)) goto done;
    }
    if (fflush(spool) != 0 || fseek(spool, 0, SEEK_SET) != 0 ||
        !wav_stream_begin(&writer, path, channels, rate, UINT32_C(0x534f4e49), width == 4)) goto done;
    for (uint64_t written = 0; written < total;) {
        uint32_t count = total - written < (uint64_t)block ? (uint32_t)(total - written) : (uint32_t)block;
        size_t samples = (size_t)count * channels;
        if (fread(mixed, sizeof(float), samples, spool) != samples) goto done;
        if (policy.normalize_if_clipping && peak > 1)
            for (size_t i = 0; i < samples; ++i) mixed[i] /= peak;
        if (!wav_stream_append(&writer, mixed, count)) goto done;
        if (callbacks && callbacks->samples) callbacks->samples(mixed, written, count, channels, callbacks->user);
        written += count;
        if (callbacks && callbacks->progress && !callbacks->progress(render_frames + written, work, callbacks->user)) goto done;
    }
    if (fclose(spool) != 0) { spool = NULL; goto done; }
    spool = NULL;
    result = wav_stream_finish(&writer);
done:
    wav_stream_abort(&writer);
    if (spool) fclose(spool);
    free(mixed);
    free(scratch);
    engine_source_plan_discard(engine, plan);
    return result;
}

// Bridges the original non-cancellable progress callback without changing its caller contract.
typedef struct BounceProgressBridge {
    void (*callback)(uint64_t, uint64_t, void*);
    void* user;
} BounceProgressBridge;

// Forwards monotonic two-pass work progress to legacy file-export clients.
static bool bounce_forward_progress(uint64_t done, uint64_t total, void* user) {
    BounceProgressBridge* bridge = user;
    if (bridge->callback) bridge->callback(done, total, bridge->user);
    return true;
}

// Publishes a bounded-memory PCM16 export without implicit comparison sidecars.
bool engine_bounce_range(Engine* engine, uint64_t start_frame, uint64_t end_frame, const char* out_path,
                         void (*progress_cb)(uint64_t, uint64_t, void*), void* user) {
    BounceProgressBridge bridge = {progress_cb, user};
    EngineBounceStreamCallbacks callbacks = {.progress = bounce_forward_progress, .user = &bridge};
    return engine_bounce_range_to_wav(engine, start_frame, end_frame, NULL, out_path,
                                     ENGINE_BOUNCE_WAV_PCM16, &callbacks) == DAW_SAVE_SYNCED;
}
