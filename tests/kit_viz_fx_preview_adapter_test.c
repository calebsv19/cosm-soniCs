#include "ui/kit_viz_fx_preview_adapter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char* msg) {
    fprintf(stderr, "kit_viz_fx_preview_adapter_test: %s\n", msg);
    exit(1);
}

static void expect(int cond, const char* msg) {
    if (!cond) fail(msg);
}

static int nearly_equal(float a, float b) {
    return fabsf(a - b) <= 1e-4f;
}

static void test_line_plot_deterministic(void) {
    const float samples[3] = {0.0f, 5.0f, 10.0f};
    const SDL_Rect rect = {10, 20, 5, 11};
    const DawKitVizPlotRange range = {0.0f, 10.0f};
    KitVizVecSegment segs[2];
    size_t count = 0;

    CoreResult r = daw_kit_viz_plot_line_from_y_samples(samples,
                                                         3,
                                                         &rect,
                                                         range,
                                                         segs,
                                                         2,
                                                         &count);
    expect(r.code == CORE_OK, "line deterministic: expected success");
    expect(count == 2, "line deterministic: expected segment count 2");

    expect(nearly_equal(segs[0].x0, 10.0f), "line deterministic: seg0 x0");
    expect(nearly_equal(segs[0].y0, 30.0f), "line deterministic: seg0 y0");
    expect(nearly_equal(segs[0].x1, 12.0f), "line deterministic: seg0 x1");
    expect(nearly_equal(segs[0].y1, 25.0f), "line deterministic: seg0 y1");
    expect(nearly_equal(segs[1].x0, 12.0f), "line deterministic: seg1 x0");
    expect(nearly_equal(segs[1].y0, 25.0f), "line deterministic: seg1 y0");
    expect(nearly_equal(segs[1].x1, 14.0f), "line deterministic: seg1 x1");
    expect(nearly_equal(segs[1].y1, 20.0f), "line deterministic: seg1 y1");
}

static void test_line_plot_bounds_and_guards(void) {
    const float samples[2] = {-100.0f, 100.0f};
    const SDL_Rect rect = {0, 0, 4, 4};
    const DawKitVizPlotRange range = {0.0f, 10.0f};
    KitVizVecSegment segs[1];
    size_t count = 0;

    CoreResult r = daw_kit_viz_plot_line_from_y_samples(samples,
                                                         2,
                                                         &rect,
                                                         range,
                                                         segs,
                                                         1,
                                                         &count);
    expect(r.code == CORE_OK, "line bounds: expected success");
    expect(count == 1, "line bounds: expected one segment");
    expect(nearly_equal(segs[0].y0, 3.0f), "line bounds: y0 must clamp to bottom");
    expect(nearly_equal(segs[0].y1, 0.0f), "line bounds: y1 must clamp to top");

    {
        float bad[2] = {0.0f, NAN};
        r = daw_kit_viz_plot_line_from_y_samples(bad, 2, &rect, range, segs, 1, &count);
        expect(r.code == CORE_ERR_INVALID_ARG, "line guard: NaN must fail");
    }
    {
        float bad[2] = {0.0f, INFINITY};
        r = daw_kit_viz_plot_line_from_y_samples(bad, 2, &rect, range, segs, 1, &count);
        expect(r.code == CORE_ERR_INVALID_ARG, "line guard: inf must fail");
    }
    {
        SDL_Rect empty = {0, 0, 0, 5};
        r = daw_kit_viz_plot_line_from_y_samples(samples, 2, &empty, range, segs, 1, &count);
        expect(r.code == CORE_ERR_INVALID_ARG, "line guard: empty rect must fail");
    }
    {
        CoreResult small = daw_kit_viz_plot_line_from_y_samples(samples, 2, &rect, range, segs, 0, &count);
        expect(small.code == CORE_ERR_INVALID_ARG, "line guard: small segment buffer must fail");
    }
    {
        DawKitVizPlotRange bad_range = {0.0f, NAN};
        CoreResult bad = daw_kit_viz_plot_line_from_y_samples(samples, 2, &rect, bad_range, segs, 1, &count);
        expect(bad.code == CORE_ERR_INVALID_ARG, "line guard: non-finite range must fail");
    }
}

static void test_line_plot_flat_and_degenerate_range(void) {
    const float samples[4] = {3.0f, 3.0f, 3.0f, 3.0f};
    const SDL_Rect rect = {4, 10, 7, 9};
    const DawKitVizPlotRange range = {3.0f, 3.0f};
    KitVizVecSegment segs[3];
    size_t count = 0;

    CoreResult r = daw_kit_viz_plot_line_from_y_samples(samples,
                                                         4,
                                                         &rect,
                                                         range,
                                                         segs,
                                                         3,
                                                         &count);
    expect(r.code == CORE_OK, "line flat: expected success");
    expect(count == 3, "line flat: expected segment count 3");
    for (size_t i = 0; i < count; ++i) {
        expect(nearly_equal(segs[i].y0, 14.0f), "line flat: y0 midpoint expected");
        expect(nearly_equal(segs[i].y1, 14.0f), "line flat: y1 midpoint expected");
    }
}

