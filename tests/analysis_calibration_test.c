#include "engine/analysis_math.h"
#include "engine/engine_internal.h"
#include "test_assert.h"
#include <math.h>
#include <string.h>
#define CHECK(v, m) daw_test_expect("analysis_calibration_test", (v), (m))

// Creates an independently known sinusoid at the analyzer's sampled frequency.
static void tone(float* data, int frames, int rate, double hz, float amplitude) {
    for (int i = 0; i < frames; ++i)
        data[i] = amplitude * sinf((float)(6.283185307179586 * hz * i / rate));
}

// Waits for actual analyzer publication instead of assuming a fixed computation time.
static void published(EngineAnalysisStream* stream, uint64_t expected) {
    for (int i = 0; i < 3000; ++i) {
        if (atomic_load(&stream->published) >= expected)
            return;
        SDL_Delay(1);
    }
    CHECK(false, "analyzer publication timed out");
}

// Proves tonal level, rate/window scaling, endpoints, and power averaging independently of display clipping.
static void calibration(void) {
    float data[2048];
    int rates[] = {44100, 48000, 96000};
    for (int r = 0; r < 3; ++r)
        for (int n = 1024; n <= 2048; n *= 2) {
            for (int f = 0; f < 2; ++f) {
                double hz = f ? 8000 : 1000;
                tone(data, n, rates[r], hz, .5f);
                CHECK(fabsf(engine_analysis_tone_db(data, n, rates[r], hz) + 6.0206f) < .03f,
                      "flat half-scale tone calibration");
            }
            memset(data, 0, sizeof(data));
            CHECK(engine_analysis_tone_db(data, n, rates[r], 1000) == -160, "silence floor");
            for (int i = 0; i < n; ++i)
                data[i] = 1;
            CHECK(fabsf(engine_analysis_tone_db(data, n, rates[r], 0)) < 1e-5f, "DC endpoint scaling");
            for (int i = 0; i < n; ++i)
                data[i] = (i & 1) ? -1 : 1;
            CHECK(fabsf(engine_analysis_tone_db(data, n, rates[r], rates[r] * .5)) < 1e-5f,
                  "Nyquist endpoint scaling");
        }
    float levels[] = {0, -20};
    CHECK(fabsf(engine_analysis_power_mean_db(levels, 2, 1) + 2.967086f) < 1e-5f,
          "power mean differs from dB mean");
    CHECK(fabsf(engine_analysis_frequency(127, 128, 22050, 20, 20000) - 11025) < .01f,
          "frequency axis exceeds Nyquist");
}

// Compares every prepared bin against the original direct trigonometric definition across signal classes.
static void prepared_parity(void) {
    EngineAnalysisPlan plan;
    float samples[2048], bins[256];
    int rates[] = {22050, 44100, 48000, 96000};
    double maximum = 0;
    unsigned random = 7;
    CHECK(!engine_analysis_prepare(&plan, 2049, 256, 48000, 20, 20000), "oversized plan accepted");
    CHECK(!engine_analysis_prepare(&plan, 2048, 257, 48000, 20, 20000), "oversized grid accepted");
    for (int r = 0; r < 4; ++r) for (int shape = 0; shape < 2; ++shape) {
        int frames = shape ? 2048 : 1024, count = shape ? 256 : 128;
        CHECK(engine_analysis_prepare(&plan, frames, count, rates[r], 20, 20000), "prepare");
        for (int signal = 0; signal < 9; ++signal) {
            double hz = engine_analysis_frequency(count / 2, count, rates[r], 20, 20000);
            for (int i = 0; i < frames; ++i) {
                random = random * 1664525u + 1013904223u;
                double phase = 6.283185307179586 * hz * i / rates[r];
                samples[i] = signal == 0 ? 0 : signal == 1 ? 1 :
                    signal == 2 ? (i & 1 ? -1 : 1) : signal == 3 ? (i == frames / 2 ? 1 : 0) :
                    signal == 4 ? .5 * sin(phase) : signal == 5 ? 1e-7 * sin(phase) :
                    signal == 6 ? (float)(random >> 8) / 8388608 - 1 :
                    signal == 7 ? .3 * sin(phase * 1.013) + .2 * cos(phase * 2.57) :
                    (i % 3 == 0 ? NAN : i % 3 == 1 ? INFINITY : -INFINITY);
            }
            engine_analysis_compute(&plan, samples, bins);
            for (int b = 0; b < count; ++b) {
                float reference = engine_analysis_tone_db(samples, frames, rates[r],
                    engine_analysis_frequency(b, count, rates[r], 20, 20000));
                double error = fabs(bins[b] - reference);
                if (error > maximum) maximum = error;
                CHECK(isfinite(bins[b]) && error <= .002, "prepared/reference dB mismatch");
            }
        }
    }
    printf("prepared parity: 13824 bins, max_error_db=%.9g, plan_bytes=%zu\n", maximum, sizeof(plan));
}

