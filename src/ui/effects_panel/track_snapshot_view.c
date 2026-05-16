#include "ui/effects_panel.h"

#include "app_state.h"
#include "engine/engine.h"
#include "ui/font.h"
#include "ui/effects_panel_eq_detail.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>
#include <stdio.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

typedef struct TrackSnapshotTheme {
    SDL_Color slider_bg;
    SDL_Color slider_border;
    SDL_Color slider_fill;
    SDL_Color slider_handle;
    SDL_Color eq_bg;
    SDL_Color eq_curve;
    SDL_Color list_bg;
    SDL_Color list_border;
    SDL_Color scroll_track;
    SDL_Color scroll_thumb;
    SDL_Color meter_bg;
    SDL_Color meter_fill;
    SDL_Color meter_peak;
    SDL_Color meter_clip_off;
    SDL_Color meter_clip_on;
    SDL_Color meter_tick;
    SDL_Color text;
    SDL_Color button_bg;
    SDL_Color button_active;
} TrackSnapshotTheme;

static void resolve_track_snapshot_theme(TrackSnapshotTheme* out) {
    if (!out) {
        return;
    }
    DawThemePalette palette = {0};
    if (daw_shared_theme_resolve_palette(&palette)) {
        out->slider_bg = palette.slider_track;
        out->slider_border = palette.control_border;
        out->slider_fill = palette.selection_fill;
        out->slider_handle = palette.slider_handle;
        out->eq_bg = palette.timeline_fill;
        out->eq_curve = palette.accent_primary;
        out->list_bg = palette.inspector_fill;
        out->list_border = palette.pane_border;
        out->scroll_track = palette.control_fill;
        out->scroll_thumb = palette.slider_handle;
        out->meter_bg = palette.timeline_fill;
        out->meter_fill = palette.selection_fill;
        out->meter_peak = palette.text_primary;
        out->meter_clip_off = palette.control_fill;
        out->meter_clip_on = palette.accent_error;
        out->meter_tick = palette.grid_major;
        out->text = palette.text_muted;
        out->button_bg = palette.control_fill;
        out->button_active = palette.control_active_fill;
        return;
    }
    *out = (TrackSnapshotTheme){
        .slider_bg = {36, 36, 44, 255},
        .slider_border = {90, 90, 110, 255},
        .slider_fill = {80, 120, 170, 220},
        .slider_handle = {180, 210, 255, 255},
        .eq_bg = {22, 24, 30, 255},
        .eq_curve = {90, 170, 200, 255},
        .list_bg = {26, 28, 34, 255},
        .list_border = {70, 75, 92, 255},
        .scroll_track = {20, 22, 28, 255},
        .scroll_thumb = {90, 110, 150, 220},
        .meter_bg = {22, 24, 30, 255},
        .meter_fill = {60, 110, 170, 210},
        .meter_peak = {200, 220, 250, 230},
        .meter_clip_off = {50, 40, 40, 255},
        .meter_clip_on = {220, 80, 80, 255},
        .meter_tick = {80, 90, 110, 255},
        .text = {170, 180, 200, 255},
        .button_bg = {44, 48, 58, 255},
        .button_active = {90, 120, 170, 200},
    };
}

static void draw_slider(SDL_Renderer* renderer, const SDL_Rect* rect, float t) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    t = clampf(t, 0.0f, 1.0f);
    TrackSnapshotTheme theme = {0};
    resolve_track_snapshot_theme(&theme);
    SDL_Color track_bg = theme.slider_bg;
    SDL_Color track_border = theme.slider_border;
    SDL_Color fill = theme.slider_fill;
    SDL_SetRenderDrawColor(renderer, track_bg.r, track_bg.g, track_bg.b, track_bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, rect);
    SDL_Rect fill_rect = *rect;
    fill_rect.w = (int)(t * (float)rect->w);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &fill_rect);
    SDL_Rect handle = {
        rect->x + fill_rect.w - 3,
        rect->y - 2,
        6,
        rect->h + 4
    };
    if (handle.x < rect->x - 2) handle.x = rect->x - 2;
    if (handle.x + handle.w > rect->x + rect->w + 2) handle.x = rect->x + rect->w - 2;
    SDL_SetRenderDrawColor(renderer, theme.slider_handle.r, theme.slider_handle.g, theme.slider_handle.b, theme.slider_handle.a);
    SDL_RenderFillRect(renderer, &handle);
    SDL_SetRenderDrawColor(renderer, track_border.r, track_border.g, track_border.b, track_border.a);
    SDL_RenderDrawRect(renderer, &handle);
}

