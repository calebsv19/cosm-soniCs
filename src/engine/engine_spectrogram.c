#include "engine/engine_internal.h"

#include <math.h>
#include "engine/analysis_math.h"
#include <string.h>

_Static_assert(ENGINE_SPECTROGRAM_FFT_SIZE <= ENGINE_ANALYSIS_MAX_FRAMES &&
               ENGINE_SPECTROGRAM_BINS <= ENGINE_ANALYSIS_MAX_BINS, "analyzer plan capacity");


// Clamps a value between bounds for stable spectrogram output.
static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Resets the spectrogram history to the configured dB floor.
static void engine_spectrogram_reset_history(Engine* engine) {
    if (!engine) {
        return;
    }
    engine->spectrogram_state.head = 0;
    engine->spectrogram_state.count = 0;
    engine->spectrogram_state.bins = ENGINE_SPECTROGRAM_BINS;
    for (int i = 0; i < ENGINE_SPECTROGRAM_HISTORY; ++i) {
        for (int b = 0; b < ENGINE_SPECTROGRAM_BINS; ++b) {
            engine->spectrogram_state.history[i][b] = ENGINE_SPECTROGRAM_DB_FLOOR;
        }
    }
}



// Invalidates outstanding captures and clears visible history without modifying live queue indices.
void engine_spectrogram_clear_history(Engine* engine) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id) return;
    engine_analysis_invalidate(&engine->spectrogram_stream);
    SDL_LockMutex(engine->spectrogram_mutex);
    engine_spectrogram_reset_history(engine);
    SDL_UnlockMutex(engine->spectrogram_mutex);
}

// Selects a coherent capture target using producer-owned window state only.
bool engine_spectrogram_begin_block(Engine* engine) {
    if (!engine) return false;
    engine->spectrogram_update_active = engine_analysis_begin_at(&engine->spectrogram_stream, atomic_load(&engine->clock_epoch));
    if (!engine->spectrogram_stream.capture.selection.effect_id) engine->spectrogram_update_active = false;
    return engine->spectrogram_update_active;
}

// Captures the selected effect by stable track and instance identity from the render revision.
void engine_spectrogram_update_fx(Engine* engine, bool is_master, int track, FxInstId id,
                                  const float* input, int frames, int channels) {
    if (!engine || !engine->spectrogram_update_active) return;
    EngineAnalysisSelection* selected = &engine->spectrogram_stream.capture.selection;
    if (selected->effect_id != id) return;
    if (is_master) { if (selected->track_id != 0) return; }
    else {
        EngineMixState* mix = engine_render_mix_state(engine);
        if (!mix || track < 0 || track >= mix->track_count || mix->tracks[track].runtime_id != selected->track_id) return;
    }
    engine_analysis_append(&engine->spectrogram_stream, &engine->spectrogram_queue, input, frames, channels, ENGINE_SPECTROGRAM_FFT_SIZE);
}

