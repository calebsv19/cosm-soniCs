#include "ui/effects_panel.h"

#include "app_state.h"
#include "engine/engine.h"
#include "effects/param_utils.h"
#include "ui/font.h"
#include "ui/layout.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define FX_PANEL_SLIDER_HEIGHT 18
#define FX_PANEL_OVERLAY_WIDTH 260
#define FX_PANEL_OVERLAY_HEADER_HEIGHT 34
#define FX_PANEL_OVERLAY_PADDING 8

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
};

static void zero_layout(EffectsPanelLayout* layout) {
    if (!layout) {
        return;
    }
    SDL_zero(*layout);
}

static void compute_list_layout(const EffectsPanelState* panel,
                                int content_x,
                                int content_y,
                                int content_w,
                                int content_h,
                                EffectsPanelLayout* layout) {
    if (!panel || !layout) {
        return;
    }
    int body_y = content_y + FX_PANEL_HEADER_HEIGHT;
    int body_h = content_h - FX_PANEL_HEADER_HEIGHT;
    if (body_h <= 0) {
        return;
    }

    int list_w = (int)((float)content_w * 0.18f);
    if (list_w < 140) list_w = 140;
    if (list_w < 0) list_w = 0;
    int gap = 12;
    if (list_w + gap > content_w) {
        gap = 6;
    }

    int max_name_w = 0;
    for (int i = 0; i < panel->chain_count; ++i) {
        const FxSlotUIState* slot = &panel->chain[i];
        const char* name = "Effect";
        for (int t = 0; t < panel->type_count; ++t) {
            if (panel->types[t].type_id == slot->type_id) {
                name = panel->types[t].name;
                break;
            }
        }
        int w = ui_measure_text_width(name, 1.3f);
        if (w > max_name_w) {
            max_name_w = w;
        }
    }
    int toggle_size = FX_PANEL_LIST_ROW_HEIGHT - 6;
    if (toggle_size < 10) toggle_size = 10;
    int min_needed = FX_PANEL_LIST_PAD * 3 + max_name_w + toggle_size + 16;
    if (min_needed > list_w) {
        list_w = min_needed;
    }
    if (list_w > content_w - 180) list_w = content_w - 180;
    if (list_w < 0) list_w = 0;

    int detail_w = content_w - list_w - gap;
    if (detail_w < 0) detail_w = 0;

    layout->list_rect = (SDL_Rect){content_x, body_y, list_w, body_h};
    layout->detail_rect = (SDL_Rect){content_x + list_w + gap, body_y, detail_w, body_h};

    layout->list_row_count = 0;
    int row_y = layout->list_rect.y + FX_PANEL_LIST_PAD;
    for (int i = 0; i < panel->chain_count && i < FX_MASTER_MAX; ++i) {
        SDL_Rect row = {layout->list_rect.x + FX_PANEL_LIST_PAD,
                        row_y,
                        layout->list_rect.w - FX_PANEL_LIST_PAD * 2,
                        FX_PANEL_LIST_ROW_HEIGHT};
        if (row.y + row.h > layout->list_rect.y + layout->list_rect.h - FX_PANEL_LIST_PAD) {
            break;
        }
        int toggle_size = FX_PANEL_LIST_ROW_HEIGHT - 6;
        if (toggle_size < 10) toggle_size = 10;
        SDL_Rect toggle_rect = {row.x + row.w - FX_PANEL_LIST_PAD - toggle_size,
                                row.y + (row.h - toggle_size) / 2,
                                toggle_size,
                                toggle_size};
        layout->list_row_rects[i] = row;
        layout->list_toggle_rects[i] = toggle_rect;
        layout->list_row_count++;
        row_y += FX_PANEL_LIST_ROW_HEIGHT + FX_PANEL_LIST_ROW_GAP;
    }
}

