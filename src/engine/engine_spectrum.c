#include "engine/engine_internal.h"

#include <math.h>
#include "engine/analysis_math.h"
#include <string.h>

_Static_assert(ENGINE_SPECTRUM_FFT_SIZE <= ENGINE_ANALYSIS_MAX_FRAMES &&
               ENGINE_SPECTRUM_BINS <= ENGINE_ANALYSIS_MAX_BINS, "analyzer plan capacity");



// Captures one coherent target and resolves its stable identity against the active render revision.
bool engine_spectrum_begin_block(Engine* engine) {
    if (!engine) return false;
    engine->spectrum_update_active = engine_analysis_begin_at(&engine->spectrum_stream, atomic_load(&engine->clock_epoch));
    EngineAnalysisSelection* selected = &engine->spectrum_stream.capture.selection;
    engine->spectrum_update_master = selected->view == ENGINE_SPECTRUM_VIEW_MASTER;
    engine->spectrum_update_track = selected->view == ENGINE_SPECTRUM_VIEW_TRACK;
    engine->spectrum_active_track = -1;
    if (engine->spectrum_update_track) {
        EngineMixState* mix = engine_render_mix_state(engine);
        for (int t = 0; mix && t < mix->track_count; ++t) {
            if (mix->tracks[t].runtime_id == selected->track_id) { engine->spectrum_active_track = t; break; }
        }
        if (engine->spectrum_active_track < 0) {
            engine->spectrum_update_active = false;
            engine->spectrum_stream.filled = 0;
        }
    }
    return engine->spectrum_update_active;
}

// Appends every master sample to producer-owned contiguous window assembly.
void engine_spectrum_update(Engine* engine, const float* input, int frames, int channels) {
    if (!engine || !engine->spectrum_update_active || !engine->spectrum_update_master) return;
    engine_analysis_append(&engine->spectrum_stream, &engine->spectrum_queue, input, frames, channels, ENGINE_SPECTRUM_FFT_SIZE);
}

// Appends the selected render track without reading editable track arrays.
void engine_spectrum_update_track(Engine* engine, int track, const float* input, int frames, int channels) {
    if (!engine || !engine->spectrum_update_active || !engine->spectrum_update_track ||
        track != engine->spectrum_active_track) return;
    engine_analysis_append(&engine->spectrum_stream, &engine->spectrum_queue, input, frames, channels, ENGINE_SPECTRUM_FFT_SIZE);
}