static float linear_to_db(float linear) {
    if (linear < 0.000001f) linear = 0.000001f;
    return 20.0f * log10f(linear);
}

static void compute_eq_preview_curve(const EqCurveState* curve,
                                     const SDL_Rect* graph,
                                     float* curve_db,
                                     int samples) {
    if (!curve || !graph || !curve_db || samples <= 1) {
        return;
    }
    for (int i = 0; i < samples; ++i) {
        float t = (float)i / (float)(samples - 1);
        float x = (float)graph->x + t * (float)graph->w;
        float freq = effects_eq_x_to_freq(graph, x);
        float db = 0.0f;
        if (curve->low_cut.enabled && freq < curve->low_cut.freq_hz) {
            float tcut = log2f(curve->low_cut.freq_hz / freq);
            float max_oct = 0.8f;
            float s = tcut / max_oct;
            if (s < 0.0f) s = 0.0f;
            if (s > 1.0f) s = 1.0f;
            float drop = s * s * (3.0f - 2.0f * s);
            db += (EQ_DETAIL_DB_MIN - db) * drop;
        }
        if (curve->high_cut.enabled && freq > curve->high_cut.freq_hz) {
            float tcut = log2f(freq / curve->high_cut.freq_hz);
            float max_oct = 1.2f;
            float s = tcut / max_oct;
            if (s < 0.0f) s = 0.0f;
            if (s > 1.0f) s = 1.0f;
            float drop = s * s * (3.0f - 2.0f * s);
            db += (EQ_DETAIL_DB_MIN - db) * drop;
        }
        for (int b = 0; b < 4; ++b) {
            if (!curve->bands[b].enabled) {
                continue;
            }
            float width = curve->bands[b].q_width;
            if (width < 0.1f) {
                width = 0.1f;
            }
            float x_oct = log2f(freq / curve->bands[b].freq_hz);
            float sigma = width * 0.35f;
            float influence = expf(-(x_oct * x_oct) / (2.0f * sigma * sigma));
            db += curve->bands[b].gain_db * influence;
        }
        curve_db[i] = clampf(db, EQ_DETAIL_DB_MIN, EQ_DETAIL_DB_MAX);
    }
}

static void draw_eq_preview(SDL_Renderer* renderer, const SDL_Rect* rect, const EqCurveState* curve) {
    if (!renderer || !rect || !curve || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    TrackSnapshotTheme theme = {0};
    resolve_track_snapshot_theme(&theme);
    SDL_Color bg = theme.eq_bg;
    SDL_Color border = theme.list_border;
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    SDL_Rect graph = *rect;
    graph.x += 6;
    graph.y += 6;
    graph.w -= 12;
    graph.h -= 12;
    if (graph.w <= 1 || graph.h <= 1) {
        return;
    }
    enum { PREVIEW_SAMPLES = 64 };
    float curve_db[PREVIEW_SAMPLES];
    compute_eq_preview_curve(curve, &graph, curve_db, PREVIEW_SAMPLES);
    SDL_SetRenderDrawColor(renderer, theme.eq_curve.r, theme.eq_curve.g, theme.eq_curve.b, theme.eq_curve.a);
    int prev_x = graph.x;
    int prev_y = (int)lroundf(effects_eq_db_to_y(&graph, curve_db[0]));
    for (int i = 1; i < PREVIEW_SAMPLES; ++i) {
        float t = (float)i / (float)(PREVIEW_SAMPLES - 1);
        int x = graph.x + (int)lroundf(t * (float)graph.w);
        int y = (int)lroundf(effects_eq_db_to_y(&graph, curve_db[i]));
        SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
        prev_x = x;
        prev_y = y;
    }
}

// Draws the effects list backdrop and scrollbar for the track snapshot column.
static void draw_effects_list_background(SDL_Renderer* renderer, const EffectsPanelTrackSnapshotLayout* snap) {
    if (!renderer || !snap || snap->list_rect.w <= 0 || snap->list_rect.h <= 0) {
        return;
    }
    TrackSnapshotTheme theme = {0};
    resolve_track_snapshot_theme(&theme);
    SDL_Color bg = theme.list_bg;
    SDL_Color border = theme.list_border;
    SDL_Color scroll_track = theme.scroll_track;
    SDL_Color scroll_thumb = theme.scroll_thumb;

    SDL_Rect prev_clip;
    SDL_bool had_clip = ui_clip_is_enabled(renderer);
    ui_get_clip_rect(renderer, &prev_clip);
    ui_set_clip_rect(renderer, &snap->list_rect);

    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &snap->list_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &snap->list_rect);

    if (snap->list_scroll_track.w > 0 && snap->list_scroll_track.h > 0) {
        SDL_SetRenderDrawColor(renderer, scroll_track.r, scroll_track.g, scroll_track.b, scroll_track.a);
        SDL_RenderFillRect(renderer, &snap->list_scroll_track);
    }
    if (snap->list_scroll_thumb.w > 0 && snap->list_scroll_thumb.h > 0) {
        SDL_SetRenderDrawColor(renderer, scroll_thumb.r, scroll_thumb.g, scroll_thumb.b, scroll_thumb.a);
        SDL_RenderFillRect(renderer, &snap->list_scroll_thumb);
    }

    ui_set_clip_rect(renderer, had_clip ? &prev_clip : NULL);
}