// Analyzes complete windows and publishes only results belonging to the current selection generation.
int engine_spectrogram_thread_main(void* userdata) {
    Engine* engine = userdata;
    if (!engine) return -1;
    EngineAnalysisPlan plan;
    if (!engine_analysis_prepare(&plan, ENGINE_SPECTROGRAM_FFT_SIZE, ENGINE_SPECTROGRAM_BINS,
                                 engine->config.sample_rate, ENGINE_SPECTROGRAM_MIN_HZ,
                                 ENGINE_SPECTROGRAM_MAX_HZ)) return -1;
    EngineAnalysisPacket packet;
    uint64_t revision = UINT64_MAX, epoch = UINT64_MAX, sequence = UINT64_MAX;
    while (atomic_load(&engine->spectrogram_running)) {
        if (!engine_analysis_receive(&engine->spectrogram_stream, &engine->spectrogram_queue, &packet)) { SDL_Delay(1); continue; }
        if (!engine_analysis_current(&engine->spectrogram_stream, &packet) || packet.epoch != atomic_load(&engine->clock_epoch)) {
            atomic_fetch_add(&engine->spectrogram_stream.stale, 1);
            continue;
        }
        float bins[ENGINE_SPECTROGRAM_BINS];
        uint64_t began = SDL_GetPerformanceCounter();
        engine_analysis_compute(&plan, packet.samples, bins);
        engine_analysis_computed(&engine->spectrogram_stream, began);
        for (int b = 0; b < ENGINE_SPECTROGRAM_BINS; ++b)
            bins[b] = clampf(bins[b], ENGINE_SPECTROGRAM_DB_FLOOR, ENGINE_SPECTROGRAM_DB_CEIL);
        SDL_LockMutex(engine->spectrogram_mutex);
        if (engine_analysis_current(&engine->spectrogram_stream, &packet) && packet.epoch == atomic_load(&engine->clock_epoch)) {
            if (revision != packet.selection.revision || epoch != packet.epoch || packet.sequence != sequence + 1)
                engine_spectrogram_reset_history(engine);
            revision = packet.selection.revision; epoch = packet.epoch; sequence = packet.sequence;
            engine->spectrogram_result_epoch = packet.epoch;
            engine->spectrogram_result_stamp.selection = packet.selection;
            engine->spectrogram_result_stamp.epoch = packet.epoch;
            engine->spectrogram_result_stamp.sequence = packet.sequence;
            engine->spectrogram_result_stamp.first_sample = packet.first_sample;
            int next = (engine->spectrogram_state.head + 1) % ENGINE_SPECTROGRAM_HISTORY;
            engine->spectrogram_state.head = next;
            memcpy(engine->spectrogram_state.history[next], bins, sizeof(bins));
            if (engine->spectrogram_state.count < ENGINE_SPECTROGRAM_HISTORY) ++engine->spectrogram_state.count;
            atomic_fetch_add(&engine->spectrogram_stream.published, 1);
        } else {
            atomic_fetch_add(&engine->spectrogram_stream.stale, 1);
        }
        SDL_UnlockMutex(engine->spectrogram_mutex);
    }
    return 0;
}

// Copies the latest spectrogram history into a caller-owned buffer (newest-first rows).
bool engine_get_fx_spectrogram_snapshot(const Engine* engine,
                                        EngineSpectrogramSnapshot* out_meta,
                                        float* out_frames,
                                        int max_frames,
                                        int max_bins) {
    if (!engine || !out_meta || !out_frames || max_frames <= 0 || max_bins <= 0) {
        return false;
    }
    if (!engine->spectrogram_mutex) {
        return false;
    }
    SDL_LockMutex(engine->spectrogram_mutex);
    int bins = engine->spectrogram_state.bins;
    int count = engine->spectrogram_result_epoch == atomic_load(&engine->clock_epoch) ? engine->spectrogram_state.count : 0;
    int head = engine->spectrogram_state.head;
    if (bins > max_bins) {
        bins = max_bins;
    }
    if (count > max_frames) {
        count = max_frames;
    }
    for (int i = 0; i < count; ++i) {
        int idx = head - i;
        while (idx < 0) idx += ENGINE_SPECTROGRAM_HISTORY;
        idx %= ENGINE_SPECTROGRAM_HISTORY;
        memcpy(&out_frames[i * bins],
               engine->spectrogram_state.history[idx],
               (size_t)bins * sizeof(float));
    }
    out_meta->sample_rate = engine->config.sample_rate;
    out_meta->window_frames = ENGINE_SPECTROGRAM_FFT_SIZE;
    out_meta->epoch = engine->spectrogram_result_stamp.epoch;
    out_meta->sequence = engine->spectrogram_result_stamp.sequence;
    out_meta->first_sample = engine->spectrogram_result_stamp.first_sample;
    out_meta->track_id = engine->spectrogram_result_stamp.selection.track_id;
    out_meta->effect_id = engine->spectrogram_result_stamp.selection.effect_id;
    SDL_UnlockMutex(engine->spectrogram_mutex);
    out_meta->bins = bins;
    out_meta->frames = count;
    out_meta->db_floor = ENGINE_SPECTROGRAM_DB_FLOOR;
    out_meta->db_ceil = ENGINE_SPECTROGRAM_DB_CEIL;
    return count > 0;
}

// Publishes one coherent stable target and clears history when its meaning changes.
void engine_set_fx_spectrogram_target(Engine* engine, int track, FxInstId id, bool enabled) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id) return;
    uint64_t identity = 0;
    if (track >= 0) {
        if (track >= engine->track_count) enabled = false;
        else identity = engine->tracks[track].runtime_id;
    }
    if (id == 0) enabled = false;
    if (engine_analysis_select(&engine->spectrogram_stream, identity, 0, id, enabled)) {
        SDL_LockMutex(engine->spectrogram_mutex);
        engine_spectrogram_reset_history(engine);
        SDL_UnlockMutex(engine->spectrogram_mutex);
    }
}