static void lower_copy(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; i + 1 < dst_size && src[i]; ++i) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static void derive_param_range(const char* name, float def_value, float* out_min, float* out_max) {
    float min_v = 0.0f;
    float max_v = 1.0f;
    char lower[64];
    lower_copy(lower, sizeof(lower), name);

    if (strstr(lower, "mix") || strstr(lower, "depth") || strstr(lower, "width") || strstr(lower, "blend")) {
        min_v = 0.0f;
        max_v = 1.0f;
    } else if (strstr(lower, "feedback")) {
        min_v = 0.0f;
        max_v = 0.95f;
    } else if (strstr(lower, "time_ms") || strstr(lower, "_ms")) {
        min_v = 0.0f;
        max_v = 2000.0f;
    } else if (strstr(lower, "time")) {
        min_v = 0.0f;
        max_v = 5.0f;
    } else if (strstr(lower, "freq") || strstr(lower, "hz")) {
        min_v = 20.0f;
        max_v = 20000.0f;
    } else if (strcmp(lower, "q") == 0 || strstr(lower, "reso")) {
        min_v = 0.1f;
        max_v = 20.0f;
    } else if (strstr(lower, "gain")) {
        min_v = 0.0f;
        max_v = 4.0f;
    } else if (strstr(lower, "threshold")) {
        min_v = -60.0f;
        max_v = 0.0f;
    } else if (strstr(lower, "ratio")) {
        min_v = 1.0f;
        max_v = 20.0f;
    } else if (strstr(lower, "attack")) {
        min_v = 0.1f;
        max_v = 500.0f;
    } else if (strstr(lower, "release")) {
        min_v = 10.0f;
        max_v = 2000.0f;
    } else if (strstr(lower, "drive") || strstr(lower, "amount")) {
        min_v = 0.0f;
        max_v = 2.5f;
    } else if (strstr(lower, "pan")) {
        min_v = -1.0f;
        max_v = 1.0f;
    } else if (strstr(lower, "level")) {
        min_v = 0.0f;
        max_v = 1.5f;
    }

    if (def_value < min_v) {
        min_v = def_value * 0.5f;
    }
    if (def_value > max_v) {
        max_v = def_value * 1.5f;
    }
    if (fabsf(max_v - min_v) < 1e-6f) {
        max_v = min_v + 1.0f;
    }

    if (out_min) *out_min = min_v;
    if (out_max) *out_max = max_v;
}

