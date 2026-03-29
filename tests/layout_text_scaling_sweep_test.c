#include "app_state.h"
#include "input/timeline/timeline_geometry.h"
#include "ui/clip_inspector.h"
#include "ui/effects_panel.h"
#include "ui/effects_panel_eq_detail.h"
#include "ui/effects_panel_meter_detail.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/library_browser.h"
#include "ui/shared_theme_font_adapter.h"
#include "ui/timeline_view.h"
#include "ui/transport.h"

#include <SDL2/SDL_ttf.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SweepWindowSize {
    int w;
    int h;
} SweepWindowSize;

static void fail(const char* msg) {
    fprintf(stderr, "layout_text_scaling_sweep_test: %s\n", msg);
    exit(1);
}

static void expect(bool cond, const char* msg) {
    if (!cond) {
        fail(msg);
    }
}

static bool rect_has_positive_size(const SDL_Rect* rect) {
    return rect && rect->w > 0 && rect->h > 0;
}

static bool rect_contains_rect(const SDL_Rect* outer, const SDL_Rect* inner) {
    if (!outer || !inner) {
        return false;
    }
    return inner->x >= outer->x &&
           inner->y >= outer->y &&
           inner->x + inner->w <= outer->x + outer->w &&
           inner->y + inner->h <= outer->y + outer->h;
}

static bool rects_overlap_strict(const SDL_Rect* a, const SDL_Rect* b) {
    if (!rect_has_positive_size(a) || !rect_has_positive_size(b)) {
        return false;
    }
    return a->x < b->x + b->w &&
           a->x + a->w > b->x &&
           a->y < b->y + b->h &&
           a->y + a->h > b->y;
}

static int rect_center_x(const SDL_Rect* rect) {
    if (!rect) {
        return 0;
    }
    return rect->x + rect->w / 2;
}

static int rect_center_y(const SDL_Rect* rect) {
    if (!rect) {
        return 0;
    }
    return rect->y + rect->h / 2;
}

static void set_test_font_for_zoom_step(int zoom_step) {
    char font_path[256] = {0};
    int point_size = 9;
    daw_shared_font_set_zoom_step(zoom_step);
    if (daw_shared_font_zoom_step() != zoom_step) {
        fail("zoom step did not apply");
    }
    if (daw_shared_font_resolve_ui_regular(font_path, sizeof(font_path), &point_size)) {
        expect(ui_font_set(font_path, point_size), "ui_font_set(shared) failed");
    } else {
        expect(ui_font_set("include/fonts/Montserrat/Montserrat-Regular.ttf", 9),
               "ui_font_set(fallback) failed");
    }
}

static void seed_effects_panel_meter_slots(EffectsPanelState* panel) {
    static const FxTypeId k_meter_types[] = {100u, 101u, 102u, 103u, 104u, 105u};
    static const char* k_meter_names[] = {
        "Correlation Meter",
        "Mid Side Meter",
        "Vector Scope Meter",
        "Peak RMS Level Meter",
        "Integrated LUFS Meter",
        "Spectrogram Meter"
    };

    panel->view_mode = FX_PANEL_VIEW_LIST;
    panel->list_detail_mode = FX_LIST_DETAIL_METER;
    panel->chain_count = (int)(sizeof(k_meter_types) / sizeof(k_meter_types[0]));
    panel->type_count = panel->chain_count;
    panel->list_open_slot_index = 0;
    panel->selected_slot_index = 0;
    strncpy(panel->target_label, "Ultra Long Target Track Name For Layout Verification",
            sizeof(panel->target_label) - 1);
    panel->target_label[sizeof(panel->target_label) - 1] = '\0';
    panel->target = FX_PANEL_TARGET_TRACK;
    panel->target_track_index = 3;

    for (int i = 0; i < panel->chain_count; ++i) {
        panel->chain[i].id = (FxInstId)(1000 + i);
        panel->chain[i].type_id = k_meter_types[i];
        panel->chain[i].param_count = 3;
        panel->chain[i].enabled = true;
        panel->types[i].type_id = k_meter_types[i];
        strncpy(panel->types[i].name, k_meter_names[i], sizeof(panel->types[i].name) - 1);
        panel->types[i].name[sizeof(panel->types[i].name) - 1] = '\0';
    }
}

