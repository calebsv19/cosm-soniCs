#include "app_state.h"
#include "ui/live_meter_panel.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "ui/control_chrome.h"
#include "ui/shared_theme_font_adapter.h"
#include "ui/routing_panel.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
// Computes same bounded rectangles for both drawing and exact pointer admission.
void daw_live_meter_panel_layout(const AppState* s, DawLiveMeterLayout* l) {
    *l = (DawLiveMeterLayout){0};
    const Pane* p = ui_layout_get_pane(s, 2);
    if (!p)
        return;
    l->panel = ui_layout_pane_content_rect(p);
    l->editable = l->panel.w >= 360 && l->panel.h >= 260;
    if (!l->editable)
        return;
    int w = (l->panel.w - 28) / 4;
    for (int i = 0; i < 4; ++i)
        l->buttons[i] = (SDL_Rect){l->panel.x + 8 + i * (w + 4), l->panel.y + 31, w, 25};
}
// Uses current native track selection solely as a read scope, including group/return rows.
static DawActionStatus current(AppState* s, DawLiveMeterInfo* info) {
    DawActionTarget target, *p = NULL;
    if (s->live_meter_ui.selected) {
        int t = s->selected_track_index;
        if (t < 0 || t >= engine_get_track_count(s->engine))
            return DAW_ACTION_INVALID;
        target = (DawActionTarget){.project_generation = engine_instance_id(s->engine),
                                   .track_id = engine_get_tracks(s->engine)[t].runtime_id,
                                   .kind = DAW_ACTION_TARGET_TRACK};
        p = &target;
    }
    return daw_live_meters_get(s, p, info);
}
// Takes effects-pane presentation ownership while leaving transport/history and monitoring intact.
bool daw_live_meter_panel_open(AppState* s) {
    if (!s || !s->engine)
        return false;
    daw_ab_stop(s, daw_ab_revision(s));
    s->ab_ui.open = s->export_ui.open = s->fx_library_ui.open = false;
    daw_routing_panel_release(s);
    s->live_meter_ui.open = true;
    s->live_meter_ui.pressed = -1;
    SDL_strlcpy(s->live_meter_ui.message,
                "400 ms production windows. Global mono monitoring never changes the mix.",
                sizeof(s->live_meter_ui.message));
    return true;
}
// Keeps the listening-only mono indication visible after the meter panel closes.
const char* daw_live_meter_toolbar_label(AppState* s) {
    DawLiveMeterInfo info;
    return daw_live_meters_get(s, NULL, &info) == DAW_ACTION_UNCHANGED && info.mono ? "Mono Monitor"
                                                                                    : "Meters";
}
// Adopts a released monitor intent only if its pressed endpoint/revision still matches.
static void activate(AppState* s, int index) {
    DawLiveMeterUI* u = &s->live_meter_ui;
    if (index == 3) {
        u->open = false;
        return;
    }
    if (index == 0) {
        u->selected = !u->selected;
        return;
    }
    DawActionStatus status = daw_monitor_set(s, u->guard, index == 2);
    const char* messages[] = {"Listening mode changed; authored music is unchanged.",
                              "Listening mode is already selected.",
                              "Invalid monitor selection.",
                              "Wrong control owner.",
                              "Monitor changed elsewhere; press again.",
                              "Monitor is busy.",
                              "Monitor revision unavailable.",
                              "Open the stereo output endpoint before monitoring."};
    SDL_strlcpy(u->message, messages[status], sizeof(u->message));
}
// Cancels partial gestures on focus/resize and leaves an explicitly selected listening mode
// visible.
bool daw_live_meter_panel_event(AppState* s, const SDL_Event* e) {
    if (!s || !e || !s->live_meter_ui.open)
        return false;
    DawLiveMeterUI* u = &s->live_meter_ui;
    DawLiveMeterLayout l;
    daw_live_meter_panel_layout(s, &l);
    if (e->type == SDL_WINDOWEVENT) {
        u->pressed = -1;
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
            e->window.event == SDL_WINDOWEVENT_HIDDEN)
            u->open = false;
        return false;
    }
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE) {
        u->pressed = -1;
        u->open = false;
        return true;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        u->pressed = -1;
        SDL_Point p = {e->button.x, e->button.y};
        if (!SDL_PointInRect(&p, &l.panel))
            return false;
        if (l.editable)
            for (int i = 0; i < 4; ++i)
                if (SDL_PointInRect(&p, &l.buttons[i]))
                    u->pressed = i;
        DawLiveMeterInfo info;
        u->guard[0] = 0;
        if (daw_live_meters_get(s, NULL, &info) == DAW_ACTION_UNCHANGED)
            SDL_strlcpy(u->guard, info.expected_monitor, sizeof(u->guard));
        return true;
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        int i = u->pressed;
        u->pressed = -1;
        SDL_Point p = {e->button.x, e->button.y};
        if (i < 0 && !SDL_PointInRect(&p, &l.panel))
            return false;
        if (l.editable && i >= 0 && SDL_PointInRect(&p, &l.buttons[i]))
            activate(s, i);
        return true;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) {
        SDL_Point p = {e->button.x, e->button.y};
        return SDL_PointInRect(&p, &l.panel);
    }
    if (e->type == SDL_MOUSEMOTION) {
        SDL_Point p = {e->motion.x, e->motion.y};
        return u->pressed >= 0 || SDL_PointInRect(&p, &l.panel);
    }
    if (e->type == SDL_MOUSEWHEEL) {
        SDL_Point p = {s->mouse_x, s->mouse_y};
        return SDL_PointInRect(&p, &l.panel);
    }
    return false;
}
// Renders silence/cancellation as distinct states rather than artificial dB floors or correlation.
void daw_live_meter_panel_render(SDL_Renderer* r, AppState* s) {
    DawLiveMeterUI* u = &s->live_meter_ui;
    if (!u->open)
        return;
    DawLiveMeterLayout l;
    daw_live_meter_panel_layout(s, &l);
    DawThemePalette t;
    daw_shared_theme_resolve_palette(&t);
    SDL_SetRenderDrawColor(r, t.inspector_fill.r, t.inspector_fill.g, t.inspector_fill.b, 255);
    SDL_RenderFillRect(r, &l.panel);
    ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + 7,
                         "Live channel / stereo / mono compatibility", t.text_primary, .85,
                         l.panel.w - 16);
    if (!l.editable) {
        ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + 38,
                             "Enlarge pane to 360 x 260 for live controls.", t.text_muted, .75,
                             l.panel.w - 16);
        return;
    }
    const char* labels[] = {u->selected ? "Selected track" : "Master", "Stereo out", "Mono out",
                            "Close"};
    for (int i = 0; i < 4; ++i) {
        SDL_strlcpy(u->labels[i], labels[i], 48);
        daw_control_frame(r, &l.buttons[i], t.control_fill, t.control_border);
        ui_draw_text_clipped(r, l.buttons[i].x + 3, l.buttons[i].y + 5, u->labels[i],
                             t.text_primary, .75, l.buttons[i].w - 6);
    }
    DawLiveMeterInfo info = {0};
    DawActionStatus status = current(s, &info);
    EngineLiveMeter* m = &info.meter;
    snprintf(u->lines[0], 256, "%s | %s | monitor %s",
             status == DAW_ACTION_UNCHANGED
                 ? (m->available
                        ? (m->valid ? (info.fresh ? "Fresh" : "Stale / frozen") : "Invalid samples")
                        : "Warming up / no live window")
                 : "Select a current track",
             info.playing ? "Playing" : "Transport stopped", info.mono ? "Mono" : "Stereo");
    for (int ch = 0; ch < 2; ++ch) {
        if (m->available && m->valid && ch < m->channels) {
            if (m->rms[ch] > 0)
                snprintf(u->lines[1 + ch], 256,
                         "%c: peak %.2f dBFS | RMS %.2f dBFS | over-range %llu",
                         'L' + (ch ? 'R' - 'L' : 0), m->peak[ch] > 0 ? 20 * log10(m->peak[ch]) : 0,
                         20 * log10(m->rms[ch]), (unsigned long long)m->overload[ch]);
            else
                snprintf(u->lines[1 + ch], 256, "%c: silence", ch ? 'R' : 'L');
        } else
            snprintf(u->lines[1 + ch], 256, "%c: no qualified measurement", ch ? 'R' : 'L');
    }
    if (m->correlation_available)
        snprintf(u->lines[3], 256, "Correlation: %+.3f | balance %+.3f (L -1 / R +1)",
                 m->correlation, m->balance);
    else
        snprintf(u->lines[3], 256, "Correlation unavailable for silence or a silent channel.");
    if (m->mono_available) {
        if (m->mono_ratio > 0)
            snprintf(u->lines[4], 256, "Mono fold loss: %.2f dB | M=(L+R)/2",
                     20 * log10(m->mono_ratio));
        else
            snprintf(u->lines[4], 256, "Mono fold: complete cancellation");
    } else
        snprintf(u->lines[4], 256, "Mono compatibility unavailable in silence.");
    if (m->available && m->valid)
        snprintf(u->lines[5], 256, "Mid RMS %.5f | Side RMS %.5f | 400 ms window", m->mid_rms,
                 m->side_rms);
    else
        snprintf(u->lines[5], 256, "Mid / Side: no qualified measurement");
    snprintf(u->lines[6], 256, "Production only; preview excluded. Sample peak / unweighted RMS.");
    for (int i = 0; i < 7; ++i)
        ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + 66 + i * 23, u->lines[i],
                             i ? t.text_muted : t.text_primary, .75, l.panel.w - 16);
    ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + l.panel.h - 23, u->message, t.text_muted, .7,
                         l.panel.w - 16);
}