// Proves real spectrum worker calibration, gap resets, epoch rejection, and fixed stereo-mid behavior.
static void spectrum_worker(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    Engine* engine = engine_create(&cfg);
    CHECK(engine != NULL, "engine");
    engine_set_spectrum_target(engine, ENGINE_SPECTRUM_VIEW_MASTER, -1, true);
    float stale_samples[2048] = {0};
    for (int i = 0; i < 2; ++i) {
        CHECK(engine_spectrum_begin_block(engine), "stale packet begin");
        engine_spectrum_update(engine, stale_samples, 2048, 1);
    }
    EngineAnalysisDiagnostics diagnostics;
    CHECK(engine_get_analysis_diagnostics(engine, false, &diagnostics) && diagnostics.pending == 2,
          "pending backlog before consumer");
    engine_analysis_invalidate(&engine->spectrum_stream);
    atomic_store(&engine->spectrum_running, true);
    SDL_Thread* thread = SDL_CreateThread(engine_spectrum_thread_main, "calibration", engine);
    CHECK(thread != NULL, "spectrum thread");
    for (int i = 0; i < 3000 && atomic_load(&engine->spectrum_stream.stale) < 2; ++i) SDL_Delay(1);
    CHECK(engine_get_analysis_diagnostics(engine, false, &diagnostics) && diagnostics.stale == 2 &&
          diagnostics.consumed == 2 && diagnostics.pending == 0 && diagnostics.computed == 0 &&
          diagnostics.backlog_high_water == 2, "stale backlog diagnostics");
    int bin = 160;
    float hz = engine_analysis_frequency(bin, ENGINE_SPECTRUM_BINS, cfg.sample_rate, 20, 20000);
    float samples[2048], bins[ENGINE_SPECTRUM_BINS];
    for (int pass = 0; pass < 3; ++pass) {
        CHECK(engine_spectrum_begin_block(engine), "begin master");
        if (pass == 2)
            engine->spectrum_stream.next_sequence++; // Models a dropped complete window.
        tone(samples, 2048, cfg.sample_rate, hz, pass ? .1f : 1);
        engine_spectrum_update(engine, samples, 2048, 1);
        published(&engine->spectrum_stream, pass + 1);
        CHECK(engine_get_spectrum_snapshot(engine, bins, ENGINE_SPECTRUM_BINS) == ENGINE_SPECTRUM_BINS,
              "snapshot");
        float expected = pass == 0 ? 0 : pass == 1 ? -2.967086f : -20;
        CHECK(fabsf(bins[bin] - expected) < .03f, "worker scale or gap reset");
    }
    CHECK(engine_spectrum_begin_block(engine), "partial begin");
    engine_spectrum_update(engine, samples, 128, 1);
    CHECK(engine_transport_seek(engine, 500), "seek");
    CHECK(engine_get_spectrum_snapshot(engine, bins, ENGINE_SPECTRUM_BINS) == 0,
          "stale epoch remained visible");
    CHECK(engine_spectrum_begin_block(engine) && engine->spectrum_stream.filled == 0,
          "partial window survived seek");
    float stereo[4096];
    for (int i = 0; i < 2048; ++i) {
        stereo[i * 2] = samples[i];
        stereo[i * 2 + 1] = -samples[i];
    }
    engine_spectrum_update(engine, stereo, 2048, 2);
    published(&engine->spectrum_stream, 4);
    CHECK(engine_get_spectrum_snapshot(engine, bins, ENGINE_SPECTRUM_BINS) == ENGINE_SPECTRUM_BINS,
          "new epoch snapshot");
    for (int b = 0; b < ENGINE_SPECTRUM_BINS; ++b)
        CHECK(bins[b] == ENGINE_SPECTRUM_DB_FLOOR, "mid cancellation contract");
    engine_set_spectrum_target(engine, ENGINE_SPECTRUM_VIEW_TRACK, 0, true);
    CHECK(engine_get_spectrum_snapshot(engine, bins, ENGINE_SPECTRUM_BINS) == 0, "wrong target visible");
    atomic_store(&engine->spectrum_running, false);
    SDL_WaitThread(thread, NULL);
    CHECK(engine_get_analysis_diagnostics(engine, false, &diagnostics) && diagnostics.computed == 4 &&
          diagnostics.published == 4 && diagnostics.pending == 0 && diagnostics.compute_total_ms > 0 &&
          diagnostics.compute_max_ms > 0 && diagnostics.compute_max_ms <= diagnostics.compute_total_ms,
          "transform diagnostics");
    engine_destroy(engine);
}