static void validate_header_layout(const EffectsPanelLayout* layout) {
    expect(rect_has_positive_size(&layout->view_toggle_rect), "view toggle rect invalid");
    expect(rect_has_positive_size(&layout->spec_toggle_rect), "spec toggle rect invalid");
    expect(rect_has_positive_size(&layout->dropdown_button_rect), "add fx rect invalid");
    expect(rect_has_positive_size(&layout->preview_toggle_rect), "preview rect invalid");
    expect(rect_has_positive_size(&layout->target_label_rect), "target label rect invalid");

    expect(layout->spec_toggle_rect.x >= layout->view_toggle_rect.x + layout->view_toggle_rect.w,
           "spec toggle overlaps view toggle");
    expect(layout->dropdown_button_rect.x >= layout->spec_toggle_rect.x + layout->spec_toggle_rect.w,
           "add fx overlaps spec toggle");
    expect(layout->preview_toggle_rect.x >= layout->dropdown_button_rect.x + layout->dropdown_button_rect.w,
           "preview overlaps add fx");
    expect(layout->target_label_rect.x >= layout->preview_toggle_rect.x + layout->preview_toggle_rect.w,
           "target label overlaps preview");
}

static void validate_eq_detail_layout(const SDL_Rect* detail_rect) {
    SDL_Rect selector_master = {0};
    SDL_Rect selector_track = {0};
    SDL_Rect toggle_low = {0};
    SDL_Rect toggle_mids[4] = {{0}};
    SDL_Rect toggle_high = {0};
    SDL_Rect graph = {0};

    effects_panel_eq_detail_compute_selector_rects(detail_rect, &selector_master, &selector_track);
    effects_panel_eq_detail_compute_toggle_rects(detail_rect, &toggle_low, toggle_mids, &toggle_high);
    graph = effects_panel_eq_detail_compute_graph_rect(detail_rect);

    expect(rect_has_positive_size(&selector_master), "eq selector master rect invalid");
    expect(rect_has_positive_size(&selector_track), "eq selector track rect invalid");
    expect(rect_has_positive_size(&toggle_low), "eq low toggle rect invalid");
    expect(rect_has_positive_size(&toggle_high), "eq high toggle rect invalid");
    expect(rect_has_positive_size(&graph), "eq graph rect invalid");

    expect(rect_contains_rect(detail_rect, &selector_master), "eq selector master outside detail rect");
    expect(rect_contains_rect(detail_rect, &selector_track), "eq selector track outside detail rect");
    expect(rect_contains_rect(detail_rect, &toggle_low), "eq low toggle outside detail rect");
    expect(rect_contains_rect(detail_rect, &toggle_high), "eq high toggle outside detail rect");
    expect(rect_contains_rect(detail_rect, &graph), "eq graph outside detail rect");

    expect(!rects_overlap_strict(&selector_master, &selector_track), "eq selector buttons overlap");
    expect(!rects_overlap_strict(&toggle_low, &toggle_high), "eq edge toggles overlap");
    for (int i = 0; i < 4; ++i) {
        expect(rect_has_positive_size(&toggle_mids[i]), "eq mid toggle rect invalid");
        expect(rect_contains_rect(detail_rect, &toggle_mids[i]), "eq mid toggle outside detail rect");
        expect(!rects_overlap_strict(&toggle_low, &toggle_mids[i]), "eq low overlaps mid toggle");
        expect(!rects_overlap_strict(&toggle_high, &toggle_mids[i]), "eq high overlaps mid toggle");
    }

    expect(graph.y >= toggle_low.y + toggle_low.h, "eq graph starts above toggle row");
}

