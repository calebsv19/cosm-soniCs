#include "ui/effects_panel_state_helpers.h"

#include "ui/font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void eq_curve_set_defaults(EqCurveState* curve) {
    if (!curve) {
        return;
    }
    SDL_zero(*curve);
    curve->low_cut.enabled = false;
    curve->low_cut.freq_hz = 80.0f;
    curve->low_cut.slope = 12.0f;
    curve->high_cut.enabled = false;
    curve->high_cut.freq_hz = 12000.0f;
    curve->high_cut.slope = 12.0f;
    curve->selected_band = -1;
    curve->selected_handle = EQ_CURVE_HANDLE_NONE;
    curve->hover_band = -1;
    curve->hover_handle = EQ_CURVE_HANDLE_NONE;
    curve->hover_toggle_band = -1;
    curve->hover_toggle_low = false;
    curve->hover_toggle_high = false;
    for (int i = 0; i < 4; ++i) {
        curve->bands[i].enabled = true;
        curve->bands[i].gain_db = 0.0f;
        curve->bands[i].q_width = 1.0f;
    }
    curve->bands[0].freq_hz = 120.0f;
    curve->bands[1].freq_hz = 500.0f;
    curve->bands[2].freq_hz = 2000.0f;
    curve->bands[3].freq_hz = 8000.0f;
}

void effects_panel_preview_reset(EffectsPanelPreviewSlotState* preview, FxInstId fx_id, bool open_default) {
    if (!preview) {
        return;
    }
    preview->fx_id = fx_id;
    preview->open = open_default;
    preview->history_write = 0;
    preview->history_filled = false;
    for (int i = 0; i < FX_PANEL_PREVIEW_HISTORY; ++i) {
        preview->history[i] = 0.0f;
    }
}

bool effects_panel_slot_supports_preview(FxTypeId type_id) {
    switch (type_id) {
        case 7u:
        case 20u:
        case 21u:
        case 22u:
        case 23u:
        case 60u:
        case 61u:
        case 62u:
        case 63u:
        case 64u:
        case 65u:
            return true;
        default:
            return false;
    }
}

void effects_panel_sync_meter_modes_from_slot(EffectsPanelState* panel, const FxSlotUIState* slot) {
    if (!panel || !slot) {
        return;
    }
    if (slot->type_id == 102u && slot->param_count > 0) {
        int mode = (int)lroundf(slot->param_values[0]);
        panel->meter_scope_mode = mode == 0 ? FX_METER_SCOPE_MID_SIDE : FX_METER_SCOPE_LEFT_RIGHT;
        return;
    }
    if (slot->type_id == 104u && slot->param_count > 0) {
        int mode = (int)lroundf(slot->param_values[0]);
        if (mode <= 0) {
            panel->meter_lufs_mode = FX_METER_LUFS_INTEGRATED;
        } else if (mode == 1) {
            panel->meter_lufs_mode = FX_METER_LUFS_SHORT_TERM;
        } else {
            panel->meter_lufs_mode = FX_METER_LUFS_MOMENTARY;
        }
        return;
    }
    if (slot->type_id == 105u && slot->param_count > 2) {
        int mode = (int)lroundf(slot->param_values[2]);
        if (mode <= 0) {
            panel->meter_spectrogram_mode = FX_METER_SPECTROGRAM_WHITE_BLACK;
        } else if (mode == 1) {
            panel->meter_spectrogram_mode = FX_METER_SPECTROGRAM_BLACK_WHITE;
        } else {
            panel->meter_spectrogram_mode = FX_METER_SPECTROGRAM_HEAT;
        }
    }
}