static void test_envelope_deterministic_and_ranges(void) {
    const float mins[3] = {-1.0f, -0.5f, -0.25f};
    const float maxs[3] = {1.0f, 0.5f, 0.75f};
    const SDL_Rect rect = {0, 0, 5, 5};
    const DawKitVizPlotRange range = {-1.0f, 1.0f};
    KitVizVecSegment segs[3];
    size_t count = 0;

    CoreResult r = daw_kit_viz_plot_envelope_from_min_max(mins,
                                                           maxs,
                                                           3,
                                                           &rect,
                                                           range,
                                                           segs,
                                                           3,
                                                           &count);
    expect(r.code == CORE_OK, "envelope deterministic: expected success");
    expect(count == 3, "envelope deterministic: expected segment count 3");

    expect(nearly_equal(segs[0].x0, 0.0f) && nearly_equal(segs[0].x1, 0.0f),
           "envelope deterministic: seg0 x");
    expect(nearly_equal(segs[1].x0, 2.0f) && nearly_equal(segs[1].x1, 2.0f),
           "envelope deterministic: seg1 x");
    expect(nearly_equal(segs[2].x0, 4.0f) && nearly_equal(segs[2].x1, 4.0f),
           "envelope deterministic: seg2 x");

    for (size_t i = 0; i < count; ++i) {
        expect(segs[i].x0 >= (float)rect.x && segs[i].x0 <= (float)(rect.x + rect.w - 1),
               "envelope ranges: x in rect");
        expect(segs[i].y0 >= (float)rect.y && segs[i].y0 <= (float)(rect.y + rect.h - 1),
               "envelope ranges: y0 in rect");
        expect(segs[i].y1 >= (float)rect.y && segs[i].y1 <= (float)(rect.y + rect.h - 1),
               "envelope ranges: y1 in rect");
        expect(segs[i].y0 >= segs[i].y1, "envelope ranges: segment must be top-to-bottom");
    }
}

static void test_envelope_guards(void) {
    const float mins[2] = {0.0f, 0.0f};
    const float maxs[2] = {1.0f, 1.0f};
    const SDL_Rect rect = {0, 0, 4, 4};
    const DawKitVizPlotRange range = {0.0f, 1.0f};
    KitVizVecSegment segs[2];
    size_t count = 0;

    {
        float bad_maxs[2] = {1.0f, NAN};
        CoreResult r = daw_kit_viz_plot_envelope_from_min_max(mins, bad_maxs, 2, &rect, range, segs, 2, &count);
        expect(r.code == CORE_ERR_INVALID_ARG, "envelope guard: NaN must fail");
    }
    {
        SDL_Rect empty = {0, 0, 4, 0};
        CoreResult r = daw_kit_viz_plot_envelope_from_min_max(mins, maxs, 2, &empty, range, segs, 2, &count);
        expect(r.code == CORE_ERR_INVALID_ARG, "envelope guard: empty rect must fail");
    }
    {
        DawKitVizPlotRange bad_range = {NAN, 1.0f};
        CoreResult r = daw_kit_viz_plot_envelope_from_min_max(mins, maxs, 2, &rect, bad_range, segs, 2, &count);
        expect(r.code == CORE_ERR_INVALID_ARG, "envelope guard: bad range must fail");
    }
    {
        CoreResult small = daw_kit_viz_plot_envelope_from_min_max(mins, maxs, 2, &rect, range, segs, 1, &count);
        expect(small.code == CORE_ERR_INVALID_ARG, "envelope guard: small segment buffer must fail");
    }
}

static void test_envelope_single_point_and_swapped_input(void) {
    {
        const float mins[1] = {0.9f};
        const float maxs[1] = {0.1f};
        const SDL_Rect rect = {2, 2, 10, 10};
        const DawKitVizPlotRange range = {0.0f, 1.0f};
        KitVizVecSegment segs[1];
        size_t count = 0;
        CoreResult r = daw_kit_viz_plot_envelope_from_min_max(mins, maxs, 1, &rect, range, segs, 1, &count);
        expect(r.code == CORE_OK, "envelope single-point: expected success");
        expect(count == 1, "envelope single-point: expected count 1");
        expect(nearly_equal(segs[0].x0, 2.0f), "envelope single-point: x at rect left");
        expect(nearly_equal(segs[0].x1, 2.0f), "envelope single-point: x1 at rect left");
        expect(segs[0].y0 >= segs[0].y1, "envelope single-point: sorted y");
        expect(nearly_equal(segs[0].y0, 10.1f), "envelope single-point: upper y from max(min,max)");
        expect(nearly_equal(segs[0].y1, 2.9f), "envelope single-point: lower y from min(min,max)");
    }
    {
        const float mins[3] = {0.8f, 0.2f, 0.6f};
        const float maxs[3] = {0.1f, 0.1f, 0.4f};
        const SDL_Rect rect = {0, 0, 6, 6};
        const DawKitVizPlotRange range = {0.0f, 1.0f};
        KitVizVecSegment segs[3];
        size_t count = 0;
        CoreResult r = daw_kit_viz_plot_envelope_from_min_max(mins, maxs, 3, &rect, range, segs, 3, &count);
        expect(r.code == CORE_OK, "envelope swapped: expected success");
        expect(count == 3, "envelope swapped: expected count 3");
        for (size_t i = 0; i < count; ++i) {
            expect(segs[i].y0 >= segs[i].y1, "envelope swapped: y ordering must be corrected");
        }
    }
}

int main(void) {
    test_line_plot_deterministic();
    test_line_plot_bounds_and_guards();
    test_line_plot_flat_and_degenerate_range();
    test_envelope_deterministic_and_ranges();
    test_envelope_guards();
    test_envelope_single_point_and_swapped_input();
    puts("kit_viz_fx_preview_adapter_test: success");
    return 0;
}