static void validate_meter_detail_layout(const SDL_Rect* detail_rect) {
    SDL_Rect toggle_ms = {0};
    SDL_Rect toggle_lr = {0};
    SDL_Rect toggle_int = {0};
    SDL_Rect toggle_short = {0};
    SDL_Rect toggle_momentary = {0};
    SDL_Rect toggle_wb = {0};
    SDL_Rect toggle_bw = {0};
    SDL_Rect toggle_heat = {0};

    effects_panel_meter_detail_compute_toggle_rects(detail_rect, &toggle_ms, &toggle_lr);
    effects_panel_meter_detail_compute_lufs_toggle_rects(detail_rect, &toggle_int, &toggle_short, &toggle_momentary);
    effects_panel_meter_detail_compute_spectrogram_toggle_rects(detail_rect, &toggle_wb, &toggle_bw, &toggle_heat);

    expect(rect_has_positive_size(&toggle_ms), "meter ms toggle rect invalid");
    expect(rect_has_positive_size(&toggle_lr), "meter lr toggle rect invalid");
    expect(rect_has_positive_size(&toggle_int), "meter lufs int rect invalid");
    expect(rect_has_positive_size(&toggle_short), "meter lufs short rect invalid");
    expect(rect_has_positive_size(&toggle_momentary), "meter lufs momentary rect invalid");
    expect(rect_has_positive_size(&toggle_wb), "meter spectrogram wb rect invalid");
    expect(rect_has_positive_size(&toggle_bw), "meter spectrogram bw rect invalid");
    expect(rect_has_positive_size(&toggle_heat), "meter spectrogram heat rect invalid");

    expect(rect_contains_rect(detail_rect, &toggle_ms), "meter ms toggle outside detail rect");
    expect(rect_contains_rect(detail_rect, &toggle_lr), "meter lr toggle outside detail rect");
    expect(rect_contains_rect(detail_rect, &toggle_int), "meter int toggle outside detail rect");
    expect(rect_contains_rect(detail_rect, &toggle_short), "meter short toggle outside detail rect");
    expect(rect_contains_rect(detail_rect, &toggle_momentary), "meter momentary toggle outside detail rect");
    expect(rect_contains_rect(detail_rect, &toggle_wb), "meter wb toggle outside detail rect");
    expect(rect_contains_rect(detail_rect, &toggle_bw), "meter bw toggle outside detail rect");
    expect(rect_contains_rect(detail_rect, &toggle_heat), "meter heat toggle outside detail rect");

    expect(!rects_overlap_strict(&toggle_ms, &toggle_lr), "meter mode toggles overlap");
    expect(!rects_overlap_strict(&toggle_int, &toggle_short), "meter lufs int/short overlap");
    expect(!rects_overlap_strict(&toggle_short, &toggle_momentary), "meter lufs short/momentary overlap");
    expect(!rects_overlap_strict(&toggle_int, &toggle_momentary), "meter lufs int/momentary overlap");
    expect(!rects_overlap_strict(&toggle_wb, &toggle_bw), "meter spectrogram wb/bw overlap");
    expect(!rects_overlap_strict(&toggle_bw, &toggle_heat), "meter spectrogram bw/heat overlap");
    expect(!rects_overlap_strict(&toggle_wb, &toggle_heat), "meter spectrogram wb/heat overlap");
}

