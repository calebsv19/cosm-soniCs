#ifndef UI_EFFECTS_PANEL_SLOT_LAYOUT_H
#define UI_EFFECTS_PANEL_SLOT_LAYOUT_H

#include "ui/effects_panel_slot.h"

struct EffectsPanelState;

// effects_slot_compute_layout calculates rects and scroll bounds for a slot's UI.
void effects_slot_compute_layout(struct EffectsPanelState* panel,
                                 int slot_index,
                                 const SDL_Rect* column_rect,
                                 int header_height,
                                 int inner_margin,
                                 int default_param_gap,
                                 EffectsSlotLayout* out_layout);

#endif
