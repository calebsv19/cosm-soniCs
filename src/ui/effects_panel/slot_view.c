#include "app_state.h"
#include "effects/param_utils.h"
#include "ui/effects_panel.h"
#include "ui/effects_panel_slot.h"
#include "ui/effects_panel_widgets.h"
#include "ui/effects_panel_preview.h"
#include "ui/effects_panel_spec.h"
#include "ui/font.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

void effects_slot_reset_runtime(EffectsSlotRuntime* runtime) {
    if (!runtime) {
        return;
    }
    runtime->scroll = 0.0f;
    runtime->scroll_max = 0.0f;
    runtime->dragging = false;
    runtime->drag_start_y = 0;
    runtime->drag_start_val = 0.0f;
}

static const FxTypeUIInfo* find_type_info(const EffectsPanelState* panel, FxTypeId type_id) {
    if (!panel) {
        return NULL;
    }
    for (int i = 0; i < panel->type_count; ++i) {
        if (panel->types[i].type_id == type_id) {
            return &panel->types[i];
        }
    }
    return NULL;
}

// effects_slot_get_gain_reduction reads the most recent gain reduction sample for a slot.
static bool effects_slot_get_gain_reduction(const AppState* state,
                                            const FxSlotUIState* slot,
                                            bool is_master,
                                            int track_index,
                                            float* out_gr_db) {
    if (!state || !slot || !out_gr_db) {
        return false;
    }
    float samples[32];
    int count = engine_get_fx_scope_samples(state->engine,
                                            is_master,
                                            track_index,
                                            slot->id,
                                            ENGINE_SCOPE_STREAM_GAIN_REDUCTION,
                                            samples,
                                            (int)(sizeof(samples) / sizeof(samples[0])));
    if (count <= 0) {
        return false;
    }
    *out_gr_db = samples[count - 1];
    return true;
}

static void format_beat_label(float beats, char* out, size_t out_size) {
    static const struct {
        float beats;
        const char* label;
    } kNotes[] = {
        {4.0f,    "4/1"},
        {2.0f,    "2/1"},
        {1.0f,    "1/1"},
        {0.75f,   "3/4"},
        {0.5f,    "1/2"},
        {1.0f / 3.0f, "1/2T"},
        {0.25f,   "1/4"},
        {0.1875f, "1/4."},
        {1.0f / 6.0f, "1/4T"},
        {0.125f,  "1/8"},
        {0.09375f,"1/8."},
        {1.0f / 12.0f, "1/8T"},
        {0.0625f, "1/16"},
        {3.0f / 64.0f, "1/16."},
        {1.0f / 24.0f, "1/16T"},
        {0.03125f,"1/32"}
    };
    float best_diff = 1e9f;
    const char* best = NULL;
    for (size_t i = 0; i < sizeof(kNotes)/sizeof(kNotes[0]); ++i) {
        float diff = fabsf(beats - kNotes[i].beats);
        if (diff < best_diff) {
            best_diff = diff;
            best = kNotes[i].label;
        }
    }
    if (best && best_diff < 0.02f) {
        snprintf(out, out_size, "%s", best);
    } else {
        snprintf(out, out_size, "%.3f b", beats);
    }
}

static void format_value_label(const EffectParamSpec* spec,
                               float value,
                               FxParamMode mode,
                               char* out,
                               size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (mode != FX_PARAM_MODE_NATIVE && fx_param_spec_is_syncable(spec)) {
        format_beat_label(value, out, out_size);
        return;
    }
    fx_param_format_value(spec, value, out, out_size);
}

