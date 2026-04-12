#include "ui/effects_panel.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/effects_panel_state_helpers.h"

#include <stdio.h>
#include <string.h>

static bool effects_panel_update_target(AppState* state) {
    if (!state) {
        return false;
    }
    EffectsPanelState* panel = &state->effects_panel;
    EffectsPanelTarget prev_target = panel->target;
    int prev_track = panel->target_track_index;
    char prev_label[sizeof(panel->target_label)];
    strncpy(prev_label, panel->target_label, sizeof(prev_label) - 1);
    prev_label[sizeof(prev_label) - 1] = '\0';

    int track_count = 0;
    if (state->engine) {
        track_count = engine_get_track_count(state->engine);
        effects_panel_ensure_eq_curve_tracks(state, track_count);
        effects_panel_ensure_last_open_tracks(panel, track_count);
    }

    FxInstId prev_open_id = 0;
    if (panel->list_open_slot_index >= 0 && panel->list_open_slot_index < panel->chain_count) {
        prev_open_id = panel->chain[panel->list_open_slot_index].id;
    }

    panel->target = FX_PANEL_TARGET_MASTER;
    panel->target_track_index = -1;
    const char* label = "Master";
    char label_buf[sizeof(panel->target_label)];
    label_buf[0] = '\0';

    if (state->engine) {
        int sel_track = state->selected_track_index;
        int track_total = engine_get_track_count(state->engine);
        if (sel_track >= 0 && sel_track < track_total) {
            panel->target = FX_PANEL_TARGET_TRACK;
            panel->target_track_index = sel_track;
            const EngineTrack* tracks = engine_get_tracks(state->engine);
            if (tracks && sel_track >= 0 && sel_track < track_total) {
                const EngineTrack* track = &tracks[sel_track];
                if (track->name[0] != '\0') {
                    label = track->name;
                } else {
                    snprintf(label_buf, sizeof(label_buf), "Track %d", sel_track + 1);
                    label = label_buf;
                }
            }
        }
    }

    strncpy(panel->target_label, label, sizeof(panel->target_label) - 1);
    panel->target_label[sizeof(panel->target_label) - 1] = '\0';

    bool label_changed = strncmp(prev_label, panel->target_label, sizeof(prev_label)) != 0;
    bool target_changed = label_changed || prev_target != panel->target || prev_track != panel->target_track_index;
    if (target_changed) {
        if (prev_open_id != 0) {
            if (prev_target == FX_PANEL_TARGET_MASTER) {
                panel->last_open_master_fx_id = prev_open_id;
            } else if (prev_target == FX_PANEL_TARGET_TRACK &&
                       prev_track >= 0 &&
                       prev_track < panel->last_open_track_fx_count) {
                panel->last_open_track_fx_ids[prev_track] = prev_open_id;
            }
        }
        effects_panel_eq_curve_store_for_view(panel, panel->eq_detail.view_mode, prev_target, prev_track);
        if (panel->eq_detail.view_mode == EQ_DETAIL_VIEW_TRACK &&
            (panel->target != FX_PANEL_TARGET_TRACK || panel->target_track_index < 0)) {
            panel->eq_detail.view_mode = EQ_DETAIL_VIEW_MASTER;
        }
        effects_panel_eq_curve_load_for_view(panel, panel->eq_detail.view_mode, panel->target, panel->target_track_index);
        panel->list_open_slot_index = -1;
        panel->pending_open_fx_id = 0;
        if (panel->target == FX_PANEL_TARGET_MASTER) {
            panel->pending_open_fx_id = panel->last_open_master_fx_id;
        } else if (panel->target == FX_PANEL_TARGET_TRACK &&
                   panel->target_track_index >= 0 &&
                   panel->target_track_index < panel->last_open_track_fx_count) {
            panel->pending_open_fx_id = panel->last_open_track_fx_ids[panel->target_track_index];
        }
    }
    return target_changed;
}