static void effects_panel_build_categories(EffectsPanelState* panel) {
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

    panel->target = FX_PANEL_TARGET_MASTER;
    panel->target_track_index = -1;
    const char* label = "Master";
    char label_buf[sizeof(panel->target_label)];
    label_buf[0] = '\0';

    if (state->engine) {
        int sel_track = state->selected_track_index;
        int track_count = engine_get_track_count(state->engine);
        if (sel_track >= 0 && sel_track < track_count) {
            panel->target = FX_PANEL_TARGET_TRACK;
            panel->target_track_index = sel_track;
            const EngineTrack* tracks = engine_get_tracks(state->engine);
            if (tracks && sel_track >= 0 && sel_track < track_count) {
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
    return label_changed || prev_target != panel->target || prev_track != panel->target_track_index;
}

static void draw_button(SDL_Renderer* renderer, const SDL_Rect* rect, bool highlighted, const char* label, float scale) {
    if (!renderer || !rect) {
        return;
    }
    SDL_Color base = {48, 52, 62, 255};
    SDL_Color highlight = {120, 160, 220, 255};
    SDL_Color border = {90, 95, 110, 255};
    SDL_Color text = {220, 220, 230, 255};

    SDL_Color fill = highlighted ? highlight : base;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);
    if (label) {
        int text_h = ui_font_line_height(scale);
        int text_x = rect->x + FX_PANEL_HEADER_BUTTON_PAD_X;
        int text_y = rect->y + (rect->h - text_h) / 2;
        ui_draw_text(renderer, text_x, text_y, label, text, scale);
    }
}

void effects_panel_init(AppState* state) {
    if (!state) {
        return;
    }
    SDL_zero(state->effects_panel);
    state->effects_panel.overlay_layer = FX_PANEL_OVERLAY_CLOSED;
    state->effects_panel.target = FX_PANEL_TARGET_MASTER;
    state->effects_panel.target_track_index = -1;
    strncpy(state->effects_panel.target_label, "Master", sizeof(state->effects_panel.target_label) - 1);
    state->effects_panel.target_label[sizeof(state->effects_panel.target_label) - 1] = '\0';
    state->effects_panel.view_mode = FX_PANEL_VIEW_STACK;
    state->effects_panel.hovered_category_index = -1;
    state->effects_panel.hovered_effect_index = -1;
    state->effects_panel.active_category_index = -1;
    state->effects_panel.highlighted_slot_index = -1;
    state->effects_panel.hovered_toggle_slot_index = -1;
    state->effects_panel.selected_slot_index = -1;
    state->effects_panel.focused = false;
    state->effects_panel.active_slot_index = -1;
    state->effects_panel.active_param_index = -1;
    state->effects_panel.list_open_slot_index = -1;
    state->effects_panel.list_last_click_ticks = 0;
    state->effects_panel.list_last_click_index = -1;
    state->effects_panel.restore_pending = false;
    state->effects_panel.restore_selected_index = -1;
    state->effects_panel.restore_open_index = -1;
    state->effects_panel.overlay_scroll_index = 0;
    state->effects_panel.title_last_click_ticks = 0;
    for (int i = 0; i < FX_MASTER_MAX; ++i) {
        effects_slot_reset_runtime(&state->effects_panel.slot_runtime[i]);
    }
    state->effects_panel.param_scroll_drag_slot = -1;
}

void effects_panel_refresh_catalog(AppState* state) {
    if (!state) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    if (!state->engine) {
        panel->type_count = 0;
        panel->category_count = 0;
        panel->initialized = true;
        panel->overlay_layer = FX_PANEL_OVERLAY_CLOSED;
        panel->overlay_scroll_index = 0;
        return;
    }
    const FxRegistryEntry* entries = NULL;
    int count = 0;
    if (!engine_fx_get_registry(state->engine, &entries, &count) || !entries) {
        panel->type_count = 0;
        panel->category_count = 0;
        panel->initialized = true;
        panel->overlay_layer = FX_PANEL_OVERLAY_CLOSED;
        panel->overlay_scroll_index = 0;
        return;
    }
    if (count > FX_PANEL_MAX_TYPES) {
        count = FX_PANEL_MAX_TYPES;
    }
    panel->type_count = count;
    for (int i = 0; i < count; ++i) {
        FxTypeUIInfo* info = &panel->types[i];
        SDL_zero(*info);
        info->type_id = entries[i].id;
        if (entries[i].name) {
            strncpy(info->name, entries[i].name, sizeof(info->name) - 1);
            info->name[sizeof(info->name) - 1] = '\0';
        }
        FxDesc desc = {0};
        if (engine_fx_registry_get_desc(state->engine, info->type_id, &desc)) {
            if (desc.name) {
                strncpy(info->name, desc.name, sizeof(info->name) - 1);
                info->name[sizeof(info->name) - 1] = '\0';
            }
            info->param_count = desc.num_params <= FX_MAX_PARAMS ? desc.num_params : FX_MAX_PARAMS;
            for (uint32_t p = 0; p < info->param_count; ++p) {
                const char* pname = desc.param_names[p] ? desc.param_names[p] : "param";
                strncpy(info->param_names[p], pname, sizeof(info->param_names[p]) - 1);
                info->param_names[p][sizeof(info->param_names[p]) - 1] = '\0';
                float def_v = desc.param_defaults[p];
                info->param_defaults[p] = def_v;
                derive_param_range(pname, def_v, &info->param_min[p], &info->param_max[p]);
                info->param_kind[p] = fx_param_kind_from_name(pname);
            }
        }
    }
    effects_panel_build_categories(panel);
    panel->initialized = true;
    panel->overlay_scroll_index = 0;
    if (panel->category_count == 0) {
        panel->overlay_layer = FX_PANEL_OVERLAY_CLOSED;
    }
}

void effects_panel_sync_from_engine(AppState* state) {
    if (!state || !state->engine) {
        return;
    }
    EffectsPanelState* panel = &state->effects_panel;
    FxInstId selected_id = 0;
    if (panel->selected_slot_index >= 0 && panel->selected_slot_index < panel->chain_count) {
        selected_id = panel->chain[panel->selected_slot_index].id;
    }
    FxInstId open_id = 0;
    if (panel->list_open_slot_index >= 0 && panel->list_open_slot_index < panel->chain_count) {
        open_id = panel->chain[panel->list_open_slot_index].id;
    }
    effects_panel_update_target(state);
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
    }
    for (int i = snap.count; i < FX_MASTER_MAX; ++i) {
        panel->chain[i].id = 0;
        panel->chain[i].param_count = 0;
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
}

void effects_panel_compute_layout(const AppState* state, EffectsPanelLayout* layout) {
    if (!state || !layout) {
        return;
    }
    zero_layout(layout);
    const Pane* mixer = ui_layout_get_pane(state, 2);
    if (!mixer) {
        return;
    }
    layout->panel_rect = mixer->rect;

    int content_x = mixer->rect.x + FX_PANEL_MARGIN;
    int content_y = mixer->rect.y + FX_PANEL_MARGIN;
    int content_w = mixer->rect.w - 2 * FX_PANEL_MARGIN;
    int content_h = mixer->rect.h - 2 * FX_PANEL_MARGIN;
    if (content_w <= 0 || content_h <= 0) {
        return;
    }

    EffectsPanelState* panel_mut = (EffectsPanelState*)&state->effects_panel;
    const EffectsPanelState* panel = panel_mut;
    if (panel_mut->param_scroll_drag_slot >= panel->chain_count) {
        panel_mut->param_scroll_drag_slot = -1;
    }
    const TrackNameEditor* editor = &state->track_name_editor;
    const char* target_label = (panel->target_label[0] != '\0') ? panel->target_label : "Master";
    if (editor && editor->editing && editor->buffer[0] != '\0') {
        target_label = editor->buffer;
        panel_mut->target = FX_PANEL_TARGET_TRACK;
        panel_mut->target_track_index = editor->track_index;
    }
    char target_line[128];
    if (panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0) {
        snprintf(target_line, sizeof(target_line), "Track FX: %s", target_label);
    } else {
        snprintf(target_line, sizeof(target_line), "Master FX");
    }
    float title_scale = FX_PANEL_TITLE_SCALE;
    int text_w = ui_measure_text_width(target_line, title_scale);
    int text_h = ui_font_line_height(title_scale);
    int padding_x = 10;
    int padding_y = 6;
    int target_w = text_w + padding_x;
    int target_h = text_h + padding_y;
    int target_x = mixer->rect.x + mixer->rect.w - FX_PANEL_MARGIN - target_w;
    int target_y = content_y + (FX_PANEL_HEADER_HEIGHT - target_h) / 2;
    layout->target_label_rect = (SDL_Rect){target_x, target_y, target_w, target_h};

    float button_scale = FX_PANEL_BUTTON_SCALE;
    int button_h = FX_PANEL_HEADER_BUTTON_HEIGHT;
    int button_y = content_y + (FX_PANEL_HEADER_HEIGHT - button_h) / 2;
    int toggle_w_list = ui_measure_text_width("List", button_scale);
    int toggle_w_rack = ui_measure_text_width("Rack", button_scale);
    int toggle_w = (toggle_w_list > toggle_w_rack ? toggle_w_list : toggle_w_rack) + FX_PANEL_HEADER_BUTTON_PAD_X * 2;
    layout->view_toggle_rect = (SDL_Rect){content_x, button_y, toggle_w, button_h};

    const char* add_label = "Add FX";
    int add_w = ui_measure_text_width(add_label, button_scale) + FX_PANEL_HEADER_BUTTON_PAD_X * 2;
    int add_x = layout->view_toggle_rect.x + layout->view_toggle_rect.w + FX_PANEL_HEADER_BUTTON_GAP;
    layout->dropdown_button_rect = (SDL_Rect){add_x, button_y, add_w, button_h};

    int body_y = content_y + FX_PANEL_HEADER_HEIGHT;
    int body_h = content_h - FX_PANEL_HEADER_HEIGHT;
    if (body_h <= 0) {
        body_h = content_h / 2;
    }

    if (panel->view_mode == FX_PANEL_VIEW_LIST) {
        compute_list_layout(panel, content_x, content_y, content_w, content_h, layout);
    } else {
        int column_count = panel->chain_count;
        layout->column_count = column_count;

        if (column_count > FX_MASTER_MAX) {
            column_count = FX_MASTER_MAX;
        }

        if (column_count > 0) {
            int total_gaps = (column_count - 1) * FX_PANEL_COLUMN_GAP;
            int column_w = (content_w - total_gaps);
            if (column_w < 0) column_w = 0;
            column_w = column_count > 0 ? column_w / column_count : content_w;
            if (column_w < 160) {
                column_w = 160;
            }

            int start_x = content_x;
            for (int i = 0; i < column_count; ++i) {
                SDL_Rect col = {start_x, body_y, column_w, body_h};
                effects_slot_compute_layout(panel_mut,
                                            i,
                                            &col,
                                            FX_PANEL_HEADER_HEIGHT,
                                            FX_PANEL_INNER_MARGIN,
                                            FX_PANEL_PARAM_GAP,
                                            &layout->slots[i]);
                start_x += column_w + FX_PANEL_COLUMN_GAP;
            }
        }
    }

    layout->overlay_visible = false;
    layout->overlay_item_count = 0;
    layout->overlay_total_items = 0;
    layout->overlay_visible_count = 0;
    layout->overlay_has_scrollbar = false;
    const bool overlay_active = (panel->overlay_layer != FX_PANEL_OVERLAY_CLOSED);
    if (!overlay_active) {
        return;
    }

    int overlay_total_items = 0;
    if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
        overlay_total_items = panel->category_count;
    } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        if (panel->active_category_index >= 0 && panel->active_category_index < panel->category_count) {
            overlay_total_items = panel->categories[panel->active_category_index].type_count;
        }
    }

    int overlay_w = FX_PANEL_OVERLAY_WIDTH;
    if (overlay_w > content_w) {
        overlay_w = content_w;
    }
    int overlay_x = layout->dropdown_button_rect.x;
    if (overlay_x + overlay_w > content_x + content_w) {
        overlay_x = content_x + content_w - overlay_w;
    }
    int overlay_y = layout->dropdown_button_rect.y + layout->dropdown_button_rect.h + 6;
    int available_h = (mixer->rect.y + mixer->rect.h) - FX_PANEL_MARGIN - overlay_y;
    if (available_h < FX_PANEL_OVERLAY_HEADER_HEIGHT + FX_PANEL_DROPDOWN_ITEM_HEIGHT) {
        return;
    }

    int max_visible_items = (available_h - FX_PANEL_OVERLAY_HEADER_HEIGHT) / FX_PANEL_DROPDOWN_ITEM_HEIGHT;
    if (max_visible_items <= 0) {
        return;
    }
    if (overlay_total_items <= 0) {
        layout->overlay_visible = false;
        return;
    }

    int display_capacity = overlay_total_items;
    if (display_capacity < 1) display_capacity = 1;
    if (display_capacity > max_visible_items) {
        display_capacity = max_visible_items;
    }

    int max_scroll = overlay_total_items - display_capacity;
    if (max_scroll < 0) max_scroll = 0;
    int scroll_index = panel->overlay_scroll_index;
    if (scroll_index > max_scroll) {
        scroll_index = max_scroll;
    }
    if (scroll_index < 0) {
        scroll_index = 0;
    }

    int first_index = scroll_index;
    int remaining = overlay_total_items - first_index;
    if (remaining < 0) remaining = 0;
    int visible_items = remaining;
    if (visible_items > display_capacity) {
        visible_items = display_capacity;
    }
    if (visible_items < 0) {
        visible_items = 0;
    }

    SDL_Rect overlay_rect = {
        overlay_x,
        overlay_y,
        overlay_w,
        FX_PANEL_OVERLAY_HEADER_HEIGHT + display_capacity * FX_PANEL_DROPDOWN_ITEM_HEIGHT + FX_PANEL_OVERLAY_PADDING
    };

    layout->overlay_visible = true;
    layout->overlay_rect = overlay_rect;
    layout->overlay_header_rect = (SDL_Rect){overlay_rect.x, overlay_rect.y, overlay_rect.w, FX_PANEL_OVERLAY_HEADER_HEIGHT};
    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        layout->overlay_back_rect = (SDL_Rect){
            overlay_rect.x + FX_PANEL_OVERLAY_PADDING,
            overlay_rect.y + 6,
            24,
            FX_PANEL_OVERLAY_HEADER_HEIGHT - 12
        };
    } else {
        layout->overlay_back_rect = (SDL_Rect){0, 0, 0, 0};
    }

    layout->overlay_total_items = overlay_total_items;
    layout->overlay_visible_count = visible_items;
    layout->overlay_item_count = visible_items;

    int item_y = overlay_rect.y + FX_PANEL_OVERLAY_HEADER_HEIGHT + 4;
    const int scrollbar_w = 8;
    bool has_scrollbar = (overlay_total_items > display_capacity);
    layout->overlay_has_scrollbar = has_scrollbar;
    int item_width = overlay_rect.w - 2 * FX_PANEL_OVERLAY_PADDING - (has_scrollbar ? (scrollbar_w + 4) : 0);
    if (item_width < 80) item_width = overlay_rect.w - 2 * FX_PANEL_OVERLAY_PADDING;

    for (int i = 0; i < visible_items; ++i) {
        layout->overlay_item_rects[i] = (SDL_Rect){
            overlay_rect.x + FX_PANEL_OVERLAY_PADDING,
            item_y,
            item_width,
            FX_PANEL_DROPDOWN_ITEM_HEIGHT
        };
        if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
            int cat_index = first_index + i;
            layout->overlay_item_order[i] = (cat_index >= 0 && cat_index < panel->category_count) ? cat_index : -1;
        } else {
            const FxCategoryUIInfo* cat = NULL;
            if (panel->active_category_index >= 0 && panel->active_category_index < panel->category_count) {
                cat = &panel->categories[panel->active_category_index];
            }
            int type_index = -1;
            if (cat) {
                int cat_offset = first_index + i;
                if (cat_offset >= 0 && cat_offset < cat->type_count) {
                    type_index = cat->type_indices[cat_offset];
                }
            }
            layout->overlay_item_order[i] = type_index;
        }
        item_y += FX_PANEL_DROPDOWN_ITEM_HEIGHT;
    }

    if (has_scrollbar) {
        int track_x = overlay_rect.x + overlay_rect.w - FX_PANEL_OVERLAY_PADDING - scrollbar_w;
        int track_y = overlay_rect.y + FX_PANEL_OVERLAY_HEADER_HEIGHT + 4;
        int track_h = display_capacity * FX_PANEL_DROPDOWN_ITEM_HEIGHT;
        layout->overlay_scrollbar_track = (SDL_Rect){track_x, track_y, scrollbar_w, track_h};
        float visible_ratio = (overlay_total_items > 0) ? (float)visible_items / (float)overlay_total_items : 1.0f;
        if (visible_ratio < 0.05f) visible_ratio = 0.05f;
        int thumb_h = (int)(track_h * visible_ratio);
        if (thumb_h < 10) thumb_h = 10;
        int max_scroll_index = overlay_total_items - visible_items;
        float scroll_ratio = (max_scroll_index > 0) ? (float)scroll_index / (float)max_scroll_index : 0.0f;
        if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
        if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;
        int thumb_y = track_y + (int)((track_h - thumb_h) * scroll_ratio);
        if (thumb_y > track_y + track_h - thumb_h) thumb_y = track_y + track_h - thumb_h;
        layout->overlay_scrollbar_thumb = (SDL_Rect){track_x, thumb_y, scrollbar_w, thumb_h};
    } else {
        layout->overlay_scrollbar_track = (SDL_Rect){0,0,0,0};
        layout->overlay_scrollbar_thumb = (SDL_Rect){0,0,0,0};
    }
}