static void validate_clip_inspector_layout(const AppState* state) {
    ClipInspectorLayout layout = {0};
    const Pane* pane = ui_layout_get_pane(state, 2);
    expect(pane != NULL, "clip inspector pane missing");
    clip_inspector_compute_layout(state, &layout);

    expect(layout.panel_rect.x == pane->rect.x &&
               layout.panel_rect.y == pane->rect.y &&
               layout.panel_rect.w == pane->rect.w &&
               layout.panel_rect.h == pane->rect.h,
           "clip inspector panel rect mismatch");
    expect(rect_has_positive_size(&layout.left_rect), "clip inspector left rect invalid");
    expect(rect_has_positive_size(&layout.right_rect), "clip inspector right rect invalid");
    expect(rect_contains_rect(&layout.panel_rect, &layout.left_rect), "clip inspector left outside panel");
    expect(rect_contains_rect(&layout.panel_rect, &layout.right_rect), "clip inspector right outside panel");
    expect(!rects_overlap_strict(&layout.left_rect, &layout.right_rect), "clip inspector left/right overlap");

    expect(layout.right_header_rect.w > 0 && layout.right_header_rect.h > 0,
           "clip inspector right header invalid");
    expect(layout.right_waveform_rect.w > 0 && layout.right_waveform_rect.h >= 0,
           "clip inspector right waveform invalid");
    expect(layout.right_detail_rect.w > 0 && layout.right_detail_rect.h >= 0,
           "clip inspector right detail invalid");
    expect(layout.right_waveform_rect.h > 0 || layout.right_detail_rect.h > 0,
           "clip inspector collapsed waveform and detail regions");
    expect(rect_contains_rect(&layout.right_rect, &layout.right_header_rect), "clip inspector header outside right");
    expect(rect_contains_rect(&layout.right_rect, &layout.right_waveform_rect), "clip inspector waveform outside right");
    expect(rect_contains_rect(&layout.right_rect, &layout.right_detail_rect), "clip inspector detail outside right");
    expect(!rects_overlap_strict(&layout.right_header_rect, &layout.right_waveform_rect),
           "clip inspector header/waveform overlap");
    expect(!rects_overlap_strict(&layout.right_waveform_rect, &layout.right_detail_rect),
           "clip inspector waveform/detail overlap");

    expect(rect_has_positive_size(&layout.right_mode_source_rect), "clip inspector source mode invalid");
    expect(rect_has_positive_size(&layout.right_mode_clip_rect), "clip inspector clip mode invalid");
    expect(rect_contains_rect(&layout.right_header_rect, &layout.right_mode_source_rect),
           "clip inspector source mode outside header");
    expect(rect_contains_rect(&layout.right_header_rect, &layout.right_mode_clip_rect),
           "clip inspector clip mode outside header");
    expect(!rects_overlap_strict(&layout.right_mode_source_rect, &layout.right_mode_clip_rect),
           "clip inspector mode toggles overlap");

    for (int i = 0; i < CLIP_INSPECTOR_ROW_COUNT; ++i) {
        const ClipInspectorRow* row = &layout.rows[i];
        expect(rect_has_positive_size(&row->label_rect), "clip inspector label rect invalid");
        expect(rect_has_positive_size(&row->value_rect), "clip inspector value rect invalid");
        expect(row->label_rect.x >= layout.left_rect.x, "clip inspector label x before left");
        expect(row->label_rect.x + row->label_rect.w <= layout.left_rect.x + layout.left_rect.w,
               "clip inspector label x overflows left");
        expect(row->value_rect.x >= layout.left_rect.x, "clip inspector value x before left");
        expect(row->value_rect.x + row->value_rect.w <= layout.left_rect.x + layout.left_rect.w,
               "clip inspector value x overflows left");
        expect(row->label_rect.y >= layout.left_rect.y, "clip inspector label y before left");
        expect(row->value_rect.y >= layout.left_rect.y, "clip inspector value y before left");
        expect(!rects_overlap_strict(&row->label_rect, &row->value_rect), "clip inspector label/value overlap");
        if (i > 0) {
            const ClipInspectorRow* prev = &layout.rows[i - 1];
            expect(row->label_rect.y >= prev->label_rect.y + prev->label_rect.h,
                   "clip inspector row label order regression");
            expect(row->value_rect.y >= prev->value_rect.y + prev->value_rect.h,
                   "clip inspector row value order regression");
        }
    }
}

