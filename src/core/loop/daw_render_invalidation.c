#include "core/loop/daw_render_invalidation.h"

// Stores frame invalidation state for the DAW UI render pipeline.
typedef struct DawRenderInvalidationState {
    bool frame_invalidated;
    bool full_redraw_required;
    uint32_t invalidation_reasons;
    uint64_t frame_counter;
} DawRenderInvalidationState;

static DawRenderInvalidationState g_state;

void daw_render_invalidation_init(void) {
    g_state.frame_invalidated = true;
    g_state.full_redraw_required = true;
    g_state.invalidation_reasons = DAW_RENDER_INVALIDATION_LAYOUT;
    g_state.frame_counter = 0;
}

void daw_invalidate_pane(Pane* pane, uint32_t reason_bits) {
    if (!pane || reason_bits == DAW_RENDER_INVALIDATION_NONE) {
        return;
    }
    pane_mark_dirty(pane, reason_bits);
    g_state.frame_invalidated = true;
    g_state.invalidation_reasons |= reason_bits;
}

void daw_invalidate_all(Pane* panes, int pane_count, uint32_t reason_bits) {
    if (!panes || pane_count <= 0 || reason_bits == DAW_RENDER_INVALIDATION_NONE) {
        return;
    }
    for (int i = 0; i < pane_count; ++i) {
        daw_invalidate_pane(&panes[i], reason_bits);
    }
}

void daw_request_full_redraw(uint32_t reason_bits) {
    if (reason_bits == DAW_RENDER_INVALIDATION_NONE) {
        return;
    }
    g_state.frame_invalidated = true;
    g_state.full_redraw_required = true;
    g_state.invalidation_reasons |= reason_bits;
}

bool daw_has_frame_invalidation(void) {
    return g_state.frame_invalidated || g_state.full_redraw_required;
}

bool daw_consume_frame_invalidation(uint32_t* out_reason_bits,
                                    bool* out_full_redraw,
                                    uint64_t* out_frame_id) {
    if (!daw_has_frame_invalidation()) {
        return false;
    }

    if (out_reason_bits) {
        *out_reason_bits = g_state.invalidation_reasons;
    }
    if (out_full_redraw) {
        *out_full_redraw = g_state.full_redraw_required;
    }

    g_state.frame_counter++;
    if (out_frame_id) {
        *out_frame_id = g_state.frame_counter;
    }

    g_state.frame_invalidated = false;
    g_state.full_redraw_required = false;
    g_state.invalidation_reasons = DAW_RENDER_INVALIDATION_NONE;
    return true;
}
