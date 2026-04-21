#include "ui/kit_viz_meter_adapter.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

static int nearly_equal(float a, float b) {
    return fabsf(a - b) < 0.001f;
}

static void test_line_plot_deterministic(void) {
    const float samples[] = {0.0f, 0.5f, 1.0f};
    SDL_Rect rect = {10, 20, 100, 50};
    DawKitVizMeterPlotRange range = {0.0f, 1.0f};
    KitVizVecSegment segments[8];
    size_t segment_count = 0;

    CoreResult r = daw_kit_viz_meter_plot_line_from_y_samples(samples,
                                                               3,
                                                               &rect,
                                                               range,
                                                               segments,
                                                               8,
                                                               &segment_count);
    assert(r.code == CORE_OK);
    assert(segment_count == 2);
    assert(nearly_equal(segments[0].x0, 10.0f));
    assert(nearly_equal(segments[0].x1, 59.5f));
    assert(nearly_equal(segments[1].x1, 109.0f));
    assert(nearly_equal(segments[0].y0, 69.0f));
    assert(nearly_equal(segments[1].y1, 20.0f));
}

static void test_line_plot_invalid_inputs(void) {
    const float samples[] = {0.0f, 1.0f};
    SDL_Rect rect = {0, 0, 10, 10};
    DawKitVizMeterPlotRange range = {0.0f, 1.0f};
    KitVizVecSegment segments[4];
    size_t segment_count = 0;

    CoreResult bad_rect = daw_kit_viz_meter_plot_line_from_y_samples(samples,
                                                                      2,
                                                                      &(SDL_Rect){0, 0, 0, 10},
                                                                      range,
                                                                      segments,
                                                                      4,
                                                                      &segment_count);
    assert(bad_rect.code == CORE_ERR_INVALID_ARG);

    CoreResult bad_capacity = daw_kit_viz_meter_plot_line_from_y_samples(samples,
                                                                          2,
                                                                          &rect,
                                                                          range,
                                                                          segments,
                                                                          0,
                                                                          &segment_count);
    assert(bad_capacity.code == CORE_ERR_INVALID_ARG);

    CoreResult nan_sample = daw_kit_viz_meter_plot_line_from_y_samples((float[]){0.0f, NAN},
                                                                        2,
                                                                        &rect,
                                                                        range,
                                                                        segments,
                                                                        4,
                                                                        &segment_count);
    assert(nan_sample.code == CORE_ERR_INVALID_ARG);
}

static void test_line_plot_fixed_slots_mapping(void) {
    const float samples[] = {0.0f, 0.5f, 1.0f};
    SDL_Rect rect = {10, 20, 100, 50};
    DawKitVizMeterPlotRange range = {0.0f, 1.0f};
    KitVizVecSegment segments[8];
    size_t segment_count = 0;

    CoreResult r = daw_kit_viz_meter_plot_line_from_y_samples_fixed_slots(samples,
                                                                           3,
                                                                           5,
                                                                           &rect,
                                                                           range,
                                                                           segments,
                                                                           8,
                                                                           &segment_count);
    assert(r.code == CORE_OK);
    assert(segment_count == 2);
    assert(nearly_equal(segments[0].x0, 10.0f));
    assert(nearly_equal(segments[0].x1, 34.75f));
    assert(nearly_equal(segments[1].x1, 59.5f));
    assert(nearly_equal(segments[0].y0, 69.0f));
    assert(nearly_equal(segments[1].y1, 20.0f));
}

static void test_scope_plot_deterministic(void) {
    const float xs[] = {-1.0f, 0.0f, 1.0f};
    const float ys[] = {-1.0f, 0.0f, 1.0f};
    SDL_Rect rect = {100, 200, 80, 60};
    KitVizVecSegment segments[8];
    size_t segment_count = 0;

    CoreResult r = daw_kit_viz_meter_plot_scope_segments(xs,
                                                          ys,
                                                          3,
                                                          &rect,
                                                          DAW_KIT_VIZ_METER_SCOPE_LEFT_RIGHT,
                                                          1.0f,
                                                          segments,
                                                          8,
                                                          &segment_count);
    assert(r.code == CORE_OK);
    assert(segment_count == 2);
    assert(nearly_equal(segments[0].x0, 100.0f));
    assert(nearly_equal(segments[0].y0, 260.0f));
    assert(nearly_equal(segments[1].x1, 180.0f));
    assert(nearly_equal(segments[1].y1, 200.0f));
}