static float meter_db_to_y(const SDL_Rect* rect, float db) {
    if (!rect || rect->h <= 0) {
        return 0.0f;
    }
    float t = (db - FX_PANEL_METER_DB_MIN) / (FX_PANEL_METER_DB_MAX - FX_PANEL_METER_DB_MIN);
    t = clampf(t, 0.0f, 1.0f);
    return (float)rect->y + (1.0f - t) * (float)rect->h;
}

static void draw_meter(SDL_Renderer* renderer,
                       const EffectsPanelTrackSnapshotLayout* snap,
                       float peak_db,
                       float rms_db,
                       bool clipped) {
    if (!renderer || !snap || snap->meter_rect.w <= 0 || snap->meter_rect.h <= 0) {
        return;
    }
    TrackSnapshotTheme theme = {0};
    resolve_track_snapshot_theme(&theme);
    SDL_Color bg = theme.meter_bg;
    SDL_Color border = theme.list_border;
    SDL_Color rms_fill = theme.meter_fill;
    SDL_Color peak_line = theme.meter_peak;
    SDL_Color clip_off = theme.meter_clip_off;
    SDL_Color clip_on = theme.meter_clip_on;
    SDL_Color tick = theme.meter_tick;
    SDL_Color text = theme.text;
    static const char* kMeterLabels[FX_PANEL_METER_TICK_COUNT] = {"0", "-6", "-12", "-24", "-36", "-48"};

    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &snap->meter_rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &snap->meter_rect);

    float rms_y = meter_db_to_y(&snap->meter_rect, rms_db);
    SDL_Rect rms_rect = snap->meter_rect;
    rms_rect.y = (int)lroundf(rms_y);
    rms_rect.h = snap->meter_rect.y + snap->meter_rect.h - rms_rect.y;
    if (rms_rect.h < 0) rms_rect.h = 0;
    SDL_SetRenderDrawColor(renderer, rms_fill.r, rms_fill.g, rms_fill.b, rms_fill.a);
    SDL_RenderFillRect(renderer, &rms_rect);

    float peak_y = meter_db_to_y(&snap->meter_rect, peak_db);
    int py = (int)lroundf(peak_y);
    SDL_SetRenderDrawColor(renderer, peak_line.r, peak_line.g, peak_line.b, peak_line.a);
    SDL_RenderDrawLine(renderer,
                       snap->meter_rect.x,
                       py,
                       snap->meter_rect.x + snap->meter_rect.w,
                       py);

    if (snap->meter_clip_rect.w > 0 && snap->meter_clip_rect.h > 0) {
        SDL_Color clip_col = clipped ? clip_on : clip_off;
        SDL_SetRenderDrawColor(renderer, clip_col.r, clip_col.g, clip_col.b, clip_col.a);
        SDL_RenderFillRect(renderer, &snap->meter_clip_rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &snap->meter_clip_rect);
    }

    for (int i = 0; i < FX_PANEL_METER_TICK_COUNT; ++i) {
        const SDL_Rect* tick_rect = &snap->meter_tick_rects[i];
        if (tick_rect->w > 0 && tick_rect->h > 0) {
            SDL_SetRenderDrawColor(renderer, tick.r, tick.g, tick.b, tick.a);
            SDL_RenderFillRect(renderer, tick_rect);
        }
        const SDL_Rect* label_rect = &snap->meter_label_rects[i];
        if (label_rect->w > 0 && label_rect->h > 0) {
            ui_draw_text(renderer, label_rect->x, label_rect->y, kMeterLabels[i], text, 0.9f);
        }
    }
}