void effects_slot_render(SDL_Renderer* renderer,
                         const struct AppState* state,
                         int slot_index,
                         const EffectsSlotLayout* slot_layout,
                         bool remove_highlight,
                         bool toggle_highlight,
                         bool selected,
                         SDL_Color label_color,
                         SDL_Color text_dim) {
    if (!renderer || !state || !slot_layout) {
        return;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    if (slot_index < 0 || slot_index >= panel->chain_count) {
        return;
    }
    const FxSlotUIState* slot = &panel->chain[slot_index];
    const FxTypeUIInfo* info = find_type_info(panel, slot->type_id);

    DawThemePalette theme = {0};
    bool have_theme = daw_shared_theme_resolve_palette(&theme);
    SDL_Color box_bg = have_theme ? theme.timeline_fill : (SDL_Color){32, 34, 42, 255};
    SDL_Color box_border = have_theme ? theme.control_border : (SDL_Color){80, 85, 100, 255};
    SDL_Color header_bg = have_theme ? theme.control_fill : (SDL_Color){44, 48, 58, 255};
    SDL_Color selected_border = have_theme ? theme.selection_fill : (SDL_Color){120, 160, 220, 180};
    SDL_SetRenderDrawColor(renderer, box_bg.r, box_bg.g, box_bg.b, box_bg.a);
    SDL_RenderFillRect(renderer, &slot_layout->column_rect);

    SDL_Rect header = slot_layout->header_rect;
    SDL_SetRenderDrawColor(renderer, header_bg.r, header_bg.g, header_bg.b, header_bg.a);
    SDL_RenderFillRect(renderer, &header);
    SDL_SetRenderDrawColor(renderer, box_border.r, box_border.g, box_border.b, box_border.a);
    SDL_RenderDrawRect(renderer, &header);
    const char* fx_name = info ? info->name : "Effect";
    float title_scale = FX_PANEL_BUTTON_SCALE;
    int title_h = ui_font_line_height(title_scale);
    int title_x = header.x + 8;
    int title_max_w = slot_layout->toggle_rect.x - title_x - 6;
    if (title_max_w < 0) {
        title_max_w = 0;
    }
    int title_y = header.y + (header.h - title_h) / 2;
    ui_draw_text_clipped(renderer, title_x, title_y, fx_name, label_color, title_scale, title_max_w);
    if (effects_slot_preview_has_gr(slot->type_id)) {
        bool is_master = panel->target == FX_PANEL_TARGET_MASTER;
        float gr_db = 0.0f;
        char gr_label[32];
        bool have_gr = effects_slot_get_gain_reduction(state,
                                                       slot,
                                                       is_master,
                                                       panel->target_track_index,
                                                       &gr_db);
        int meter_h = max_int(10, header.h - 8);
        int meter_w = max_int(56, ui_measure_text_width("GR -24.0 dB", 1.0f) + 14);
        SDL_Rect gr_meter = {slot_layout->toggle_rect.x - meter_w - 6,
                             header.y + (header.h - meter_h) / 2,
                             meter_w,
                             meter_h};
        if (have_gr) {
            effects_slot_draw_gr_meter(renderer, &gr_meter, gr_db);
        } else {
            effects_slot_draw_gr_meter(renderer, &gr_meter, 0.0f);
        }
        if (have_gr) {
            snprintf(gr_label, sizeof(gr_label), "GR %.1f dB", gr_db);
        } else {
            snprintf(gr_label, sizeof(gr_label), "GR --");
        }
        int right_bound = gr_meter.x - 8;
        int min_x = title_x + 8;
        int gr_y = header.y + (header.h - ui_font_line_height(1.1f)) / 2;
        if (right_bound > min_x) {
            int gr_max_w = right_bound - min_x;
            ui_draw_text_clipped(renderer, min_x, gr_y, gr_label, text_dim, 1.1f, gr_max_w);
        }
        int title_space_after_meter = gr_meter.x - title_x - 10;
        if (title_space_after_meter > 0) {
            ui_draw_text_clipped(renderer, title_x, title_y, fx_name, label_color, title_scale, title_space_after_meter);
        }
    }

    effects_slot_draw_enable_toggle(renderer, &slot_layout->toggle_rect, slot->enabled, toggle_highlight);
    effects_slot_draw_remove_button(renderer, &slot_layout->remove_rect, remove_highlight);

    SDL_SetRenderDrawColor(renderer, box_border.r, box_border.g, box_border.b, box_border.a);
    SDL_RenderDrawRect(renderer, &slot_layout->column_rect);
    if (selected) {
        SDL_SetRenderDrawColor(renderer,
                               selected_border.r,
                               selected_border.g,
                               selected_border.b,
                               selected_border.a);
        SDL_RenderDrawRect(renderer, &slot_layout->column_rect);
    }

    SDL_Rect body_clip = slot_layout->body_rect;
    if (body_clip.w > 0 && body_clip.h > 0) {
        SDL_Rect prev_clip;
        SDL_bool had_clip = ui_clip_is_enabled(renderer);
        ui_get_clip_rect(renderer, &prev_clip);
        ui_set_clip_rect(renderer, &body_clip);

        if (effects_panel_spec_enabled(panel, slot->type_id)) {
            EffectsSpecPanelLayout spec_layout;
            const EffectsSlotRuntime* runtime = &panel->slot_runtime[slot_index];
            effects_panel_spec_compute_layout(state,
                                              panel,
                                              slot,
                                              &body_clip,
                                              runtime ? runtime->scroll : 0.0f,
                                              &spec_layout);
            effects_panel_spec_render(renderer, state, slot, &spec_layout, label_color, text_dim);
        } else {
            for (uint32_t p = 0; p < slot->param_count && p < FX_MAX_PARAMS; ++p) {
                SDL_Rect label_rect = slot_layout->label_rects[p];
                SDL_Rect slider_rect = slot_layout->slider_rects[p];
                SDL_Rect value_rect = slot_layout->value_rects[p];
                if (slider_rect.y > body_clip.y + body_clip.h ||
                    slider_rect.y + slider_rect.h < body_clip.y) {
                    continue;
                }

                const char* pname = info ? info->param_names[p] : "Param";
                FxParamMode mode = slot->param_mode[p];
                const EffectParamSpec* spec = info ? &info->param_specs[p] : NULL;
                bool tempo_syncable = fx_param_spec_is_syncable(spec);
                float value = slot->param_values[p];
                float beat_min = 0.0f;
                float beat_max = 0.0f;
                bool have_beat_bounds = fx_param_spec_get_beat_bounds(spec, &state->tempo, &beat_min, &beat_max);
                if (mode != FX_PARAM_MODE_NATIVE && tempo_syncable) {
                    value = slot->param_beats[p];
                    if (fabsf(value) < 1e-6f) {
                        value = fx_param_spec_native_to_beats(spec, slot->param_values[p], &state->tempo);
                    }
                    if (have_beat_bounds) {
                        if (value < beat_min) value = beat_min;
                        if (value > beat_max) value = beat_max;
                    }
                }
                float t = 0.0f;
                if (mode != FX_PARAM_MODE_NATIVE && tempo_syncable && have_beat_bounds) {
                    t = (value - beat_min) / (beat_max - beat_min);
                } else {
                    t = fx_param_map_value_to_ui(spec, value);
                }
                effects_slot_draw_slider(renderer, &slider_rect, t);

                char label_line[96];
                snprintf(label_line, sizeof(label_line), "%s", pname);
                ui_draw_text_clipped(renderer,
                                     label_rect.x,
                                     label_rect.y,
                                     label_line,
                                     label_color,
                                     1.3f,
                                     label_rect.w);

                char value_line[64];
                format_value_label(spec, value, mode, value_line, sizeof(value_line));
                ui_draw_text_clipped(renderer,
                                     value_rect.x,
                                     value_rect.y,
                                     value_line,
                                     text_dim,
                                     1.3f,
                                     value_rect.w);
                SDL_Rect mode_rect = slot_layout->mode_rects[p];
                if (tempo_syncable && mode_rect.w > 0 && mode_rect.h > 0) {
                    effects_slot_draw_mode_toggle(renderer, &mode_rect, mode);
                }
            }
        }

        ui_set_clip_rect(renderer, had_clip ? &prev_clip : NULL);
    }

    if (slot_layout->preview_rect.w > 0 && slot_layout->preview_rect.h > 0) {
        EffectsPanelPreviewSlotState* preview = &((EffectsPanelState*)panel)->preview_slots[slot_index];
        EffectsPreviewMode preview_mode = effects_slot_preview_mode(slot->type_id);
        bool have_gr = false;
        float gr_db = 0.0f;
        if (preview_mode == FX_PREVIEW_HISTORY_GR || preview_mode == FX_PREVIEW_HISTORY_TRIM) {
            bool is_master = panel->target == FX_PANEL_TARGET_MASTER;
            have_gr = effects_slot_get_gain_reduction(state,
                                                      slot,
                                                      is_master,
                                                      panel->target_track_index,
                                                      &gr_db);
        }
        float sample_rate = (float)state->runtime_cfg.sample_rate;
        if (sample_rate <= 0.0f) {
            sample_rate = 48000.0f;
        }
        effects_slot_preview_render(renderer,
                                    slot,
                                    preview,
                                    &slot_layout->preview_rect,
                                    &slot_layout->preview_toggle_rect,
                                    label_color,
                                    text_dim,
                                    sample_rate,
                                    have_gr,
                                    gr_db);
    }

    const EffectsSlotRuntime* runtime = &panel->slot_runtime[slot_index];
    if (runtime->scroll_max > 0.5f && slot_layout->scrollbar_track.h > 0) {
        SDL_Color track = have_theme ? theme.slider_track : (SDL_Color){52, 56, 64, 180};
        SDL_Color thumb = have_theme ? theme.slider_handle : (SDL_Color){130, 170, 230, 220};
        SDL_Color border = have_theme ? theme.control_border : (SDL_Color){80, 85, 100, 200};
        SDL_SetRenderDrawColor(renderer, track.r, track.g, track.b, track.a);
        SDL_RenderFillRect(renderer, &slot_layout->scrollbar_track);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &slot_layout->scrollbar_track);
        SDL_SetRenderDrawColor(renderer, thumb.r, thumb.g, thumb.b, thumb.a);
        SDL_RenderFillRect(renderer, &slot_layout->scrollbar_thumb);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &slot_layout->scrollbar_thumb);
    }
}