static void validate_timeline_layout(const AppState* state) {
    const Pane* pane = ui_layout_get_pane(state, 1);
    TimelineGeometry geom = {0};
    TimelineTrackHeaderLayout header = {0};
    SDL_Rect clip_rect = {0};
    int controls_h = 0;
    int ruler_h = 0;
    expect(pane != NULL, "timeline pane missing");

    expect(timeline_compute_geometry(state, pane, &geom), "timeline geometry computation failed");
    controls_h = timeline_view_controls_height_for_width(pane->rect.w);
    ruler_h = timeline_view_ruler_height();
    expect(controls_h >= TIMELINE_CONTROLS_HEIGHT, "timeline controls height below minimum");
    expect(geom.track_top == pane->rect.y + controls_h + ruler_h,
           "timeline track top ignores dynamic controls height");
    expect(geom.content_width > 0, "timeline content width invalid");
    expect(geom.track_height > 0, "timeline track height invalid");
    expect(geom.header_width > 0, "timeline header width invalid");
    expect(geom.pixels_per_second > 0.0f, "timeline pixels per second invalid");
    expect(geom.content_left >= pane->rect.x, "timeline content starts before pane");
    expect(geom.content_left + geom.content_width <= pane->rect.x + pane->rect.w,
           "timeline content exceeds pane bounds");
    expect(geom.track_top >= pane->rect.y, "timeline track top before pane");

    float x0_secs = timeline_x_to_seconds(&geom, geom.content_left);
    int x0_back = timeline_seconds_to_x(&geom, x0_secs);
    expect(abs(x0_back - geom.content_left) <= 1, "timeline seconds<->x conversion unstable");

    timeline_view_compute_track_header_layout(&pane->rect,
                                              geom.track_top,
                                              geom.track_height,
                                              geom.header_width,
                                              &header);
    expect(rect_has_positive_size(&header.header_rect), "timeline header rect invalid");
    expect(rect_has_positive_size(&header.mute_rect), "timeline mute rect invalid");
    expect(rect_has_positive_size(&header.solo_rect), "timeline solo rect invalid");
    expect(rect_contains_rect(&pane->rect, &header.header_rect), "timeline header outside pane");
    expect(rect_contains_rect(&header.header_rect, &header.mute_rect), "timeline mute outside header");
    expect(rect_contains_rect(&header.header_rect, &header.solo_rect), "timeline solo outside header");
    expect(!rects_overlap_strict(&header.mute_rect, &header.solo_rect), "timeline mute/solo overlap");
    expect(header.text_max_x >= header.text_x, "timeline text range invalid");

    timeline_view_compute_lane_clip_rect(geom.track_top,
                                         geom.track_height,
                                         geom.content_left + 10,
                                         120,
                                         &clip_rect);
    expect(rect_has_positive_size(&clip_rect), "timeline lane clip rect invalid");
    expect(clip_rect.y >= geom.track_top, "timeline lane clip starts above lane");
    expect(clip_rect.y + clip_rect.h <= geom.track_top + geom.track_height, "timeline lane clip exceeds lane");
}