void effects_panel_render_track_snapshot(SDL_Renderer* renderer, const AppState* state, const EffectsPanelLayout* layout) {
    if (!renderer || !state || !layout) {
        return;
    }
    const EffectsPanelState* panel = &state->effects_panel;
    const EffectsPanelTrackSnapshotLayout* snap = &layout->track_snapshot;
    TrackSnapshotTheme theme = {0};
    resolve_track_snapshot_theme(&theme);
    SDL_Color label = theme.meter_peak;
    SDL_Color card_border = theme.list_border;
    SDL_Color button_bg = theme.button_bg;
    SDL_Color active_bg = theme.button_active;

    const EngineTrack* track = NULL;
    if (state->engine && panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0) {
        int track_count = engine_get_track_count(state->engine);
        if (panel->target_track_index < track_count) {
            const EngineTrack* tracks = engine_get_tracks(state->engine);
            if (tracks) {
                track = &tracks[panel->target_track_index];
            }
        }
    }

    float gain_linear = (track ? track->gain : panel->track_snapshot.gain);
    if (gain_linear <= 0.0f) gain_linear = 1.0f;
    float gain_db = linear_to_db(gain_linear);
    if (gain_db < -20.0f) gain_db = -20.0f;
    if (gain_db > 20.0f) gain_db = 20.0f;
    float gain_t = (gain_db + 20.0f) / 40.0f;

    float pan_value = track ? track->pan : panel->track_snapshot.pan;
    if (pan_value < -1.0f) pan_value = -1.0f;
    if (pan_value > 1.0f) pan_value = 1.0f;
    float pan_t = (pan_value + 1.0f) * 0.5f;

    bool muted = track ? track->muted : panel->track_snapshot.muted;
    bool solo = track ? track->solo : panel->track_snapshot.solo;

    if (snap->eq_rect.w > 0 && snap->eq_rect.h > 0) {
        draw_eq_preview(renderer, &snap->eq_rect, &panel->eq_curve);
    }

    if (snap->gain_rect.w > 0 && snap->gain_rect.h > 0) {
        draw_slider(renderer, &snap->gain_rect, gain_t);
        if (snap->gain_label_rect.w > 0 && snap->gain_label_rect.h > 0) {
            char line[64];
            snprintf(line, sizeof(line), "Gain %.1f dB", gain_db);
            ui_draw_text(renderer,
                         snap->gain_label_rect.x,
                         snap->gain_label_rect.y,
                         line,
                         label,
                         1.0f);
        }
    }

    if (snap->pan_rect.w > 0 && snap->pan_rect.h > 0) {
        draw_slider(renderer, &snap->pan_rect, pan_t);
        if (snap->pan_label_rect.w > 0 && snap->pan_label_rect.h > 0) {
            char line[64];
            snprintf(line, sizeof(line), "Pan %.2f", pan_value);
            ui_draw_text(renderer,
                         snap->pan_label_rect.x,
                         snap->pan_label_rect.y,
                         line,
                         label,
                         1.0f);
        }
    }

    if (snap->instrument_button_rect.w > 0 && snap->instrument_button_rect.h > 0) {
        bool midi_enabled = state->engine && panel->target == FX_PANEL_TARGET_TRACK &&
                            panel->target_track_index >= 0 &&
                            engine_track_midi_instrument_enabled(state->engine, panel->target_track_index);
        EngineInstrumentPresetId preset = state->engine && panel->target == FX_PANEL_TARGET_TRACK
                                              ? engine_track_midi_instrument_preset(state->engine,
                                                                                    panel->target_track_index)
                                              : ENGINE_INSTRUMENT_PRESET_PURE_SINE;
        SDL_SetRenderDrawColor(renderer,
                               panel->track_snapshot.instrument_menu_open ? active_bg.r : button_bg.r,
                               panel->track_snapshot.instrument_menu_open ? active_bg.g : button_bg.g,
                               panel->track_snapshot.instrument_menu_open ? active_bg.b : button_bg.b,
                               panel->track_snapshot.instrument_menu_open ? active_bg.a : button_bg.a);
        SDL_RenderFillRect(renderer, &snap->instrument_button_rect);
        SDL_SetRenderDrawColor(renderer, card_border.r, card_border.g, card_border.b, card_border.a);
        SDL_RenderDrawRect(renderer, &snap->instrument_button_rect);
        char instrument_label[96];
        snprintf(instrument_label,
                 sizeof(instrument_label),
                 midi_enabled ? "Instrument: %s" : "Audio Track",
                 engine_instrument_preset_display_name(preset));
        ui_draw_text_clipped(renderer,
                             snap->instrument_button_rect.x + 8,
                             snap->instrument_button_rect.y + 5,
                             instrument_label,
                             label,
                             0.95f,
                             snap->instrument_button_rect.w - 16);
    }

    draw_effects_list_background(renderer, snap);

    if (snap->mute_rect.w > 0 && snap->mute_rect.h > 0) {
        SDL_SetRenderDrawColor(renderer,
                               muted ? active_bg.r : button_bg.r,
                               muted ? active_bg.g : button_bg.g,
                               muted ? active_bg.b : button_bg.b,
                               muted ? active_bg.a : button_bg.a);
        SDL_RenderFillRect(renderer, &snap->mute_rect);
        SDL_SetRenderDrawColor(renderer, card_border.r, card_border.g, card_border.b, card_border.a);
        SDL_RenderDrawRect(renderer, &snap->mute_rect);
        ui_draw_text(renderer,
                     snap->mute_rect.x + 8,
                     snap->mute_rect.y + 6,
                     "Mute",
                     label,
                     1.2f);
    }

    if (snap->solo_rect.w > 0 && snap->solo_rect.h > 0) {
        SDL_SetRenderDrawColor(renderer,
                               solo ? active_bg.r : button_bg.r,
                               solo ? active_bg.g : button_bg.g,
                               solo ? active_bg.b : button_bg.b,
                               solo ? active_bg.a : button_bg.a);
        SDL_RenderFillRect(renderer, &snap->solo_rect);
        SDL_SetRenderDrawColor(renderer, card_border.r, card_border.g, card_border.b, card_border.a);
        SDL_RenderDrawRect(renderer, &snap->solo_rect);
        ui_draw_text(renderer,
                     snap->solo_rect.x + 8,
                     snap->solo_rect.y + 6,
                     "Solo",
                     label,
                     1.2f);
    }

    float meter_peak_db = FX_PANEL_METER_DB_MIN;
    float meter_rms_db = FX_PANEL_METER_DB_MIN;
    bool meter_clipped = false;
    if (state->engine) {
        EngineMeterSnapshot meter = {0};
        bool have_meter = false;
        if (panel->target == FX_PANEL_TARGET_TRACK && panel->target_track_index >= 0) {
            have_meter = engine_get_track_meter_snapshot(state->engine, panel->target_track_index, &meter);
        } else if (panel->target == FX_PANEL_TARGET_MASTER) {
            have_meter = engine_get_master_meter_snapshot(state->engine, &meter);
        }
        if (have_meter) {
            meter_peak_db = linear_to_db(meter.peak);
            meter_rms_db = linear_to_db(meter.rms);
            meter_peak_db = clampf(meter_peak_db, FX_PANEL_METER_DB_MIN, FX_PANEL_METER_DB_MAX);
            meter_rms_db = clampf(meter_rms_db, FX_PANEL_METER_DB_MIN, FX_PANEL_METER_DB_MAX);
            meter_clipped = meter.clipped;
        }
    }
    draw_meter(renderer, snap, meter_peak_db, meter_rms_db, meter_clipped);

    if (panel->target == FX_PANEL_TARGET_TRACK &&
        panel->track_snapshot.instrument_menu_open &&
        snap->instrument_menu_rect.w > 0 &&
        snap->instrument_menu_rect.h > 0) {
        EngineInstrumentPresetId preset = state->engine
                                              ? engine_track_midi_instrument_preset(state->engine,
                                                                                    panel->target_track_index)
                                              : ENGINE_INSTRUMENT_PRESET_PURE_SINE;
        DawThemePalette daw_theme;
        if (!daw_shared_theme_resolve_palette(&daw_theme)) {
            return;
        }
        midi_preset_browser_draw(renderer, &snap->instrument_browser, preset, &daw_theme);
    }
}