// Proves spectrogram insert identity, calibrated rows, gap resets, and captured-window time metadata.
static void spectrogram_worker(void) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    Engine* engine = engine_create(&cfg);
    CHECK(engine != NULL, "spectrogram engine");
    engine_set_fx_spectrogram_target(engine, -1, 77, true);
    atomic_store(&engine->spectrogram_running, true);
    SDL_Thread* thread = SDL_CreateThread(engine_spectrogram_thread_main, "spectrogram-cal", engine);
    CHECK(thread != NULL, "spectrogram thread");
    float samples[1024], rows[ENGINE_SPECTROGRAM_HISTORY * ENGINE_SPECTROGRAM_BINS];
    int bin = 80;
    float hz = engine_analysis_frequency(bin, ENGINE_SPECTROGRAM_BINS, cfg.sample_rate, 20, 20000);
    tone(samples, 1024, cfg.sample_rate, hz, .5f);
    EngineSpectrogramSnapshot meta;
    for (int pass = 0; pass < 3; ++pass) {
        CHECK(engine_spectrogram_begin_block(engine), "begin insert");
        if (pass == 2)
            engine->spectrogram_stream.next_sequence++;
        engine_spectrogram_update_fx(engine, true, -1, 77, samples, 1024, 1);
        published(&engine->spectrogram_stream, pass + 1);
        CHECK(engine_get_fx_spectrogram_snapshot(engine, &meta, rows, ENGINE_SPECTROGRAM_HISTORY,
                                                 ENGINE_SPECTROGRAM_BINS),
              "spectrogram snapshot");
        CHECK(meta.frames == (pass == 1 ? 2 : 1) && fabsf(rows[bin] + 6.0206f) < .03f,
              "spectrogram calibration or gap history");
        CHECK(meta.effect_id == 77 && meta.track_id == 0 && meta.window_frames == 1024 &&
                  meta.sample_rate == cfg.sample_rate && meta.first_sample == (uint64_t)pass * 1024,
              "window identity/timing");
    }
    CHECK(engine_spectrogram_begin_block(engine), "partial insert before bypass");
    engine_spectrogram_update_fx(engine, true, -1, 77, samples, 128, 1);
    CHECK(engine_spectrogram_begin_block(engine), "bypassed insert block");
    CHECK(engine_spectrogram_begin_block(engine), "reenabled insert block");
    CHECK(engine->spectrogram_stream.filled == 0, "partial window bridged bypass");
    engine_spectrogram_update_fx(engine, true, -1, 77, samples, 1024, 1);
    published(&engine->spectrogram_stream, 4);
    CHECK(engine_get_fx_spectrogram_snapshot(engine, &meta, rows, ENGINE_SPECTROGRAM_HISTORY,
                                            ENGINE_SPECTROGRAM_BINS) && meta.frames == 1,
          "history bridged bypass");
    CHECK(engine_transport_seek(engine, 1000), "spectrogram seek");
    CHECK(!engine_get_fx_spectrogram_snapshot(engine, &meta, rows, ENGINE_SPECTROGRAM_HISTORY,
                                              ENGINE_SPECTROGRAM_BINS),
          "stale spectrogram epoch");
    atomic_store(&engine->spectrogram_running, false);
    SDL_WaitThread(thread, NULL);
    engine_destroy(engine);
}

// Runs deterministic calibration and actual consumer publication without opening an audio device.
int main(void) {
    calibration();
    prepared_parity();
    spectrum_worker();
    spectrogram_worker();
    puts("analysis_calibration_test: success (tones, endpoints, power mean, real workers, gaps, epochs, mid, "
         "insert metadata)");
    return 0;
}