static void validate_transport_layout(const AppState* state) {
    const Pane* pane = ui_layout_get_pane(state, 0);
    const TransportUI* ui = state ? &state->transport_ui : NULL;
    TransportUI hover = {0};
    expect(pane != NULL, "transport pane missing");
    expect(ui != NULL, "transport ui missing");

    expect(ui->panel_rect.x == pane->rect.x &&
               ui->panel_rect.y == pane->rect.y &&
               ui->panel_rect.w == pane->rect.w &&
               ui->panel_rect.h == pane->rect.h,
           "transport panel rect mismatch");

    expect(rect_has_positive_size(&ui->load_rect), "transport load rect invalid");
    expect(rect_has_positive_size(&ui->save_rect), "transport save rect invalid");
    expect(rect_has_positive_size(&ui->play_rect), "transport play rect invalid");
    expect(rect_has_positive_size(&ui->stop_rect), "transport stop rect invalid");
    expect(rect_has_positive_size(&ui->grid_rect), "transport grid rect invalid");
    expect(rect_has_positive_size(&ui->time_label_rect), "transport time rect invalid");
    expect(rect_has_positive_size(&ui->bpm_rect), "transport bpm rect invalid");
    expect(rect_has_positive_size(&ui->ts_rect), "transport ts rect invalid");
    expect(rect_has_positive_size(&ui->seek_track_rect), "transport seek track invalid");
    expect(rect_has_positive_size(&ui->window_track_rect), "transport window track invalid");
    expect(rect_has_positive_size(&ui->horiz_track_rect), "transport horizontal track invalid");
    expect(rect_has_positive_size(&ui->vert_track_rect), "transport vertical track invalid");
    expect(rect_has_positive_size(&ui->fit_width_rect), "transport fit-width rect invalid");
    expect(rect_has_positive_size(&ui->fit_height_rect), "transport fit-height rect invalid");

    expect(rect_contains_rect(&ui->panel_rect, &ui->load_rect), "transport load outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->save_rect), "transport save outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->play_rect), "transport play outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->stop_rect), "transport stop outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->grid_rect), "transport grid outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->time_label_rect), "transport time outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->bpm_rect), "transport bpm outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->ts_rect), "transport ts outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->seek_track_rect), "transport seek outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->window_track_rect), "transport window outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->horiz_track_rect), "transport horizontal outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->vert_track_rect), "transport vertical outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->fit_width_rect), "transport fit-width outside panel");
    expect(rect_contains_rect(&ui->panel_rect, &ui->fit_height_rect), "transport fit-height outside panel");

    expect(rect_contains_rect(&ui->ts_rect, &ui->ts_num_rect), "transport ts num outside ts rect");
    expect(rect_contains_rect(&ui->ts_rect, &ui->ts_den_rect), "transport ts den outside ts rect");
    expect(!rects_overlap_strict(&ui->ts_num_rect, &ui->ts_den_rect), "transport ts num/den overlap");

    expect(ui->play_rect.y > ui->load_rect.y, "transport play should be below load");
    expect(ui->stop_rect.y > ui->save_rect.y, "transport stop should be below save");
    expect(ui->grid_rect.x >= ui->time_label_rect.x + ui->time_label_rect.w,
           "transport grid overlaps time group");
    expect(!rects_overlap_strict(&ui->fit_width_rect, &ui->fit_height_rect), "transport fit buttons overlap");

    expect(rect_has_positive_size(&ui->seek_handle_rect), "transport seek handle invalid");
    expect(rect_has_positive_size(&ui->window_handle_rect), "transport window handle invalid");
    expect(rect_has_positive_size(&ui->horiz_handle_rect), "transport horizontal handle invalid");
    expect(rect_has_positive_size(&ui->vert_handle_rect), "transport vertical handle invalid");
    expect(ui->seek_handle_rect.x >= ui->seek_track_rect.x &&
               ui->seek_handle_rect.x + ui->seek_handle_rect.w <= ui->seek_track_rect.x + ui->seek_track_rect.w,
           "transport seek handle out of track range");
    expect(ui->window_handle_rect.x >= ui->window_track_rect.x &&
               ui->window_handle_rect.x + ui->window_handle_rect.w <= ui->window_track_rect.x + ui->window_track_rect.w,
           "transport window handle out of track range");
    expect(ui->horiz_handle_rect.x >= ui->horiz_track_rect.x &&
               ui->horiz_handle_rect.x + ui->horiz_handle_rect.w <= ui->horiz_track_rect.x + ui->horiz_track_rect.w,
           "transport horizontal handle out of track range");
    expect(ui->vert_handle_rect.x >= ui->vert_track_rect.x &&
               ui->vert_handle_rect.x + ui->vert_handle_rect.w <= ui->vert_track_rect.x + ui->vert_track_rect.w,
           "transport vertical handle out of track range");

    expect(transport_ui_click_play(ui, rect_center_x(&ui->play_rect), rect_center_y(&ui->play_rect)),
           "transport play click helper mismatch");
    expect(transport_ui_click_stop(ui, rect_center_x(&ui->stop_rect), rect_center_y(&ui->stop_rect)),
           "transport stop click helper mismatch");

    hover = *ui;
    transport_ui_update_hover(&hover, rect_center_x(&ui->grid_rect), rect_center_y(&ui->grid_rect));
    expect(hover.grid_hovered, "transport grid hover mismatch");
    hover = *ui;
    transport_ui_update_hover(&hover, rect_center_x(&ui->bpm_rect), rect_center_y(&ui->bpm_rect));
    expect(hover.bpm_hovered, "transport bpm hover mismatch");
}

