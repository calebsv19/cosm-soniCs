#include "engine/export_stream.h"
#include "engine/engine_internal.h"
#include "audio/wav_writer.h"
#include <stdlib.h>
#include <math.h>
// Retains immutable render metadata and an engine owner used only for control-side retirement.
struct EngineExportSnapshot {
    Engine* owner;
    EngineSourcePlan* plan;
    uint64_t start, end;
    EngineBounceOptions policy;
    EngineBounceWavFormat format;
    int channels, rate, block;
    bool no_replace;
};
// Validates stream geometry and prepares a source plan before dispatch to the worker.
EngineExportSnapshot* engine_export_prepare(Engine* engine, uint64_t start, uint64_t end,
                                            const EngineBounceOptions* options,
                                            EngineBounceWavFormat format, bool no_replace) {
    if (!engine_is_control_thread(engine) || end <= start ||
        (format != ENGINE_BOUNCE_WAV_PCM16 && format != ENGINE_BOUNCE_WAV_FLOAT32))
        return NULL;
    EngineBounceOptions policy = options ? *options : engine_bounce_options_default();
    int channels = engine_graph_get_channels(engine->graph), rate = engine->config.sample_rate,
        block = engine->config.block_size;
    unsigned width = format == ENGINE_BOUNCE_WAV_FLOAT32 ? 4 : 2;
    if (channels <= 0 || rate <= 0 || block <= 0 || policy.tail_frames > UINT64_MAX - end ||
        policy.preroll_frames > (uint64_t)rate * 60 || policy.tail_frames > (uint64_t)rate * 60 ||
        (size_t)block > SIZE_MAX / sizeof(float) / channels ||
        end - start + policy.tail_frames > (UINT32_MAX - 36u) / ((uint64_t)channels * width))
        return NULL;
    EngineExportSnapshot* s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    *s = (EngineExportSnapshot){.owner = engine,
                                .start = start,
                                .end = end,
                                .policy = policy,
                                .format = format,
                                .channels = channels,
                                .rate = rate,
                                .block = block,
                                .no_replace = no_replace};
    s->plan = engine_source_plan_prepare_export_filtered(engine, end, policy.tail_frames, &policy);
    if (!s->plan) {
        free(s);
        return NULL;
    }
    return s;
}
// Retires private sample pins before their engine/cache owner can be destroyed.
void engine_export_free(EngineExportSnapshot* s) {
    if (!s)
        return;
    engine_source_plan_discard(s->owner, s->plan);
    free(s);
}
// Streams the accepted graph through bounded buffers and an anonymous spool.
DawSaveResult engine_export_run(EngineExportSnapshot* capture, const char* path,
                                const EngineBounceStreamCallbacks* callbacks) {
    if (!capture || !path)
        return DAW_SAVE_FAILED;
    EngineSourcePlan* plan = capture->plan;
    uint64_t start = capture->start, end = capture->end;
    EngineBounceOptions policy = capture->policy;
    EngineBounceWavFormat format = capture->format;
    int channels = capture->channels, rate = capture->rate, block = capture->block;
    uint64_t total = end - start + policy.tail_frames;
    unsigned width = format == ENGINE_BOUNCE_WAV_FLOAT32 ? 4 : 2;
    FILE* spool = NULL;
    float* mixed = NULL;
    float* scratch = NULL;
    WavStreamWriter writer = {0};
    DawSaveResult result = DAW_SAVE_FAILED;
    uint64_t latency = engine_source_plan_export_latency(plan);
    uint64_t preroll = policy.preroll_frames < start ? policy.preroll_frames : start;
    if (latency > UINT64_MAX - end - policy.tail_frames || preroll > UINT64_MAX - latency ||
        total > (UINT64_MAX - preroll - latency) / 2)
        goto done;
    uint64_t discard = preroll + latency, render_frames = discard + total;
    uint64_t work = render_frames + total;
    mixed = calloc((size_t)block * channels, sizeof(float));
    scratch = calloc((size_t)block * channels, sizeof(float));
    spool = tmpfile();
    if (!mixed || !scratch || !spool)
        goto done;
    if (callbacks && callbacks->progress && !callbacks->progress(0, work, callbacks->user))
        goto done;
    float peak = 0;
    for (uint64_t rendered = 0; rendered < render_frames;) {
        int count =
            render_frames - rendered < (uint64_t)block ? (int)(render_frames - rendered) : block;
        if (!engine_source_plan_render_export(plan, start - preroll + rendered, count, mixed,
                                              scratch, channels))
            goto done;
        uint64_t first = rendered < discard ? discard : rendered;
        uint64_t last = rendered + count;
        if (last > first) {
            float* selected = mixed + (first - rendered) * channels;
            size_t samples = (size_t)(last - first) * channels;
            for (size_t i = 0; i < samples; ++i) {
                if (!isfinite(selected[i]))
                    goto done;
                peak = fmaxf(peak, fabsf(selected[i]));
            }
            if (fwrite(selected, sizeof(float), samples, spool) != samples)
                goto done;
        }
        rendered = last;
        if (callbacks && callbacks->progress &&
            !callbacks->progress(rendered, work, callbacks->user))
            goto done;
    }
    if (fflush(spool) != 0 || fseek(spool, 0, SEEK_SET) != 0 ||
        !wav_stream_begin(&writer, path, channels, rate, UINT32_C(0x534f4e49), width == 4))
        goto done;
    for (uint64_t written = 0; written < total;) {
        uint32_t count =
            total - written < (uint64_t)block ? (uint32_t)(total - written) : (uint32_t)block;
        size_t samples = (size_t)count * channels;
        if (fread(mixed, sizeof(float), samples, spool) != samples)
            goto done;
        if (policy.normalize_if_clipping && peak > 1)
            for (size_t i = 0; i < samples; ++i)
                mixed[i] /= peak;
        if (!wav_stream_append(&writer, mixed, count))
            goto done;
        if (callbacks && callbacks->samples)
            callbacks->samples(mixed, written, count, channels, callbacks->user);
        written += count;
        if (callbacks && callbacks->progress &&
            !callbacks->progress(render_frames + written, work, callbacks->user))
            goto done;
    }
    if (fclose(spool) != 0) {
        spool = NULL;
        goto done;
    }
    spool = NULL;
    writer.no_replace = capture->no_replace;
    result = wav_stream_finish(&writer);
done:
    wav_stream_abort(&writer);
    if (spool)
        fclose(spool);
    free(mixed);
    free(scratch);

    return result;
}
