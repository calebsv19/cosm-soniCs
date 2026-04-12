#include "ui/clip_inspector.h"

#include "app_state.h"
#include "engine/engine.h"
#include "engine/audio_source.h"
#include "engine/sampler.h"
#include "audio/media_registry.h"
#include "ui/font.h"
#include "ui/clip_inspector_controls.h"
#include "ui/layout.h"
#include "ui/clip_inspector_waveform.h"
#include "ui/render_utils.h"
#include "ui/shared_theme_font_adapter.h"

#include <math.h>
#include <string.h>

#define INSPECTOR_MARGIN 12
#define INSPECTOR_COL_GAP 16
#define INSPECTOR_ROW_HEIGHT 18
#define INSPECTOR_ROW_GAP 6
#define INSPECTOR_SECTION_GAP 10
#define INSPECTOR_LABEL_SCALE 1.0f
#define INSPECTOR_VALUE_SCALE 1.0f
#define INSPECTOR_SLIDER_HEIGHT 6
#define INSPECTOR_PRESET_GAP 6
#define INSPECTOR_WAVE_HEADER_HEIGHT 26
#define INSPECTOR_WAVE_HEADER_PAD 8
#define INSPECTOR_WAVE_SECTION_GAP 10
#define INSPECTOR_WAVE_HEIGHT 140

static int max_int(int a, int b) { return (a > b) ? a : b; }
static int min_int(int a, int b) { return (a < b) ? a : b; }

static void resolve_inspector_theme(DawThemePalette* palette) {
    if (!palette) {
        return;
    }
    if (!daw_shared_theme_resolve_palette(palette)) {
        *palette = (DawThemePalette){
            .timeline_fill = {32, 32, 40, 255},
            .inspector_fill = {28, 28, 36, 255},
            .pane_border = {70, 75, 92, 255},
            .control_fill = {48, 52, 62, 255},
            .control_active_fill = {120, 160, 220, 255},
            .control_border = {90, 95, 110, 255},
            .slider_track = {36, 36, 44, 255},
            .slider_handle = {80, 120, 170, 220},
            .text_primary = {220, 220, 230, 255},
            .text_muted = {180, 180, 190, 255},
            .selection_fill = {120, 160, 220, 40},
            .accent_primary = {140, 180, 240, 120},
            .accent_warning = {255, 210, 110, 220}
        };
    }
}

static EngineRuntimeConfig clip_inspector_active_config(const AppState* state) {
    EngineRuntimeConfig cfg;
    config_set_defaults(&cfg);
    if (!state) {
        return cfg;
    }
    cfg = state->runtime_cfg;
    if (state->engine) {
        const EngineRuntimeConfig* engine_cfg = engine_get_config(state->engine);
        if (engine_cfg) {
            cfg = *engine_cfg;
        }
    }
    return cfg;
}

static void zero_layout(ClipInspectorLayout* layout) {
    if (!layout) {
        return;
    }
    SDL_zero(*layout);
}

static void draw_button(SDL_Renderer* renderer,
                        const SDL_Rect* rect,
                        const char* label,
                        bool active,
                        const DawThemePalette* theme) {
    if (!renderer || !rect || !label) {
        return;
    }
    SDL_Color base = theme ? theme->control_fill : (SDL_Color){48, 52, 62, 255};
    SDL_Color highlight = theme ? theme->control_active_fill : (SDL_Color){120, 160, 220, 255};
    SDL_Color border = theme ? theme->control_border : (SDL_Color){90, 95, 110, 255};
    SDL_Color text = theme ? theme->text_primary : (SDL_Color){220, 220, 230, 255};

    SDL_Color fill = active ? highlight : base;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, rect);

    float scale = 1.0f;
    int text_w = ui_measure_text_width(label, scale);
    int text_h = ui_font_line_height(scale);
    int text_pad = 4;
    int text_x = rect->x + (rect->w - text_w) / 2;
    int text_y = rect->y + (rect->h - text_h) / 2;
    int max_text_w = rect->w - text_pad * 2;
    if (text_w <= max_text_w) {
        ui_draw_text(renderer, text_x, text_y, label, text, scale);
    } else if (max_text_w > 0) {
        ui_draw_text_clipped(renderer, rect->x + text_pad, text_y, label, text, scale, max_text_w);
    }
}

