#include "ui/ab_panel.h"
#include "app_state.h"
#include "ui/layout.h"
#include "ui/text_edit.h"
#include "ui/font.h"
#include "ui/control_chrome.h"
#include "ui/effects_panel.h"
#include "ui/routing_panel.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
// Uses the same pane content geometry for drawing and pointer admission.
void daw_ab_panel_layout(const AppState* s, DawAbLayout* l) {
    memset(l, 0, sizeof(*l));
    const Pane* p = ui_layout_get_pane(s, 2);
    if (!p)
        return;
    l->panel = ui_layout_pane_content_rect(p);
    l->editable = l->panel.w >= 360 && l->panel.h >= 260;
    if (!l->editable)
        return;
    int x = l->panel.x + 8, y = l->panel.y + 30, w = l->panel.w - 16, b = (w - 12) / 4;
    for (int i = 0; i < 12; ++i)
        l->buttons[i] = (SDL_Rect){x + i % 4 * (b + 4), y + i / 4 * 27, b, 24};
    for (int i = 0; i < 3; ++i)
        l->fields[i] = (SDL_Rect){x + i * (w / 3 + 1), y + 87, w / 3 - 3, 26};
}
// Starts with an explicit five-second passage at the stopped authored cursor.
bool daw_ab_panel_open(AppState* s) {
    if (!s || !s->engine)
        return false;
    s->live_meter_ui.open=false;
    s->export_ui.open=false;
    daw_routing_panel_release(s);
    s->fx_library_ui.open = false;
    DawAbUI* u = &s->ab_ui;
    u->open = true;
    u->focused = u->pressed = -1;
    u->matched = !s->ab || s->ab->selection < 2;
    u->side = s->ab ? s->ab->selection % 2 : 0;
    uint64_t rate = engine_get_config(s->engine)->sample_rate,
             start = engine_get_presentation_frame(s->engine);
    if (start > INT64_MAX - rate * 5)
        start = 0;
    if (!u->fields[0][0]) {
        snprintf(u->fields[0], 32, "%" PRIu64, start);
        snprintf(u->fields[1], 32, "%" PRIu64, start + rate * 5);
        snprintf(u->fields[2], 32, "%" PRIu64, rate);
    }
    SDL_strlcpy(u->message,
                "Capture A, change processing, Capture B. RMS matching never edits the project.",
                sizeof(u->message));
    daw_text_resume_input(s);
    return true;
}
// Reads canonical exact frames without coercing text or changing authored selection.
static bool frame(const char* text, uint64_t* out) {
    if (!*text || (text[0] == '0' && text[1]))
        return false;
    for (const char* p = text; *p; ++p)
        if (*p < '0' || *p > '9')
            return false;
    errno = 0;
    char* end;
    uint64_t v = strtoull(text, &end, 10);
    if (errno || *end)
        return false;
    *out = v;
    return true;
}
// Provides discrete outcome feedback without treating analysis/listening as music edits.
static void result(DawAbUI* u, DawActionStatus status) {
    const char* messages[] = {
        "Comparison updated; authored project and history unchanged.",
        "No listening change.",
        "Capture compatible A/B ranges; match needs two non-silent captures.",
        "Wrong control owner.",
        "State changed; capture A again or refresh the comparison.",
        "Stop transport/recording and other auditions; finish the active gesture.",
        "Comparison capacity unavailable.",
        "Output unavailable; open the configured audio endpoint before listening."};
    SDL_strlcpy(u->message, messages[status], sizeof(u->message));
}
// Keeps every side/mode switch on the same cursor and retires only the addressed audition.
static void activate(AppState* s, int index) {
    DawAbUI* u = &s->ab_ui;
    uint64_t revision = daw_ab_revision(s);
    if (index == 11) {
        daw_ab_stop(s, revision);
        u->open = false;
        u->focused = -1;
        kit_ui_text_cancel_composition(&u->text);
        daw_text_resume_input(s);
        return;
    }
    if (index == 9) {
        daw_ab_stop(s, revision);
        daw_ab_release(s);
        result(u, DAW_ACTION_APPLIED);
        return;
    }
    if (index == 10) {
        u->dry = !u->dry;
        return;
    }
    if (index == 8) {
        u->track_scope = !u->track_scope;
        return;
    }
    if (index == 7) {
        uint64_t rate = engine_get_config(s->engine)->sample_rate,
                 start = engine_get_presentation_frame(s->engine);
        if (start <= INT64_MAX - rate * 5) {
            snprintf(u->fields[0], 32, "%" PRIu64, start);
            snprintf(u->fields[1], 32, "%" PRIu64, start + rate * 5);
        }
        return;
    }
    if (index == 0 || index == 1) {
        DawCompositionRequest request = {.dry = u->dry};
        if (!frame(u->fields[0], &request.start) || !frame(u->fields[1], &request.end) ||
            !frame(u->fields[2], &request.tail)) {
            result(u, DAW_ACTION_INVALID);
            return;
        }
        if (u->track_scope) {
            int t = s->effects_panel.target_track_index;
            if (s->effects_panel.target != FX_PANEL_TARGET_TRACK || t < 0 ||
                t >= engine_get_track_count(s->engine)) {
                result(u, DAW_ACTION_INVALID);
                return;
            }
            request.has_target = true;
            request.target =
                (DawActionTarget){.project_generation = engine_instance_id(s->engine),
                                  .track_id = engine_get_tracks(s->engine)[t].runtime_id,
                                  .kind = DAW_ACTION_TARGET_TRACK};
        }
        DawMixSnapshot* guard = NULL;
        DawActionStatus status = daw_action_get_mix_snapshot(s, &guard);
        if (status == DAW_ACTION_UNCHANGED)
            status = daw_ab_capture(s, revision, guard, index, &request);
        daw_mix_snapshot_free(guard);
        result(u, status);
        return;
    }
    if (index == 4) {
        result(u, daw_ab_stop(s, revision));
        return;
    }
    if (index == 5 || index == 6) {
        bool matched = index == 5;
        uint64_t position;
        bool active = s->ab && engine_comparison_status(s->engine, s->ab->revision, &position);
        if (active) {
            DawActionStatus status = daw_ab_listen(s, revision, u->side, matched, false);
            result(u, status);
            if (status != DAW_ACTION_APPLIED)
                return;
        }
        u->matched = matched;
        return;
    }
    if (index == 2 || index == 3) {
        DawActionStatus status = daw_ab_listen(s, revision, index - 2, u->matched, false);
        result(u, status);
        if (status == DAW_ACTION_APPLIED)
            u->side = index - 2;
    }
}
// Gives comparison controls modal ownership only while they are fully visible.
bool daw_ab_panel_event(AppState* s, const SDL_Event* e) {
    DawAbUI* u = &s->ab_ui;
    if (!u->open)
        return false;
    DawAbLayout l;
    daw_ab_panel_layout(s, &l);
    if (e->type == SDL_WINDOWEVENT) {
        u->pressed = -1;
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
            e->window.event == SDL_WINDOWEVENT_HIDDEN) {
            daw_ab_stop(s, daw_ab_revision(s));
            u->open = false;
            u->focused = -1;
            kit_ui_text_cancel_composition(&u->text);
            daw_text_resume_input(s);
        }
        return false;
    }
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE) {
        activate(s, 11);
        return true;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        SDL_Point p = {e->button.x, e->button.y};
        u->pressed = -1;
        if (l.editable) {
            for (int i = 0; i < 12; ++i)
                if (SDL_PointInRect(&p, &l.buttons[i]))
                    u->pressed = i;
            for (int i = 0; i < 3; ++i)
                if (SDL_PointInRect(&p, &l.fields[i])) {
                    u->focused = i;
                    u->cursor = strlen(u->fields[i]);
                    daw_text_edit_begin(&u->text, u->fields[i], 32, &u->cursor,
                                        KIT_UI_TEXT_SINGLE_LINE);
                    SDL_StartTextInput();
                }
        }
        return true;
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        int pressed = u->pressed;
        u->pressed = -1;
        SDL_Point p = {e->button.x, e->button.y};
        if (l.editable && pressed >= 0 && SDL_PointInRect(&p, &l.buttons[pressed]))
            activate(s, pressed);
        return true;
    }
    if (u->focused >= 0 && u->focused < 3) {
        KitUiTextEventResult r = daw_text_edit_event(&u->text, u->fields[u->focused], 32,
                                                     &u->cursor, KIT_UI_TEXT_SINGLE_LINE, e);
        if (r.changed)
            u->pressed = -1;
    }
    return true;
}
// Uses persistent per-control text to describe the actual immutable comparison and playback
// request.
void daw_ab_panel_render(SDL_Renderer* r, AppState* s) {
    DawAbUI* u = &s->ab_ui;
    if (!u->open)
        return;
    DawAbLayout l;
    daw_ab_panel_layout(s, &l);
    DawThemePalette t;
    daw_shared_theme_resolve_palette(&t);
    SDL_SetRenderDrawColor(r, t.inspector_fill.r, t.inspector_fill.g, t.inspector_fill.b, 255);
    SDL_RenderFillRect(r, &l.panel);
    ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + 7,
                         "A/B listening - immutable captures, listening-only gain", t.text_primary,
                         .85, l.panel.w - 16);
    if (!l.editable) {
        ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + 36,
                             "Enlarge pane to 360 x 260 for A/B controls.", t.text_muted, .8,
                             l.panel.w - 16);
        return;
    }
    const char* labels[] = {"Capture A",
                            "Capture B",
                            "Listen A",
                            "Listen B",
                            "Stop",
                            "Matched",
                            "Original",
                            "At head",
                            u->track_scope ? "Track" : "Project",
                            "Clear",
                            u->dry ? "Dry" : "Processed",
                            "Close"};
    for (int i = 0; i < 12; ++i) {
        SDL_strlcpy(u->labels[i], labels[i], sizeof(u->labels[i]));
        daw_control_frame(r, &l.buttons[i], t.control_fill, t.control_border);
        ui_draw_text_clipped(r, l.buttons[i].x + 4, l.buttons[i].y + 4, u->labels[i],
                             t.text_primary, .75, l.buttons[i].w - 8);
    }
    const char* field_labels[] = {"Start", "End", "Tail"};
    for (int i = 0; i < 3; ++i) {
        daw_control_frame(r, &l.fields[i], t.control_fill, t.control_border);
        snprintf(u->labels[12 + i], sizeof(u->labels[12 + i]), "%s: %s", field_labels[i],
                 u->fields[i]);
        if (u->focused == i)
            daw_text_edit_draw(r, &u->text, u->fields[i], 32, u->cursor, l.fields[i],
                               t.text_primary, t.control_fill, .75);
        else
            ui_draw_text_clipped(r, l.fields[i].x + 3, l.fields[i].y + 4, u->labels[12 + i],
                                 t.text_primary, .75, l.fields[i].w - 6);
    }
    uint64_t position = 0;
    bool active = s->ab && engine_comparison_status(s->engine, s->ab->revision, &position);
    snprintf(u->summary, sizeof(u->summary), "%s / %s / %s | cursor %" PRIu64 " | match %s",
             active ? "Listening" : "Stopped", s->ab && s->ab->selection % 2 ? "B" : "A",
             (active ? s->ab->selection < 2 : u->matched) ? "Matched" : "Original", position,
             s->ab && s->ab->match_available ? "available" : "needs two non-silent captures");
    ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + 150, u->summary, t.text_primary, .75,
                         l.panel.w - 16);
    for (int k = 0; k < 2; ++k) {
        DawCompositionRender* c = s->ab ? &s->ab->captures[k] : NULL;
        if (c && c->audio.data)
            snprintf(u->meters[k], sizeof(u->meters[k]),
                     "%c %s: peak %.4f RMS %.4f | matched gain %.2f dB", 'A' + k,
                     c->request.dry ? "dry" : "processed", c->analysis.peak, c->analysis.rms,
                     s->ab->ready ? 20 * log10(s->ab->gains[k]) : 0);
        else
            snprintf(u->meters[k], sizeof(u->meters[k]), "%c: capture pending", 'A' + k);
        ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + 174 + k * 22, u->meters[k], t.text_muted,
                             .75, l.panel.w - 16);
    }
    ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + l.panel.h - 30, u->message, t.text_muted, .7,
                         l.panel.w - 16);
}
