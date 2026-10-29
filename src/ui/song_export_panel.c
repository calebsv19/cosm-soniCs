#include "app_state.h"
#include "ui/song_export_panel.h"
#include "ui/ab_panel.h"
#include "ui/routing_panel.h"
#include "ui/layout.h"
#include "ui/font.h"
#include "ui/shared_theme_font_adapter.h"
#include <string.h>
#include "ui/control_chrome.h"
#include <stdio.h>
#include <inttypes.h>
// Offers explicit effect/release-tail choices in seconds at the captured project sample rate.
static const unsigned tails[] = {0, 1, 3, 5, 10, 30, 60};
// Shares bounded row geometry across matching press/release and frame rendering.
void daw_song_export_panel_layout(const AppState* s, DawSongExportLayout* l) {
    *l = (DawSongExportLayout){0};
    const Pane* p = ui_layout_get_pane(s, 2);
    if (!p)
        return;
    l->panel = ui_layout_pane_content_rect(p);
    l->editable = l->panel.w >= 360 && l->panel.h >= 180;
    if (!l->editable)
        return;
    int w = (l->panel.w - 24) / 3;
    for (int i = 0; i < 6; ++i)
        l->buttons[i] =
            (SDL_Rect){l->panel.x + 8 + i % 3 * (w + 4), l->panel.y + 35 + i / 3 * 30, w, 26};
}
// Retains the latest job and choices while taking only effects-pane modal ownership.
bool daw_song_export_panel_open(AppState* s) {
    if (!s || !s->engine)
        return false;
    daw_ab_stop(s, daw_ab_revision(s));
    s->live_meter_ui.open=false;
    s->ab_ui.open = false;
    daw_routing_panel_release(s);
    s->fx_library_ui.open = false;
    s->export_ui.open = true;
    s->export_ui.pressed = -1;
    SDL_strlcpy(
        s->export_ui.message,
        "Whole song from frame zero. Export does not insert audio or change project history.",
        sizeof(s->export_ui.message));
    return true;
}
// Applies only a fully released visible export choice or addressed cancellation.
static void activate(AppState* s, int i) {
    DawSongExportUI* u = &s->export_ui;
    if (i == 5) {
        u->open = false;
        return;
    }
    if (i == 2) {
        u->format = !u->format;
        return;
    }
    if (i == 3) {
        u->tail_choice = (u->tail_choice + 1) % 7;
        return;
    }
    if (i == 4) {
        u->normalize = !u->normalize;
        return;
    }
    if (i == 1) {
        DawSongExportInfo info;
        if (u->pressed_job[0] && daw_song_export_get(s, u->pressed_job, &info)) {
            const char* code = daw_song_export_cancel(s, info.id);
            const char* message = !strcmp(code, "publication_started")
                                      ? "Publication has started; export will finish."
                                  : !strcmp(code, "already_finished")
                                      ? "Export already finished; published files are retained."
                                  : !strcmp(code, "unknown_export")
                                      ? "Export record is no longer available."
                                      : "Cancellation requested; waiting for the worker to stop.";
            SDL_strlcpy(u->message, message, sizeof(u->message));
        } else
            SDL_strlcpy(u->message, "No export to cancel.", sizeof(u->message));
        return;
    }
    DawMixSnapshot* guard = NULL;
    DawSongExportInfo info;
    DawActionStatus status = daw_action_get_mix_snapshot(s, &guard);
    if (status == DAW_ACTION_UNCHANGED)
        status = daw_song_export_start(s, guard, u->format,
                                       (uint64_t)tails[u->tail_choice] *
                                           engine_get_config(s->engine)->sample_rate,
                                       u->normalize, &info);
    daw_mix_snapshot_free(guard);
    const char* names[] = {"Export queued; monitor progress or Cancel.",
                           "Unchanged",
                           "Invalid song, range or output root.",
                           "Wrong control owner.",
                           "Project changed; retry capture.",
                           "Stop playback/recording; finish the gesture or current export.",
                           "Export storage/worker capacity unavailable.",
                           "Could not prepare the export graph."};
    SDL_strlcpy(u->message, names[status], sizeof(u->message));
}
// Cancels incomplete gestures on focus/resize and closes without cancelling a running export.
bool daw_song_export_panel_event(AppState* s, const SDL_Event* e) {
    if (!s || !e || !s->export_ui.open)
        return false;
    DawSongExportUI* u = &s->export_ui;
    DawSongExportLayout l;
    daw_song_export_panel_layout(s, &l);
    if (e->type == SDL_WINDOWEVENT) {
        u->pressed = -1;
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
            e->window.event == SDL_WINDOWEVENT_HIDDEN)
            u->open = false;
        return false;
    }
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE) {
        u->open = false;
        u->pressed = -1;
        return true;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        u->pressed = -1;
        SDL_Point p = {e->button.x, e->button.y};
        if (l.editable)
            for (int i = 0; i < 6; ++i)
                if (SDL_PointInRect(&p, &l.buttons[i]))
                    u->pressed = i;
        u->pressed_job[0] = 0;
        if (u->pressed == 1) {
            DawSongExportInfo job;
            if (daw_song_export_get(s, NULL, &job))
                SDL_strlcpy(u->pressed_job, job.id, sizeof(u->pressed_job));
        }
        return true;
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        int i = u->pressed;
        u->pressed = -1;
        SDL_Point p = {e->button.x, e->button.y};
        if (l.editable && i >= 0 && SDL_PointInRect(&p, &l.buttons[i]))
            activate(s, i);
        return true;
    }
    return true;
}
// Displays provisional work separately from verified publication and durability status.
void daw_song_export_panel_render(SDL_Renderer* r, AppState* s) {
    if (!s->export_ui.open)
        return;
    DawSongExportUI* u = &s->export_ui;
    DawSongExportLayout l;
    daw_song_export_panel_layout(s, &l);
    DawThemePalette t;
    daw_shared_theme_resolve_palette(&t);
    SDL_SetRenderDrawColor(r, t.inspector_fill.r, t.inspector_fill.g, t.inspector_fill.b, 255);
    SDL_RenderFillRect(r, &l.panel);
    ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + 8, "Asynchronous whole-song export",
                         t.text_primary, .85, l.panel.w - 16);
    if (!l.editable) {
        ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + 38,
                             "Enlarge pane to 360 x 180 for export controls.", t.text_muted, .75,
                             l.panel.w - 16);
        return;
    }
    snprintf(u->labels[0], 80, "Export song");
    snprintf(u->labels[1], 80, "Cancel job");
    snprintf(u->labels[2], 80, "%s WAV", u->format ? "Float32" : "PCM16");
    snprintf(u->labels[3], 80, "Tail: %u s", tails[u->tail_choice]);
    snprintf(u->labels[4], 80, "Normalize: %s", u->normalize ? "if clipping" : "off");
    snprintf(u->labels[5], 80, "Close");
    for (int i = 0; i < 6; ++i) {
        daw_control_frame(r, &l.buttons[i], t.control_fill, t.control_border);
        ui_draw_text_clipped(r, l.buttons[i].x + 4, l.buttons[i].y + 5, u->labels[i],
                             t.text_primary, .75, l.buttons[i].w - 8);
    }
    DawSongExportInfo info;
    if (daw_song_export_get(s, NULL, &info)) {
        snprintf(u->summary, sizeof(u->summary),
                 "%s | %.1f%% | %" PRIu64 "/%" PRIu64 " work frames | %s", info.state,
                 info.total ? 100.0 * info.done / info.total : 0, info.done, info.total,
                 info.verified ? "verified output" : "provisional");
        SDL_strlcpy(u->path, info.path, sizeof(u->path));
    } else {
        strcpy(u->summary, "Idle - no export captured");
        u->path[0] = 0;
    }
    ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + 104, u->summary, t.text_primary, .75,
                         l.panel.w - 16);
    ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + 129, u->path, t.text_muted, .7,
                         l.panel.w - 16);
    ui_draw_text_clipped(r, l.panel.x + 8, l.panel.y + l.panel.h - 25, u->message, t.text_muted, .7,
                         l.panel.w - 16);
}