static const char* inspector_basename(const char* path, char* scratch, size_t scratch_len) {
    if (!scratch || scratch_len == 0) {
        return "";
    }
    scratch[0] = '\0';
    if (!path || path[0] == '\0') {
        return scratch;
    }
    const char* base = strrchr(path, '/');
#if defined(_WIN32)
    const char* alt = strrchr(path, '\\');
    if (!base || (alt && alt > base)) {
        base = alt;
    }
#endif
    base = base ? base + 1 : path;
    SDL_strlcpy(scratch, base, scratch_len);
    char* dot = strrchr(scratch, '.');
    if (dot) {
        *dot = '\0';
    }
    return scratch;
}

static const char* inspector_clip_display_name(const AppState* state,
                                               const EngineClip* clip,
                                               char* scratch,
                                               size_t scratch_len) {
    if (!clip) {
        return "(unnamed clip)";
    }
    if (clip->name[0] != '\0') {
        return clip->name;
    }
    const char* media_id = engine_clip_get_media_id(clip);
    if (state && media_id && media_id[0] != '\0') {
        const MediaRegistryEntry* entry = media_registry_find_by_id(&state->media_registry, media_id);
        if (entry) {
            if (entry->name[0] != '\0') {
                return entry->name;
            }
            if (entry->path[0] != '\0') {
                return inspector_basename(entry->path, scratch, scratch_len);
            }
        }
    }
    const char* media_path = engine_clip_get_media_path(clip);
    if (media_path && media_path[0] != '\0') {
        return inspector_basename(media_path, scratch, scratch_len);
    }
    return "(unnamed clip)";
}

// Assigns label/value rectangles for a left-column inspector row.
static void clip_inspector_set_row(ClipInspectorLayout* layout,
                                   ClipInspectorRowType row,
                                   int label_x,
                                   int label_w,
                                   int value_x,
                                   int value_w,
                                   int y,
                                   int h) {
    if (!layout || row < 0 || row >= CLIP_INSPECTOR_ROW_COUNT) {
        return;
    }
    layout->rows[row].label_rect = (SDL_Rect){label_x, y, label_w, h};
    layout->rows[row].value_rect = (SDL_Rect){value_x, y, value_w, h};
}

// Computes the waveform view range used by the inspector panel.
bool clip_inspector_get_waveform_view(const AppState* state,
                                      const EngineClip* clip,
                                      uint64_t clip_frames,
                                      uint64_t* view_start,
                                      uint64_t* view_frames) {
    if (!state || !clip || !view_start || !view_frames) {
        return false;
    }
    bool view_source = state->inspector.waveform.view_source;
    uint64_t total = 0;
    if (clip->media && clip->media->frame_count > 0) {
        total = clip->media->frame_count;
    }
    if (view_source) {
        if (total == 0) {
            return false;
        }
        float zoom = state->inspector.waveform.zoom;
        if (zoom < 1.0f) zoom = 1.0f;
        uint64_t frames = (uint64_t)llround((double)total / (double)zoom);
        if (frames < 1) frames = 1;
        if (frames > total) frames = total;
        float scroll = state->inspector.waveform.scroll;
        if (scroll < 0.0f) scroll = 0.0f;
        if (scroll > 1.0f) scroll = 1.0f;
        uint64_t max_start = total > frames ? total - frames : 0;
        *view_start = (uint64_t)llround((double)max_start * (double)scroll);
        *view_frames = frames;
        return true;
    }

    *view_start = clip->offset_frames;
    *view_frames = clip_frames > 0 ? clip_frames : 1;
    return true;
}