static void render_overlay(SDL_Renderer* renderer, const AppState* state, const EffectsPanelLayout* layout) {
    if (!renderer || !state || !layout || !layout->overlay_visible) {
        return;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    SDL_Color bg = {26, 26, 32, 240};
    SDL_Color border = {90, 95, 110, 255};
    SDL_Color header_bg = {48, 52, 62, 255};
    SDL_Color label = {210, 210, 220, 255};
    SDL_Color hover = {80, 110, 160, 255};

    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &layout->overlay_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &layout->overlay_rect);

    SDL_SetRenderDrawColor(renderer, header_bg.r, header_bg.g, header_bg.b, header_bg.a);
    SDL_RenderFillRect(renderer, &layout->overlay_header_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &layout->overlay_header_rect);

    const char* title = "Add Effect";
    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS && panel->active_category_index >= 0 &&
        panel->active_category_index < panel->category_count) {
        title = panel->categories[panel->active_category_index].name;
    } else if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
        title = "Select Category";
    }
    ui_draw_text(renderer,
                 layout->overlay_header_rect.x + FX_PANEL_OVERLAY_PADDING + (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS ? 32 : 8),
                 layout->overlay_header_rect.y + 10,
                 title,
                 label,
                 2);

    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        SDL_Color back_color = {70, 90, 120, 255};
        SDL_SetRenderDrawColor(renderer, back_color.r, back_color.g, back_color.b, back_color.a);
        SDL_RenderFillRect(renderer, &layout->overlay_back_rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &layout->overlay_back_rect);
        ui_draw_text(renderer,
                     layout->overlay_back_rect.x + 6,
                     layout->overlay_back_rect.y + 8,
                     "<",
                     label,
                     2);
    }

    if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
        bool invalid_cat = (panel->active_category_index < 0 || panel->active_category_index >= panel->category_count);
        bool empty_cat = false;
        if (!invalid_cat) {
            const FxCategoryUIInfo* cat = &panel->categories[panel->active_category_index];
            empty_cat = (cat->type_count == 0);
        }
        if (invalid_cat || empty_cat || layout->overlay_item_count == 0) {
            const char* msg = "No effects in this category.";
            ui_draw_text(renderer,
                         layout->overlay_rect.x + FX_PANEL_OVERLAY_PADDING,
                         layout->overlay_rect.y + FX_PANEL_OVERLAY_HEADER_HEIGHT + 12,
                         msg,
                         label,
                         2);
            return;
        }
    }

    for (int i = 0; i < layout->overlay_item_count; ++i) {
        SDL_Rect item_rect = layout->overlay_item_rects[i];
        bool is_hover = false;
        if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
            if (layout->overlay_item_order[i] == panel->hovered_category_index) {
                is_hover = true;
            }
        } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
            if (layout->overlay_item_order[i] == panel->hovered_effect_index) {
                is_hover = true;
            }
        }
        if (is_hover) {
            SDL_SetRenderDrawColor(renderer, hover.r, hover.g, hover.b, hover.a);
            SDL_RenderFillRect(renderer, &item_rect);
        }

        const char* item_label = "";
        char buffer[96];
        if (panel->overlay_layer == FX_PANEL_OVERLAY_CATEGORIES) {
            int cat_index = layout->overlay_item_order[i];
            if (cat_index >= 0 && cat_index < panel->category_count) {
                const FxCategoryUIInfo* cat = &panel->categories[cat_index];
                snprintf(buffer, sizeof(buffer), "%s (%d)", cat->name, cat->type_count);
                item_label = buffer;
            }
        } else if (panel->overlay_layer == FX_PANEL_OVERLAY_EFFECTS) {
            int type_index = layout->overlay_item_order[i];
            if (type_index >= 0 && type_index < panel->type_count) {
                item_label = panel->types[type_index].name;
            }
        }
        ui_draw_text(renderer, item_rect.x + 8, item_rect.y + 4, item_label, label, 1.5f);
    }

    if (layout->overlay_has_scrollbar) {
        SDL_Color track = {52, 56, 64, 220};
        SDL_Color thumb = {120, 160, 220, 255};
        SDL_SetRenderDrawColor(renderer, track.r, track.g, track.b, track.a);
        SDL_RenderFillRect(renderer, &layout->overlay_scrollbar_track);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &layout->overlay_scrollbar_track);
        SDL_SetRenderDrawColor(renderer, thumb.r, thumb.g, thumb.b, thumb.a);
        SDL_RenderFillRect(renderer, &layout->overlay_scrollbar_thumb);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &layout->overlay_scrollbar_thumb);
    }
}

