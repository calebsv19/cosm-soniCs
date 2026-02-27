#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/panes.h"

// Initializes DAW frame invalidation state for a new app lifetime.
void daw_render_invalidation_init(void);
// Marks one pane dirty and records frame invalidation reasons.
void daw_invalidate_pane(Pane* pane, uint32_t reason_bits);
// Marks all panes dirty and records frame invalidation reasons.
void daw_invalidate_all(Pane* panes, int pane_count, uint32_t reason_bits);
// Requests a full redraw with reason bits without targeting specific panes.
void daw_request_full_redraw(uint32_t reason_bits);
// Returns true when any frame invalidation is pending.
bool daw_has_frame_invalidation(void);
// Consumes pending frame invalidation and advances frame id on success.
bool daw_consume_frame_invalidation(uint32_t* out_reason_bits,
                                    bool* out_full_redraw,
                                    uint64_t* out_frame_id);