// Computes complete windows with consumer-owned averaging and generation-checked publication.
int engine_spectrum_thread_main(void* userdata) {
    Engine* engine = userdata;
    if (!engine) return -1;
    EngineAnalysisPlan plan;
    if (!engine_analysis_prepare(&plan, ENGINE_SPECTRUM_FFT_SIZE, ENGINE_SPECTRUM_BINS,
                                 engine->config.sample_rate, ENGINE_SPECTRUM_MIN_HZ,
                                 ENGINE_SPECTRUM_MAX_HZ)) return -1;
    EngineAnalysisPacket packet;
    float history[ENGINE_SPECTRUM_AVG_FRAMES][ENGINE_SPECTRUM_BINS] = {{0}};
    int average_index = 0, average_count = 0;
    uint64_t revision = UINT64_MAX, epoch = UINT64_MAX, sequence = UINT64_MAX;
    while (atomic_load(&engine->spectrum_running)) {
        if (!engine_analysis_receive(&engine->spectrum_stream, &engine->spectrum_queue, &packet)) { SDL_Delay(1); continue; }
        if (!engine_analysis_current(&engine->spectrum_stream, &packet) || packet.epoch != atomic_load(&engine->clock_epoch)) {
            atomic_fetch_add(&engine->spectrum_stream.stale, 1);
            continue;
        }
        if (revision != packet.selection.revision || epoch != packet.epoch || packet.sequence != sequence + 1) {
            average_index = 0; average_count = 0;
        }
        revision = packet.selection.revision; epoch = packet.epoch; sequence = packet.sequence;
        float bins[ENGINE_SPECTRUM_BINS], average[ENGINE_SPECTRUM_BINS];
        uint64_t began = SDL_GetPerformanceCounter();
        engine_analysis_compute(&plan, packet.samples, bins);
        engine_analysis_computed(&engine->spectrum_stream, began);
        memcpy(history[average_index], bins, sizeof(bins));
        average_index = (average_index + 1) % ENGINE_SPECTRUM_AVG_FRAMES;
        if (average_count < ENGINE_SPECTRUM_AVG_FRAMES) ++average_count;
        for (int b = 0; b < ENGINE_SPECTRUM_BINS; ++b) {
            float db = engine_analysis_power_mean_db(&history[0][b], average_count, ENGINE_SPECTRUM_BINS);
            average[b] = fmaxf(ENGINE_SPECTRUM_DB_FLOOR, fminf(ENGINE_SPECTRUM_DB_CEIL, db));
        }
        SDL_LockMutex(engine->spectrum_mutex);
        if (engine_analysis_current(&engine->spectrum_stream, &packet) && packet.epoch == atomic_load(&engine->clock_epoch)) {
            int next = (engine->spectrum_history_index + 1) % ENGINE_SPECTRUM_HISTORY;
            memcpy(engine->spectrum_history[next], average, sizeof(average));
            engine->spectrum_history_index = next;
            engine->spectrum_bins = ENGINE_SPECTRUM_BINS;
            engine->spectrum_result_epoch = packet.epoch;
            engine->spectrum_result_track_id = packet.selection.track_id;
            engine->spectrum_result_view = packet.selection.view;
            atomic_fetch_add(&engine->spectrum_stream.published, 1);
        } else {
            atomic_fetch_add(&engine->spectrum_stream.stale, 1);
        }
        SDL_UnlockMutex(engine->spectrum_mutex);
    }
    return 0;
}

// Copies one selected spectrum result only when its source identity matches the requested view.
static int spectrum_snapshot(const Engine* engine, int view, uint64_t identity, float* bins, int capacity) {
    if (!engine || !bins || capacity <= 0 || !engine->spectrum_mutex) return 0;
    SDL_LockMutex(engine->spectrum_mutex);
    int count = engine->spectrum_bins < capacity ? engine->spectrum_bins : capacity;
    if (engine->spectrum_result_epoch != atomic_load(&engine->clock_epoch)) count = 0;
    if (engine->spectrum_result_view != view || engine->spectrum_result_track_id != identity) count = 0;
    if (count > 0) memcpy(bins, engine->spectrum_history[engine->spectrum_history_index], (size_t)count * sizeof(float));
    SDL_UnlockMutex(engine->spectrum_mutex);
    return count;
}

// Copies the latest master spectrum under the result publication lock.
int engine_get_spectrum_snapshot(const Engine* engine, float* bins, int capacity) {
    return spectrum_snapshot(engine, ENGINE_SPECTRUM_VIEW_MASTER, 0, bins, capacity);
}

// Resolves an editable track's stable identity before retrieving its spectrum result.
int engine_get_track_spectrum_snapshot(const Engine* engine, int track, float* bins, int capacity) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id || track < 0 || track >= engine->track_count) return 0;
    return spectrum_snapshot(engine, ENGINE_SPECTRUM_VIEW_TRACK, engine->tracks[track].runtime_id, bins, capacity);
}

// Publishes target changes atomically and clears the previous result without resetting a live queue.
void engine_set_spectrum_target(Engine* engine, EngineSpectrumView view, int track, bool enabled) {
    if (!engine || SDL_ThreadID() != engine->control_thread_id) return;
    uint64_t identity = 0;
    if (view == ENGINE_SPECTRUM_VIEW_TRACK) {
        if (track < 0 || track >= engine->track_count) enabled = false;
        else identity = engine->tracks[track].runtime_id;
    }
    if (engine_analysis_select(&engine->spectrum_stream, identity, view, 0, enabled)) {
        SDL_LockMutex(engine->spectrum_mutex);
        engine->spectrum_bins = 0;
        engine->spectrum_result_track_id = identity;
        engine->spectrum_result_view = view;
        SDL_UnlockMutex(engine->spectrum_mutex);
    }
}