static void test_scope_mid_side_transform(void) {
    const float xs[] = {1.0f, -1.0f};
    const float ys[] = {-1.0f, 1.0f};
    SDL_Rect rect = {0, 0, 100, 100};
    KitVizVecSegment segments[4];
    size_t segment_count = 0;

    CoreResult r = daw_kit_viz_meter_plot_scope_segments(xs,
                                                          ys,
                                                          2,
                                                          &rect,
                                                          DAW_KIT_VIZ_METER_SCOPE_MID_SIDE,
                                                          1.0f,
                                                          segments,
                                                          4,
                                                          &segment_count);
    assert(r.code == CORE_OK);
    assert(segment_count == 1);
    assert(nearly_equal(segments[0].x0, 50.0f));
    assert(nearly_equal(segments[0].y0, 0.0f));
    assert(nearly_equal(segments[0].x1, 50.0f));
    assert(nearly_equal(segments[0].y1, 100.0f));
}

static void test_spectrogram_palette_and_range(void) {
    const float frames[] = {
        -60.0f, 0.0f,
        -30.0f, -30.0f
    };
    uint8_t rgba_wb[4u * 2u * 2u];
    uint8_t rgba_bw[4u * 2u * 2u];
    CoreResult r_wb = daw_kit_viz_meter_build_spectrogram_rgba(frames,
                                                                2,
                                                                2,
                                                                2,
                                                                -60.0f,
                                                                0.0f,
                                                                DAW_KIT_VIZ_METER_SPECTROGRAM_WHITE_BLACK,
                                                                rgba_wb,
                                                                sizeof(rgba_wb));
    CoreResult r_bw = daw_kit_viz_meter_build_spectrogram_rgba(frames,
                                                                2,
                                                                2,
                                                                2,
                                                                -60.0f,
                                                                0.0f,
                                                                DAW_KIT_VIZ_METER_SPECTROGRAM_BLACK_WHITE,
                                                                rgba_bw,
                                                                sizeof(rgba_bw));
    assert(r_wb.code == CORE_OK);
    assert(r_bw.code == CORE_OK);

    // y is flipped: (frame0, bin0) => y=1 x=0.
    size_t low_idx = ((size_t)1 * 2u + 0u) * 4u;
    // (frame0, bin1) => y=0 x=0.
    size_t high_idx = ((size_t)0 * 2u + 0u) * 4u;

    // White/black mode: low dB brighter than high dB.
    assert(rgba_wb[low_idx] > rgba_wb[high_idx]);
    // Black/white mode: low dB darker than high dB.
    assert(rgba_bw[low_idx] < rgba_bw[high_idx]);
}

static void test_spectrogram_age_fade(void) {
    const float frames[] = {
        -30.0f,
        -30.0f,
        -30.0f,
        -30.0f
    };
    uint8_t rgba[4u * 4u * 1u];
    CoreResult r = daw_kit_viz_meter_build_spectrogram_rgba(frames,
                                                             4,
                                                             1,
                                                             4,
                                                             -60.0f,
                                                             0.0f,
                                                             DAW_KIT_VIZ_METER_SPECTROGRAM_BLACK_WHITE,
                                                             rgba,
                                                             sizeof(rgba));
    assert(r.code == CORE_OK);
    size_t newest = ((size_t)0 * 4u + 0u) * 4u;
    size_t oldest = ((size_t)0 * 4u + 3u) * 4u;
    assert(rgba[newest] > rgba[oldest]);
}

int main(void) {
    test_line_plot_deterministic();
    test_line_plot_invalid_inputs();
    test_line_plot_fixed_slots_mapping();
    test_scope_plot_deterministic();
    test_scope_mid_side_transform();
    test_spectrogram_palette_and_range();
    test_spectrogram_age_fade();
    puts("kit_viz_meter_adapter_test: ok");
    return 0;
}