void effects_panel_render(SDL_Renderer* renderer, const AppState* state, const EffectsPanelLayout* layout) {
    if (!renderer || !state || !layout) {
        return;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    SDL_Color label_color = {210, 210, 220, 255};
    SDL_Color text_dim = {160, 170, 190, 255};

    if (panel->title_debug_last_click) {
        SDL_Log("FX title clicked (target=%s, track=%d)", panel->target == FX_PANEL_TARGET_TRACK ? "track" : "master", panel->target_track_index);
        ((EffectsPanelState*)panel)->title_debug_last_click = false;
    }

    const TrackNameEditor* editor = &state->track_name_editor;
    const char* target_label = (panel->target_label[0] != '\0') ? panel->target_label : "Master";
    if (panel->target == FX_PANEL_TARGET_TRACK &&
        editor &&
        editor->editing &&
        editor->track_index == panel->target_track_index &&
        editor->buffer[0] != '\0') {
        target_label = editor->buffer;
    }
    char target_line[128];
    if (panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0) {
        snprintf(target_line, sizeof(target_line), "Track FX: %s", target_label);
    } else {
        snprintf(target_line, sizeof(target_line), "Master FX");
    }
    float title_scale = FX_PANEL_TITLE_SCALE;
    SDL_Rect title_rect = layout->target_label_rect.w > 0 ? layout->target_label_rect
                                                          : (SDL_Rect){layout->panel_rect.x + FX_PANEL_MARGIN,
                                                                       layout->panel_rect.y + FX_PANEL_MARGIN + 6,
                                                                       ui_measure_text_width(target_line, title_scale) + 12,
                                                                       ui_font_line_height(title_scale) + 8};
    SDL_SetRenderDrawColor(renderer, 40, 44, 54, 200);
    SDL_RenderFillRect(renderer, &title_rect);
    SDL_SetRenderDrawColor(renderer, 90, 95, 110, 255);
    SDL_RenderDrawRect(renderer, &title_rect);
    int text_h = ui_font_line_height(title_scale);
    int text_y = title_rect.y + (title_rect.h - text_h) / 2;
    ui_draw_text(renderer, title_rect.x + 6, text_y, target_line, label_color, title_scale);
    if (editor && editor->editing) {
        const char* prefix = "Track FX: ";
        int prefix_w = ui_measure_text_width(prefix, title_scale);
        char temp[ENGINE_CLIP_NAME_MAX + 32];
        snprintf(temp, sizeof(temp), "%.*s", editor->cursor, editor->buffer);
        int caret_x = title_rect.x + 6 + prefix_w + ui_measure_text_width(temp, title_scale);
        if (caret_x > title_rect.x + title_rect.w) {
            caret_x = title_rect.x + title_rect.w;
        }
        int caret_h = ui_font_line_height(title_scale);
        SDL_Rect caret = {caret_x, text_y, 2, caret_h};
        SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
        SDL_RenderFillRect(renderer, &caret);
    }

    // View mode + add button
    const char* toggle_label = panel->view_mode == FX_PANEL_VIEW_STACK ? "List" : "Rack";
    draw_button(renderer, &layout->view_toggle_rect, false, toggle_label, FX_PANEL_BUTTON_SCALE);
    bool button_active = (panel->overlay_layer != FX_PANEL_OVERLAY_CLOSED);
    draw_button(renderer, &layout->dropdown_button_rect, button_active, "Add FX", FX_PANEL_BUTTON_SCALE);

    if (panel->view_mode == FX_PANEL_VIEW_LIST) {
        effects_panel_render_list(renderer, state, layout);
        render_overlay(renderer, state, layout);
        return;
    }

    if (panel->chain_count == 0) {
        char msg[160];
        snprintf(msg,
                 sizeof(msg),
                 "No effects on %s yet. Use 'Add FX' to insert one.",
                 target_label);
        int msg_x = layout->panel_rect.x + FX_PANEL_MARGIN;
        int msg_y = layout->panel_rect.y + FX_PANEL_MARGIN + 48;
        ui_draw_text(renderer, msg_x, msg_y, msg, text_dim, 2);
    }

    for (int i = 0; i < layout->column_count && i < panel->chain_count; ++i) {
        effects_slot_render(renderer,
                            state,
                            i,
                            &layout->slots[i],
                            panel->highlighted_slot_index == i,
                            panel->hovered_toggle_slot_index == i,
                            panel->selected_slot_index == i,
                            label_color,
                            text_dim);
    }

    render_overlay(renderer, state, layout);
}