void effects_panel_sync_from_engine(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    int track_count = engine_get_track_count(state->engine);
    effects_panel_ensure_eq_curve_tracks(state, track_count);
    effects_panel_ensure_last_open_tracks(panel, track_count);
    bool target_changed = effects_panel_update_target(state);
    FxInstId selected_id = 0;
    if (!target_changed &&
        panel->selected_slot_index >= 0 &&
        panel->selected_slot_index < panel->chain_count) {
        selected_id = panel->chain[panel->selected_slot_index].id;
    }
    FxInstId open_id = 0;
    if (panel->pending_open_fx_id != 0) {
        open_id = panel->pending_open_fx_id;
    } else if (!target_changed &&
               panel->list_open_slot_index >= 0 &&
               panel->list_open_slot_index < panel->chain_count) {
        open_id = panel->chain[panel->list_open_slot_index].id;
    }
    FxMasterSnapshot snap;
    bool ok = false;
    if (panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0) {
        ok = engine_fx_track_snapshot(state->engine, panel->target_track_index, &snap);
    } else {
        ok = engine_fx_master_snapshot(state->engine, &snap);
    }
    if (!ok) {
        panel->chain_count = 0;
        panel->selected_slot_index = -1;
        return;
    }
    panel->chain_count = snap.count;
    for (int i = 0; i < snap.count && i < FX_MASTER_MAX; ++i) {
        FxSlotUIState* slot = &panel->chain[i];
        SDL_zero(*slot);
        slot->id = snap.items[i].id;
        slot->type_id = snap.items[i].type;
        slot->enabled = snap.items[i].enabled;
        slot->param_count = snap.items[i].param_count;
        if (slot->param_count > FX_MAX_PARAMS) {
            slot->param_count = FX_MAX_PARAMS;
        }
        for (uint32_t p = 0; p < slot->param_count; ++p) {
            slot->param_values[p] = snap.items[i].params[p];
            slot->param_mode[p] = snap.items[i].param_mode[p];
            slot->param_beats[p] = snap.items[i].param_beats[p];
        }
        bool open_default = slot->type_id == 20u;
        if (panel->preview_slots[i].fx_id != slot->id) {
            effects_panel_preview_reset(&panel->preview_slots[i], slot->id, open_default);
        }
    }
    for (int i = snap.count; i < FX_MASTER_MAX; ++i) {
        panel->chain[i].id = 0;
        panel->chain[i].param_count = 0;
        if (panel->preview_slots[i].fx_id != 0) {
            effects_panel_preview_reset(&panel->preview_slots[i], 0, false);
        }
    }
    if (panel->highlighted_slot_index >= panel->chain_count) {
        panel->highlighted_slot_index = -1;
    }
    if (panel->hovered_toggle_slot_index >= panel->chain_count) {
        panel->hovered_toggle_slot_index = -1;
    }
    if (panel->active_slot_index >= panel->chain_count) {
        panel->active_slot_index = -1;
    }
    if (panel->selected_slot_index >= panel->chain_count) {
        panel->selected_slot_index = -1;
    }
    if (panel->list_open_slot_index >= panel->chain_count) {
        panel->list_open_slot_index = -1;
    }
    if (selected_id != 0) {
        for (int i = 0; i < panel->chain_count; ++i) {
            if (panel->chain[i].id == selected_id) {
                panel->selected_slot_index = i;
                break;
            }
        }
    }
    if (open_id != 0) {
        for (int i = 0; i < panel->chain_count; ++i) {
            if (panel->chain[i].id == open_id) {
                panel->list_open_slot_index = i;
                break;
            }
        }
    }
    panel->pending_open_fx_id = 0;
    if (panel->restore_pending) {
        int sel = panel->restore_selected_index;
        int open = panel->restore_open_index;
        if (sel < 0 || sel >= panel->chain_count) {
            sel = -1;
        }
        if (open < 0 || open >= panel->chain_count) {
            open = -1;
        }
        panel->selected_slot_index = sel;
        panel->list_open_slot_index = panel->view_mode == FX_PANEL_VIEW_LIST ? open : -1;
        panel->restore_pending = false;
    }

    bool previews_open = true;
    bool preview_found = false;
    for (int i = 0; i < panel->chain_count; ++i) {
        if (!effects_panel_slot_supports_preview(panel->chain[i].type_id)) {
            continue;
        }
        preview_found = true;
        if (!panel->preview_slots[i].open) {
            previews_open = false;
            break;
        }
    }
    panel->preview_all_open = preview_found && previews_open;

    if (panel->list_open_slot_index >= 0 && panel->list_open_slot_index < panel->chain_count) {
        FxInstId active_id = panel->chain[panel->list_open_slot_index].id;
        if (panel->target == FX_PANEL_TARGET_MASTER) {
            panel->last_open_master_fx_id = active_id;
        } else if (panel->target == FX_PANEL_TARGET_TRACK &&
                   panel->target_track_index >= 0 &&
                   panel->target_track_index < panel->last_open_track_fx_count) {
            panel->last_open_track_fx_ids[panel->target_track_index] = active_id;
        }
        if (panel->list_detail_mode == FX_LIST_DETAIL_METER) {
            effects_panel_sync_meter_modes_from_slot(panel, &panel->chain[panel->list_open_slot_index]);
        }
    }
}