void effects_panel_fill_fallback_param_spec(EffectParamSpec* spec, const char* name, float def_value) {
    if (!spec) {
        return;
    }
    SDL_zero(*spec);
    spec->id = name;
    spec->display_name = name;
    spec->type = FX_PARAM_TYPE_FLOAT;
    spec->unit = FX_PARAM_UNIT_GENERIC;
    spec->curve = FX_PARAM_CURVE_LINEAR;
    spec->ui_hint = FX_PARAM_UI_SLIDER;
    spec->flags = FX_PARAM_FLAG_AUTOMATABLE;
    spec->min_value = 0.0f;
    spec->max_value = 1.0f;
    spec->default_value = def_value;
    if (def_value < spec->min_value) {
        spec->min_value = def_value;
    }
    if (def_value > spec->max_value) {
        spec->max_value = def_value;
    }
    if (fabsf(spec->max_value - spec->min_value) < 1e-6f) {
        spec->max_value = spec->min_value + 1.0f;
    }
}

typedef struct {
    const char* name;
    FxTypeId    id_min;
    FxTypeId    id_max;
} FxCategorySpec;

static const FxCategorySpec kCategorySpecs[] = {
    {"Basics",        1u,  19u},
    {"Dynamics",      20u, 29u},
    {"EQ",            30u, 39u},
    {"Filter & Tone", 40u, 49u},
    {"Delay",         50u, 59u},
    {"Distortion",    60u, 69u},
    {"Modulation",    70u, 79u},
    {"Reverb",        90u, 99u},
    {"Metering",      100u, 109u},
};

void effects_panel_build_categories(EffectsPanelState* panel) {
    if (!panel) {
        return;
    }

    panel->category_count = 0;
    bool assigned[FX_PANEL_MAX_TYPES];
    memset(assigned, 0, sizeof(assigned));

    int spec_count = (int)(sizeof(kCategorySpecs) / sizeof(kCategorySpecs[0]));
    for (int s = 0; s < spec_count && panel->category_count < FX_PANEL_MAX_CATEGORIES; ++s) {
        const FxCategorySpec* spec = &kCategorySpecs[s];
        FxCategoryUIInfo* cat = &panel->categories[panel->category_count];
        SDL_zero(*cat);
        strncpy(cat->name, spec->name, sizeof(cat->name) - 1);
        cat->name[sizeof(cat->name) - 1] = '\0';

        for (int t = 0; t < panel->type_count; ++t) {
            FxTypeId type_id = panel->types[t].type_id;
            if (type_id >= spec->id_min && type_id <= spec->id_max) {
                if (cat->type_count < FX_PANEL_MAX_TYPES) {
                    cat->type_indices[cat->type_count++] = t;
                    assigned[t] = true;
                }
            }
        }

        if (cat->type_count > 0) {
            panel->category_count++;
        }
    }

    FxCategoryUIInfo others;
    SDL_zero(others);
    strncpy(others.name, "Other", sizeof(others.name) - 1);
    others.name[sizeof(others.name) - 1] = '\0';

    for (int t = 0; t < panel->type_count; ++t) {
        if (!assigned[t] && others.type_count < FX_PANEL_MAX_TYPES) {
            others.type_indices[others.type_count++] = t;
        }
    }

    if (others.type_count > 0 && panel->category_count < FX_PANEL_MAX_CATEGORIES) {
        panel->categories[panel->category_count++] = others;
    }

    if (panel->category_count == 0 && panel->type_count > 0) {
        FxCategoryUIInfo all;
        SDL_zero(all);
        strncpy(all.name, "All Effects", sizeof(all.name) - 1);
        all.name[sizeof(all.name) - 1] = '\0';
        for (int t = 0; t < panel->type_count && t < FX_PANEL_MAX_TYPES; ++t) {
            all.type_indices[all.type_count++] = t;
        }
        panel->categories[panel->category_count++] = all;
    }

    if (panel->category_count == 0) {
        panel->active_category_index = -1;
    } else if (panel->active_category_index >= panel->category_count) {
        panel->active_category_index = -1;
    }
}

void effects_panel_eq_curve_set_defaults(EqCurveState* curve) {
    eq_curve_set_defaults(curve);
}

void effects_panel_eq_curve_copy_settings(EqCurveState* dst, const EqCurveState* src) {
    if (!dst || !src) {
        return;
    }
    dst->low_cut = src->low_cut;
    dst->high_cut = src->high_cut;
    for (int i = 0; i < 4; ++i) {
        dst->bands[i] = src->bands[i];
    }
}