static void validate_library_layout(const AppState* state) {
    const Pane* pane = ui_layout_get_pane(state, 3);
    SDL_Rect content = {0};
    LibraryBrowser browser = {0};
    int header_h = 0;
    int row_h = 0;
    int x = 0;
    int y0 = 0;
    int y1 = 0;
    expect(pane != NULL, "library pane missing");

    header_h = ui_layout_pane_header_height(pane);
    content = ui_layout_pane_content_rect(pane);
    expect(header_h >= 0, "library header height negative");
    expect(content.y == pane->rect.y + header_h, "library content y mismatch");
    expect(content.h >= 0, "library content height negative");
    expect(rect_contains_rect(&pane->rect, &content), "library content outside pane");

    row_h = library_browser_row_height();
    expect(row_h > 0, "library row height invalid");

    browser.count = 3;
    x = content.x + content.w / 2;
    if (x < content.x) {
        x = content.x;
    }
    y0 = content.y + 8 + row_h / 2;
    y1 = y0 + row_h;
    expect(library_browser_hit_test(&browser, &content, x, y0) == 0, "library hit row 0 failed");
    expect(library_browser_hit_test(&browser, &content, x, y1) == 1, "library hit row 1 failed");
    expect(library_browser_hit_test(&browser, &content, content.x - 1, y0) == -1, "library x-outside hit expected -1");
    expect(library_browser_hit_test(&browser, &content, x, content.y + 7) == -1, "library pre-content hit expected -1");
    expect(library_browser_hit_test(&browser, &content, x, y0 + row_h * 4) == -1, "library out-of-range row hit expected -1");
}

static void run_sweep_case(int zoom_step, const SweepWindowSize* window) {
    AppState state;
    EffectsPanelLayout layout;
    const Pane* library_pane = NULL;
    SDL_Rect library_content = {0};
    int header_h = 0;

    memset(&state, 0, sizeof(state));
    memset(&layout, 0, sizeof(layout));

    ui_init_panes(&state);
    effects_panel_init(&state);
    seed_effects_panel_meter_slots(&state.effects_panel);
    ui_layout_panes(&state, window->w, window->h);
    expect(ui_layout_debug_validate(&state), "ui layout debug validation failed");
    effects_panel_compute_layout(&state, &layout);

    expect(rect_has_positive_size(&layout.panel_rect), "effects panel rect invalid");
    expect(rect_has_positive_size(&layout.list_rect), "effects list rect invalid");
    expect(rect_has_positive_size(&layout.detail_rect), "effects detail rect invalid");
    expect(rect_contains_rect(&layout.panel_rect, &layout.list_rect), "effects list outside panel");
    expect(rect_contains_rect(&layout.panel_rect, &layout.detail_rect), "effects detail outside panel");
    expect(!rects_overlap_strict(&layout.list_rect, &layout.detail_rect), "effects list/detail overlap");

    validate_header_layout(&layout);
    validate_eq_detail_layout(&layout.detail_rect);
    validate_meter_detail_layout(&layout.detail_rect);
    validate_transport_layout(&state);
    validate_clip_inspector_layout(&state);
    validate_timeline_layout(&state);
    validate_library_layout(&state);

    library_pane = ui_layout_get_pane(&state, 3);
    expect(library_pane != NULL, "library pane missing");
    header_h = ui_layout_pane_header_height(library_pane);
    library_content = ui_layout_pane_content_rect(library_pane);
    expect(header_h >= 0, "pane header height negative");
    expect(library_content.y == library_pane->rect.y + header_h, "content rect y does not respect pane header");
    expect(library_content.h >= 0, "content rect height negative");

    expect(zoom_step == daw_shared_font_zoom_step(), "zoom step changed unexpectedly during sweep case");
}

int main(void) {
    const SweepWindowSize windows[] = {
        {960, 600},
        {1280, 800},
        {1728, 1117}
    };
    const int zoom_steps[] = {-4, -3, -2, -1, 0, 1, 2, 3, 4, 5};

    unsetenv("DAW_FONT_ZOOM_STEP");
    setenv("DAW_USE_SHARED_THEME_FONT", "1", 1);
    setenv("DAW_USE_SHARED_FONT", "1", 1);
    setenv("DAW_FONT_PRESET", "daw_default", 1);

    if (TTF_Init() != 0) {
        fail("TTF_Init failed");
    }

    for (size_t i = 0; i < sizeof(zoom_steps) / sizeof(zoom_steps[0]); ++i) {
        set_test_font_for_zoom_step(zoom_steps[i]);
        for (size_t w = 0; w < sizeof(windows) / sizeof(windows[0]); ++w) {
            run_sweep_case(zoom_steps[i], &windows[w]);
        }
    }

    ui_font_shutdown();
    TTF_Quit();
    puts("layout_text_scaling_sweep_test: success");
    return 0;
}