void clip_inspector_compute_layout(const AppState* state, ClipInspectorLayout* layout) {
    if (!state || !layout) return;

    zero_layout(layout);

    // ---------- Host pane ----------
    const Pane* mixer = ui_layout_get_pane(state, 2);
    if (!mixer) return;
    layout->panel_rect = mixer->rect;

    // ---------- Metrics / constants ----------
    const int label_line_h = ui_font_line_height(INSPECTOR_LABEL_SCALE);
    const int value_line_h = ui_font_line_height(INSPECTOR_VALUE_SCALE);
    const int text_line_h = max_int(label_line_h, value_line_h);
    const int text_pad_y = max_int(2, text_line_h / 3);
    const int M = max_int(INSPECTOR_MARGIN, text_line_h / 2 + 4);
    const int col_gap = max_int(INSPECTOR_COL_GAP, text_line_h / 2 + 8);
    const int row_h = max_int(INSPECTOR_ROW_HEIGHT, text_line_h + text_pad_y * 2);
    const int row_gap = max_int(INSPECTOR_ROW_GAP, text_line_h / 2);
    const int section_gap = max_int(INSPECTOR_SECTION_GAP, text_line_h / 2 + 2);
    const int slider_h = min_int(row_h - 2, max_int(INSPECTOR_SLIDER_HEIGHT, text_line_h / 2));
    const int value_gap = max_int(8, text_line_h / 3);
    const int right_pad = max_int(INSPECTOR_WAVE_HEADER_PAD, text_line_h / 2);
    const int wave_section_gap = max_int(INSPECTOR_WAVE_SECTION_GAP, text_line_h / 2 + 2);
    const int header_h = max_int(INSPECTOR_WAVE_HEADER_HEIGHT, text_line_h + text_pad_y * 2);
    const int wave_h = max_int(INSPECTOR_WAVE_HEIGHT, text_line_h * 7);
    const int preset_gap = max_int(INSPECTOR_PRESET_GAP, text_line_h / 3 + 2);
    const int content_x = mixer->rect.x + M;
    const int content_y = mixer->rect.y + M;
    const int content_w = mixer->rect.w - 2 * M;
    const int content_h = mixer->rect.h - 2 * M;
    if (content_w <= 0 || content_h <= 0) {
        return;
    }

    int mode_label_source_w = ui_measure_text_width("Source", 1.0f);
    int mode_label_clip_w = ui_measure_text_width("Clip", 1.0f);
    int mode_w = max_int(mode_label_source_w, mode_label_clip_w) + 14;
    int header_min_w = ui_measure_text_width("Source View", 1.0f) + (mode_w * 2) + preset_gap + right_pad * 2 + 12;

    int left_w = (int)(content_w * 0.25f);
    int right_w = content_w - left_w - col_gap;
    const int min_left = max_int(220,
                                 ui_measure_text_width("Timeline Length", INSPECTOR_LABEL_SCALE) +
                                     ui_measure_text_width("00:00.000", INSPECTOR_VALUE_SCALE) +
                                     value_gap + 24);
    const int min_right = max_int(220, header_min_w);
    if (left_w < min_left) left_w = min_left;
    right_w = content_w - left_w - col_gap;
    if (right_w < min_right) {
        right_w = min_right;
        left_w = content_w - right_w - col_gap;
        if (left_w < min_left) left_w = min_left;
    }

    layout->left_rect = (SDL_Rect){content_x, content_y, left_w, content_h};
    layout->right_rect = (SDL_Rect){content_x + left_w + col_gap, content_y, right_w, content_h};

    int right_inner_x = layout->right_rect.x + right_pad;
    int right_inner_w = layout->right_rect.w - right_pad * 2;
    int right_inner_h = layout->right_rect.h - right_pad * 2;
    if (right_inner_w < 0) right_inner_w = 0;
    if (right_inner_h < 0) right_inner_h = 0;
    int right_header_h = header_h;
    if (right_header_h > right_inner_h) {
        right_header_h = right_inner_h;
    }
    int available_wave_detail_h = right_inner_h - right_header_h - wave_section_gap * 2;
    if (available_wave_detail_h < 0) {
        available_wave_detail_h = 0;
    }
    int min_detail_h = max_int(row_h * 3, text_line_h * 4);
    if (min_detail_h > available_wave_detail_h) {
        min_detail_h = available_wave_detail_h;
    }
    int right_wave_h = wave_h;
    int max_wave_h = available_wave_detail_h - min_detail_h;
    if (max_wave_h < 0) {
        max_wave_h = 0;
    }
    if (right_wave_h > max_wave_h) {
        right_wave_h = max_wave_h;
    }
    int right_y = layout->right_rect.y + right_pad;
    layout->right_header_rect = (SDL_Rect){right_inner_x, right_y, right_inner_w, right_header_h};
    right_y += right_header_h + wave_section_gap;
    layout->right_waveform_rect = (SDL_Rect){right_inner_x, right_y, right_inner_w, right_wave_h};
    right_y += right_wave_h + wave_section_gap;
    int detail_h = layout->right_rect.y + layout->right_rect.h - right_pad - right_y;
    if (detail_h < 0) detail_h = 0;
    layout->right_detail_rect = (SDL_Rect){right_inner_x, right_y, right_inner_w, detail_h};

    int mode_h = header_h - max_int(2, text_pad_y - 1) * 2;
    if (mode_h < text_line_h + 4) mode_h = text_line_h + 4;
    if (mode_h > header_h) mode_h = header_h;
    int max_mode_w = (layout->right_header_rect.w - preset_gap) / 2;
    if (mode_w > max_mode_w) mode_w = max_mode_w;
    if (mode_w < 0) mode_w = 0;
    int mode_y = layout->right_header_rect.y + (layout->right_header_rect.h - mode_h) / 2;
    int mode_x = layout->right_header_rect.x + layout->right_header_rect.w - mode_w * 2 - preset_gap;
    if (mode_x < layout->right_header_rect.x) mode_x = layout->right_header_rect.x;
    layout->right_mode_source_rect = (SDL_Rect){mode_x, mode_y, mode_w, mode_h};
    layout->right_mode_clip_rect = (SDL_Rect){mode_x + mode_w + preset_gap, mode_y, mode_w, mode_h};

    static const char* k_row_labels[] = {
        "Timeline Start", "Timeline End", "Timeline Length", "Source Start",
        "Source End", "Playback Rate", "Phase Invert", "Normalize", "Reverse"
    };
    int label_min_w = 0;
    for (size_t i = 0; i < sizeof(k_row_labels) / sizeof(k_row_labels[0]); ++i) {
        int w = ui_measure_text_width(k_row_labels[i], INSPECTOR_LABEL_SCALE);
        if (w > label_min_w) {
            label_min_w = w;
        }
    }
    int min_value_w = max_int(ui_measure_text_width("00:00.000", INSPECTOR_VALUE_SCALE) + 18,
                              ui_measure_text_width("100.0%", INSPECTOR_VALUE_SCALE) + 18);
    int label_w = max_int((int)(left_w * 0.38f), label_min_w + 12);
    int max_label_w = left_w - value_gap - min_value_w;
    if (max_label_w < 0) {
        max_label_w = 0;
    }
    if (label_w > max_label_w) {
        label_w = max_label_w;
    }
    {
        int preferred_min_label_w = 80;
        if (preferred_min_label_w > max_label_w) {
            preferred_min_label_w = max_label_w;
        }
        if (label_w < preferred_min_label_w) {
            label_w = preferred_min_label_w;
        }
    }
    int label_x = layout->left_rect.x;
    int value_x = label_x + label_w + value_gap;
    int value_w = left_w - label_w - value_gap;
    if (value_w < 0) value_w = 0;

    int y = layout->left_rect.y;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_NAME, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_SOURCE, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_FORMAT, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + section_gap;

    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_TIMELINE_START, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_TIMELINE_END, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_TIMELINE_LENGTH, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_SOURCE_START, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_SOURCE_END, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_PLAYBACK_RATE, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + section_gap;

    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_GAIN, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_FADE_IN, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_FADE_OUT, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + section_gap;

    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_PHASE, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_NORMALIZE, label_x, label_w, value_x, value_w, y, row_h);
    y += row_h + row_gap;
    clip_inspector_set_row(layout, CLIP_INSPECTOR_ROW_REVERSE, label_x, label_w, value_x, value_w, y, row_h);

    layout->name_rect = layout->rows[CLIP_INSPECTOR_ROW_NAME].value_rect;
    layout->name_rect.h = row_h;

    SDL_Rect gain_row = layout->rows[CLIP_INSPECTOR_ROW_GAIN].value_rect;
    SDL_Rect fade_in_row = layout->rows[CLIP_INSPECTOR_ROW_FADE_IN].value_rect;
    SDL_Rect fade_out_row = layout->rows[CLIP_INSPECTOR_ROW_FADE_OUT].value_rect;
    int slider_y_offset = (row_h - slider_h) / 2;
    layout->gain_track_rect = (SDL_Rect){gain_row.x, gain_row.y + slider_y_offset, gain_row.w, slider_h};
    layout->gain_fill_rect = layout->gain_track_rect;
    layout->gain_handle_rect = layout->gain_track_rect;
    layout->fade_in_track_rect = (SDL_Rect){fade_in_row.x, fade_in_row.y + slider_y_offset, fade_in_row.w, slider_h};
    layout->fade_in_fill_rect = layout->fade_in_track_rect;
    layout->fade_in_handle_rect = layout->fade_in_track_rect;
    layout->fade_out_track_rect = (SDL_Rect){fade_out_row.x, fade_out_row.y + slider_y_offset, fade_out_row.w, slider_h};
    layout->fade_out_fill_rect = layout->fade_out_track_rect;
    layout->fade_out_handle_rect = layout->fade_out_track_rect;

    int control_inset_y = max_int(2, text_pad_y / 2);
    int control_h = row_h - control_inset_y * 2;
    if (control_h < text_line_h + 2) {
        control_h = text_line_h + 2;
    }
    if (control_h > row_h) {
        control_h = row_h;
    }

    SDL_Rect phase_row = layout->rows[CLIP_INSPECTOR_ROW_PHASE].value_rect;
    int phase_button_w = (phase_row.w - preset_gap) / 2;
    if (phase_button_w < 0) phase_button_w = 0;
    layout->phase_left_rect = (SDL_Rect){phase_row.x, phase_row.y + control_inset_y, phase_button_w, control_h};
    layout->phase_right_rect = (SDL_Rect){phase_row.x + phase_button_w + preset_gap,
                                          phase_row.y + control_inset_y,
                                          phase_button_w,
                                          control_h};

    int toggle_min_w = max_int(ui_measure_text_width("On", 1.0f),
                               ui_measure_text_width("Off", 1.0f)) + 14;
    SDL_Rect normalize_row = layout->rows[CLIP_INSPECTOR_ROW_NORMALIZE].value_rect;
    int normalize_w = min_int(normalize_row.w, max_int(52, toggle_min_w));
    if (normalize_w < 0) normalize_w = 0;
    layout->normalize_toggle_rect = (SDL_Rect){normalize_row.x,
                                               normalize_row.y + control_inset_y,
                                               normalize_w,
                                               control_h};
    SDL_Rect reverse_row = layout->rows[CLIP_INSPECTOR_ROW_REVERSE].value_rect;
    int reverse_w = min_int(reverse_row.w, max_int(52, toggle_min_w));
    if (reverse_w < 0) reverse_w = 0;
    layout->reverse_toggle_rect = (SDL_Rect){reverse_row.x,
                                             reverse_row.y + control_inset_y,
                                             reverse_w,
                                             control_h};
}