void effects_panel_eq_curve_clear_transient(EqCurveState* curve) {
    if (!curve) {
        return;
    }
    curve->selected_band = -1;
    curve->selected_handle = EQ_CURVE_HANDLE_NONE;
    curve->hover_band = -1;
    curve->hover_handle = EQ_CURVE_HANDLE_NONE;
    curve->hover_toggle_band = -1;
    curve->hover_toggle_low = false;
    curve->hover_toggle_high = false;
}

void effects_panel_eq_curve_store_for_view(EffectsPanelState* panel,
                                           EffectsPanelEqDetailView view_mode,
                                           EffectsPanelTarget target,
                                           int track_index) {
    if (!panel) {
        return;
    }
    if (view_mode == EQ_DETAIL_VIEW_TRACK &&
        target == FX_PANEL_TARGET_TRACK &&
        track_index >= 0 &&
        track_index < panel->eq_curve_tracks_count &&
        panel->eq_curve_tracks) {
        effects_panel_eq_curve_copy_settings(&panel->eq_curve_tracks[track_index], &panel->eq_curve);
    } else {
        effects_panel_eq_curve_copy_settings(&panel->eq_curve_master, &panel->eq_curve);
    }
}

void effects_panel_eq_curve_load_for_view(EffectsPanelState* panel,
                                          EffectsPanelEqDetailView view_mode,
                                          EffectsPanelTarget target,
                                          int track_index) {
    if (!panel) {
        return;
    }
    if (view_mode == EQ_DETAIL_VIEW_TRACK &&
        target == FX_PANEL_TARGET_TRACK &&
        track_index >= 0 &&
        track_index < panel->eq_curve_tracks_count &&
        panel->eq_curve_tracks) {
        effects_panel_eq_curve_copy_settings(&panel->eq_curve, &panel->eq_curve_tracks[track_index]);
    } else {
        effects_panel_eq_curve_copy_settings(&panel->eq_curve, &panel->eq_curve_master);
    }
    effects_panel_eq_curve_clear_transient(&panel->eq_curve);
}

void effects_panel_ensure_last_open_tracks(EffectsPanelState* panel, int track_count) {
    if (!panel) {
        return;
    }
    if (track_count <= 0) {
        free(panel->last_open_track_fx_ids);
        panel->last_open_track_fx_ids = NULL;
        panel->last_open_track_fx_count = 0;
        return;
    }
    if (panel->last_open_track_fx_ids && panel->last_open_track_fx_count == track_count) {
        return;
    }
    FxInstId* next = (FxInstId*)calloc((size_t)track_count, sizeof(FxInstId));
    if (!next) {
        return;
    }
    int copy_count = panel->last_open_track_fx_count < track_count ? panel->last_open_track_fx_count : track_count;
    for (int i = 0; i < copy_count; ++i) {
        next[i] = panel->last_open_track_fx_ids[i];
    }
    free(panel->last_open_track_fx_ids);
    panel->last_open_track_fx_ids = next;
    panel->last_open_track_fx_count = track_count;
}

void effects_panel_ensure_eq_curve_tracks(AppState* state, int track_count) {
    if (!state) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (track_count <= 0) {
        free(panel->eq_curve_tracks);
        panel->eq_curve_tracks = NULL;
        panel->eq_curve_tracks_count = 0;
        return;
    }
    if (panel->eq_curve_tracks && panel->eq_curve_tracks_count == track_count) {
        return;
    }
    EqCurveState* next = (EqCurveState*)calloc((size_t)track_count, sizeof(EqCurveState));
    if (!next) {
        return;
    }
    int copy_count = panel->eq_curve_tracks_count < track_count ? panel->eq_curve_tracks_count : track_count;
    for (int i = 0; i < copy_count; ++i) {
        next[i] = panel->eq_curve_tracks[i];
    }
    for (int i = copy_count; i < track_count; ++i) {
        eq_curve_set_defaults(&next[i]);
    }
    free(panel->eq_curve_tracks);
    panel->eq_curve_tracks = next;
    panel->eq_curve_tracks_count = track_count;
}
