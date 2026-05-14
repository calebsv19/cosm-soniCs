#include "kit_render.h"

#include <string.h>

int kit_render_text_zoom_percent(const KitRenderContext *ctx) {
    (void)ctx;
    return 100;
}

CoreResult kit_render_measure_text(const KitRenderContext *ctx,
                                   CoreFontRoleId font_role,
                                   CoreFontTextSizeTier text_tier,
                                   const char *text,
                                   KitRenderTextMetrics *out_metrics) {
    (void)ctx;
    (void)font_role;
    (void)text_tier;
    if (!out_metrics) {
        return (CoreResult){ CORE_ERR_INVALID_ARG, "null metrics" };
    }
    out_metrics->width_px = (float)((text && text[0]) ? strlen(text) * 7u : 0u);
    out_metrics->height_px = 12.0f;
    return core_result_ok();
}