void clip_inspector_render(SDL_Renderer* renderer, AppState* state, const ClipInspectorLayout* layout) {
    DawThemePalette theme = {0};
    if (!renderer || !state || !layout) {
        return;
    }
    resolve_inspector_theme(&theme);

    SDL_Color label = theme.text_muted;
    SDL_Color muted = theme.text_muted;
    SDL_Color box_bg = theme.slider_track;
    SDL_Color panel_bg = theme.inspector_fill;
    SDL_Color panel_border = theme.pane_border;

    if (layout->panel_rect.w > 0 && layout->panel_rect.h > 0) {
        SDL_SetRenderDrawColor(renderer, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
        SDL_RenderFillRect(renderer, &layout->panel_rect);
        SDL_SetRenderDrawColor(renderer, panel_border.r, panel_border.g, panel_border.b, panel_border.a);
        SDL_RenderDrawRect(renderer, &layout->panel_rect);
    }

    if (layout->left_rect.w > 0 && layout->left_rect.h > 0) {
        SDL_SetRenderDrawColor(renderer, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
        SDL_RenderFillRect(renderer, &layout->left_rect);
        SDL_SetRenderDrawColor(renderer, panel_border.r, panel_border.g, panel_border.b, panel_border.a);
        SDL_RenderDrawRect(renderer, &layout->left_rect);
    }

    const EngineTrack* tracks = engine_get_tracks(state->engine);
    int track_count = engine_get_track_count(state->engine);
    if (!tracks || state->inspector.track_index < 0 || state->inspector.track_index >= track_count) {
        ui_draw_text(renderer, layout->panel_rect.x + 16, layout->panel_rect.y + 48, "Clip unavailable.", label, 1.0f);
        return;
    }
    const EngineTrack* track = &tracks[state->inspector.track_index];
    if (!track || state->inspector.clip_index < 0 || state->inspector.clip_index >= track->clip_count) {
        ui_draw_text(renderer, layout->panel_rect.x + 16, layout->panel_rect.y + 48, "Clip unavailable.", label, 1.0f);
        return;
    }

    const EngineClip* clip = &track->clips[state->inspector.clip_index];
    EngineRuntimeConfig runtime_cfg = clip_inspector_active_config(state);

    char name_buf[ENGINE_CLIP_NAME_MAX];
    const char* display_name = inspector_clip_display_name(state, clip, name_buf, sizeof(name_buf));
    if (state->inspector.editing_name) {
        display_name = state->inspector.name;
    }

    char source_buf[ENGINE_CLIP_PATH_MAX];
    const char* source_name = "";
    const char* source_path = NULL;
    const char* media_id = engine_clip_get_media_id(clip);
    if (state && media_id && media_id[0] != '\0') {
        const MediaRegistryEntry* entry = media_registry_find_by_id(&state->media_registry, media_id);
        if (entry && entry->path[0] != '\0') {
            source_path = entry->path;
            source_name = inspector_basename(entry->path, source_buf, sizeof(source_buf));
        }
    }
    if (source_name[0] == '\0') {
        const char* media_path = engine_clip_get_media_path(clip);
        if (media_path && media_path[0] != '\0') {
            source_path = media_path;
            source_name = inspector_basename(media_path, source_buf, sizeof(source_buf));
        }
    }
    if (source_name[0] == '\0') {
        source_name = "(no source)";
    }

    int sample_rate = 0;
    if (clip->media) {
        sample_rate = clip->media->sample_rate;
    } else if (clip->source) {
        sample_rate = clip->source->sample_rate;
    }
    if (sample_rate <= 0) {
        sample_rate = runtime_cfg.sample_rate;
    }

    uint64_t total_frames = 0;
    if (clip->media && clip->media->frame_count > 0) {
        total_frames = clip->media->frame_count;
    } else if (state->engine) {
        total_frames = engine_clip_get_total_frames(state->engine,
                                                    state->inspector.track_index,
                                                    state->inspector.clip_index);
    }

    uint64_t clip_frames = clip->duration_frames;
    if (clip_frames == 0 && total_frames > clip->offset_frames) {
        clip_frames = total_frames - clip->offset_frames;
    }
    if (clip_frames == 0 && clip->sampler) {
        clip_frames = engine_sampler_get_frame_count(clip->sampler);
    }
    if (clip_frames == 0) {
        clip_frames = 1;
    }

    uint64_t fade_in_frames = state->inspector.adjusting_fade_in ? state->inspector.fade_in_frames : clip->fade_in_frames;
    uint64_t fade_out_frames = state->inspector.adjusting_fade_out ? state->inspector.fade_out_frames : clip->fade_out_frames;
    if (fade_in_frames > clip_frames) fade_in_frames = clip_frames;
    if (fade_out_frames > clip_frames) fade_out_frames = clip_frames;

    clip_inspector_render_controls_panel(renderer,
                                         state,
                                         layout,
                                         clip,
                                         sample_rate,
                                         clip_frames,
                                         total_frames,
                                         display_name,
                                         source_name,
                                         &theme);

    if (layout->right_rect.w > 0 && layout->right_rect.h > 0) {
        SDL_SetRenderDrawColor(renderer, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
        SDL_RenderFillRect(renderer, &layout->right_rect);
        SDL_SetRenderDrawColor(renderer, panel_border.r, panel_border.g, panel_border.b, panel_border.a);
        SDL_RenderDrawRect(renderer, &layout->right_rect);

        if (layout->right_header_rect.w > 0 && layout->right_header_rect.h > 0) {
            SDL_Rect header_bg = layout->right_header_rect;
            header_bg.x -= 4;
            header_bg.w += 8;
            SDL_SetRenderDrawColor(renderer, box_bg.r, box_bg.g, box_bg.b, box_bg.a);
            SDL_RenderFillRect(renderer, &header_bg);
            bool view_source = state->inspector.waveform.view_source;
            int header_text_h = ui_font_line_height(1.0f);
            int header_text_y = layout->right_header_rect.y + (layout->right_header_rect.h - header_text_h) / 2;
            int header_text_w = layout->right_mode_source_rect.x - layout->right_header_rect.x - 8;
            if (header_text_w < 0) {
                header_text_w = layout->right_header_rect.w;
            }
            ui_draw_text_clipped(renderer,
                                 layout->right_header_rect.x,
                                 header_text_y,
                                 view_source ? "Source View" : "Clip View",
                                 label,
                                 1.0f,
                                 header_text_w);
            draw_button(renderer, &layout->right_mode_source_rect, "Source", view_source, &theme);
            draw_button(renderer, &layout->right_mode_clip_rect, "Clip", !view_source, &theme);
        }

        clip_inspector_render_waveform_panel(renderer,
                                             state,
                                             layout,
                                             clip,
                                             clip_frames,
                                             fade_in_frames,
                                             fade_out_frames,
                                             source_path,
                                             &theme);

        if (layout->right_detail_rect.w > 0 && layout->right_detail_rect.h > 0) {
            SDL_SetRenderDrawColor(renderer, box_bg.r, box_bg.g, box_bg.b, box_bg.a);
            SDL_RenderFillRect(renderer, &layout->right_detail_rect);
            SDL_SetRenderDrawColor(renderer, panel_border.r, panel_border.g, panel_border.b, panel_border.a);
            SDL_RenderDrawRect(renderer, &layout->right_detail_rect);
            ui_draw_text(renderer,
                         layout->right_detail_rect.x + 8,
                         layout->right_detail_rect.y + 8,
                         "Clip overlays (coming soon)",
                         muted,
                         1.0f);
        }
    }
}
