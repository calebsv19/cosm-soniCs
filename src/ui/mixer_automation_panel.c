#include "ui/mixer_automation_panel.h"
#include "app_state.h"
#include "ui/effects_panel.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/control_chrome.h"
#include "ui/text_edit.h"
#include "daw/routing_actions.h"
#include "daw/midi_actions.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Resolves only group faders or explicitly bound non-return send slots.
static EngineMixerAutomation* lane(DawRoutingUI* ui, EngineRoutingSnapshot* snapshot) {
    if (ui->effect_mode) {
        float lo, hi;
        return ui->auto_lane >= 0 && ui->auto_lane < ui->effect_capture.chain.count &&
                       fx_parameter_automation_range(
                           ui->effect_capture.chain.items[ui->auto_lane].type, 0, &lo, &hi)
                   ? &ui->effect_capture.chain.items[ui->auto_lane].automation
                   : NULL;
    }
    if (ui->selected < 0 || ui->selected >= snapshot->track_count)
        return NULL;
    EngineRoutingTrack* row = &snapshot->tracks[ui->selected];
    if (ui->auto_lane == 0)
        return row->is_group ? &row->volume_automation : NULL;
    int s = ui->auto_lane - 1;
    return s >= 0 && s < ENGINE_SEND_CAP && row->sends[s].return_id && !row->is_return
               ? &row->sends[s].automation
               : NULL;
}
// Refreshes editable point text without implicitly adopting externally changed project state.
static void point_text(DawRoutingUI* ui) {
    EngineMixerAutomation* a = lane(ui, &ui->captured);
    ui->focused = -1;
    kit_ui_text_cancel_composition(&ui->text_edit);
    if (!a || !a->count) {
        ui->auto_point = -1;
        strcpy(ui->auto_frame, "0");
        snprintf(ui->auto_value, sizeof(ui->auto_value), "%.9g",
                 ui->effect_mode && ui->auto_lane >= 0 &&
                         ui->auto_lane < ui->effect_capture.chain.count
                     ? (double)ui->effect_capture.chain.items[ui->auto_lane].params[0]
                     : 1.0);
        return;
    }
    if (ui->auto_point < 0 || ui->auto_point >= a->count)
        ui->auto_point = 0;
    snprintf(ui->auto_frame, sizeof(ui->auto_frame), "%" PRIu64, a->points[ui->auto_point].frame);
    snprintf(ui->auto_value, sizeof(ui->auto_value), "%.9g",
             (double)a->points[ui->auto_point].value);
}
// Opens an independently staged point editor within the current exact routing snapshot.
void daw_mixer_automation_open(AppState* state) {
    DawRoutingUI* ui = &state->routing_ui;
    ui->automation_mode = true;
    ui->effect_mode = false;
    ui->auto_lane = 0;
    ui->auto_point = -1;
    for (int i = 0; i <= ENGINE_SEND_CAP; ++i) {
        ui->auto_lane = i;
        if (lane(ui, &ui->captured))
            break;
    }
    point_text(ui);
    ui->pressed = -1;
    SDL_strlcpy(ui->message,
                "Project frames; absolute linear gain. Choose lane, enter frame/value, Add or Set.",
                sizeof(ui->message));
    daw_text_resume_input(state);
}
// Opens only qualified inserts while retaining exact chain and project identity.
bool daw_effect_automation_open(AppState* state) {
    EffectsPanelState* p = &state->effects_panel;
    DawRoutingUI* ui = &state->routing_ui;
    int t = p->target == FX_PANEL_TARGET_TRACK ? p->target_track_index : -1;
    if (t < -1 || t >= engine_get_track_count(state->engine))
        return false;
    uint64_t id = t < 0 ? 0 : engine_get_tracks(state->engine)[t].runtime_id;
    DawFxSnapshot snapshot;
    if (daw_action_get_fx(state, engine_instance_id(state->engine), id, &snapshot) !=
        DAW_ACTION_UNCHANGED)
        return false;
    int chosen = -1;
    float lo, hi;
    for (int i = 0; i < snapshot.chain.count; ++i)
        if (fx_parameter_automation_range(snapshot.chain.items[i].type, 0, &lo, &hi)) {
            if (chosen < 0 || i == p->selected_slot_index)
                chosen = i;
        }
    if (chosen < 0)
        return false;
    daw_routing_panel_release(state);
    ui->effect_capture = snapshot;
    ui->effect_mode = true;
    ui->open = ui->automation_mode = true;
    ui->auto_lane = chosen;
    ui->auto_point = -1;
    ui->pressed = -1;
    point_text(ui);
    SDL_strlcpy(
        ui->message,
        "Exact project frames; Gain dB or Pan -1..1. Add/Set applies one shared FX undo step.",
        sizeof(ui->message));
    daw_text_resume_input(state);
    return true;
}
// Returns the active lane's signed physical range without accepting unsupported processors.
static void bounds(const DawRoutingUI* ui, float* lo, float* hi) {
    *lo = 0;
    *hi = ui->auto_lane ? 1 : 10;
    if (ui->effect_mode) {
        *lo = 0;
        *hi = 1;
        if (ui->auto_lane >= 0 && ui->auto_lane < ui->effect_capture.chain.count)
            fx_parameter_automation_range(ui->effect_capture.chain.items[ui->auto_lane].type, 0, lo,
                                          hi);
    }
}
// Consumes only matching shared effect-chain history and reacquires its exact result.
static DawActionStatus effect_history(AppState* s, DawRoutingUI* ui, bool redo) {
    if (daw_midi_actions_busy(s))
        return DAW_ACTION_BUSY;
    int count = redo ? s->undo.redo_count : s->undo.undo_count;
    const UndoCommand* stack = redo ? s->undo.redo_stack : s->undo.undo_stack;
    if (!count || stack[count - 1].type != UNDO_CMD_FX_CHAIN)
        return DAW_ACTION_HISTORY_UNAVAILABLE;
    const DawFxSnapshot* expected =
        redo ? &stack[count - 1].data.fx_chain->before : &stack[count - 1].data.fx_chain->after;
    if (!daw_fx_snapshot_equal(expected, &ui->effect_capture))
        return DAW_ACTION_STALE_TARGET;
    if (!(redo ? undo_manager_redo(&s->undo, s) : undo_manager_undo(&s->undo, s)))
        return DAW_ACTION_ENGINE_REJECTED;
    DawActionStatus status = daw_action_get_fx(s, ui->effect_capture.generation,
                                               ui->effect_capture.track_id, &ui->effect_capture);
    return status == DAW_ACTION_UNCHANGED ? DAW_ACTION_APPLIED : status;
}
// Fits all point controls and preview before enabling edits in a dense pane.
void daw_mixer_automation_layout(const AppState* state, DawMixerAutomationLayout* l) {
    *l = (DawMixerAutomationLayout){0};
    EffectsPanelLayout base;
    effects_panel_compute_layout(state, &base);
    const Pane* pane = ui_layout_get_pane(state, 2);
    l->panel = pane ? ui_layout_pane_content_rect(pane) : base.panel_rect;
    l->editable = l->panel.w >= 360 && l->panel.h >= 275;
    if (!l->editable)
        return;
    int x = l->panel.x + 8, y = l->panel.y + 30, w = l->panel.w - 16, b = (w - 12) / 4;
    for (int i = 0; i < 14; ++i)
        l->buttons[i] = (SDL_Rect){x + (i % 4) * (b + 4), y + (i / 4) * 27, b, 24};
    l->frame = (SDL_Rect){x, y + 112, (w - 4) / 2, 26};
    l->value = (SDL_Rect){x + (w - 4) / 2 + 4, y + 112, (w - 4) / 2, 26};
    l->preview = (SDL_Rect){x, y + 145, w, l->panel.h - 222};
    l->status = (SDL_Rect){x, l->panel.y + l->panel.h - 27, w, 22};
}
// Maps matching pointer releases only to fully visible editor buttons.
static int hit(const DawMixerAutomationLayout* l, int x, int y) {
    if (!l->editable)
        return -1;
    SDL_Point p = {x, y};
    for (int i = 0; i < 14; ++i)
        if (SDL_PointInRect(&p, &l->buttons[i]))
            return i;
    return -1;
}
// Reports refusal without changing staged fields or silently replacing the captured guard.
static bool accepted(DawRoutingUI* ui, DawActionStatus status) {
    const char* messages[] = {
        "Curve applied as one undo step.",
        "No authored change.",
        "Invalid curve or destination; check ranges, distinct frames and limits.",
        "Wrong owner.",
        "State changed: Refresh before editing.",
        "Stop transport/recording and finish the active gesture.",
        "Matching shared history unavailable.",
        "Engine rejected the complete candidate."};
    SDL_strlcpy(ui->message, messages[status], sizeof(ui->message));
    return status <= DAW_ACTION_UNCHANGED;
}
// Parses exact canonical frame text and finite physical gain without clamping.
static bool inputs(DawRoutingUI* ui, uint64_t* frame, float* value) {
    char* text = ui->auto_frame;
    if (!*text || (text[0] == '0' && text[1]))
        return false;
    for (char* p = text; *p; ++p)
        if (*p < '0' || *p > '9')
            return false;
    errno = 0;
    char* end;
    unsigned long long n = strtoull(text, &end, 10);
    if (errno || *end)
        return false;
    errno = 0;
    double v = strtod(ui->auto_value, &end);
    float lo, hi;
    bounds(ui, &lo, &hi);
    if (end == ui->auto_value || errno || *end || !isfinite(v) || v < lo || v > hi)
        return false;
    *frame = n;
    *value = v;
    return true;
}
// Publishes discrete point/shape edits only after reserved shared routing history is available.
static void activate(AppState* state, int control) {
    DawRoutingUI* ui = &state->routing_ui;
    if (control == 13) {
        if (ui->effect_mode) {
            daw_routing_panel_release(state);
            return;
        }
        ui->automation_mode = false;
        ui->focused = -1;
        kit_ui_text_cancel_composition(&ui->text_edit);
        daw_text_resume_input(state);
        return;
    }
    if (control == 0) {
        int lanes = ui->effect_mode ? ui->effect_capture.chain.count : ENGINE_SEND_CAP + 1;
        for (int i = 0; i < lanes; ++i) {
            ui->auto_lane = (ui->auto_lane + 1) % lanes;
            if (lane(ui, &ui->captured))
                break;
        }
        ui->auto_point = -1;
        point_text(ui);
        daw_text_resume_input(state);
        return;
    }
    if (control == 1) {
        if ((ui->effect_mode
                 ? daw_action_get_fx(state, ui->effect_capture.generation,
                                     ui->effect_capture.track_id, &ui->effect_capture)
                 : daw_action_get_routing(state, &ui->captured)) == DAW_ACTION_UNCHANGED) {
            point_text(ui);
            SDL_strlcpy(ui->message, "Refreshed exact routing guard.", sizeof(ui->message));
        }
        return;
    }
    EngineMixerAutomation* current = lane(ui, &ui->captured);
    if (!current)
        return;
    if (control == 2 || control == 3) {
        if (current->count) {
            ui->auto_point =
                (ui->auto_point + (control == 3 ? 1 : current->count - 1) + current->count) %
                current->count;
            point_text(ui);
        }
        return;
    }
    if (ui->effect_mode && (control == 11 || control == 12)) {
        if (accepted(ui, effect_history(state, ui, control == 12)))
            point_text(ui);
        return;
    }
    EngineRoutingSnapshot* out = calloc(1, sizeof(*out));
    if (!out)
        return;
    if (control == 11 || control == 12) {
        DawActionStatus status =
            daw_action_routing_history(state, &ui->captured, control == 12, out);
        if (accepted(ui, status)) {
            ui->captured = *out;
            point_text(ui);
        }
        free(out);
        return;
    }
    EngineRoutingSnapshot* next = malloc(sizeof(*next));
    if (!next) {
        free(out);
        return;
    }
    *next = ui->captured;
    EngineMixerAutomation candidate = *current;
    EngineMixerAutomation* a = &candidate;
    bool ok = true;
    if (control == 7) {
        *a = (EngineMixerAutomation){0};
        ui->auto_point = -1;
    } else if (control >= 8 && control <= 10) {
        if (!a->count)
            ok = false;
        else if (control == 8) {
            if (ui->auto_point < 0 || ui->auto_point >= a->count)
                ok = false;
            else
                a->points[ui->auto_point].curve =
                    (a->points[ui->auto_point].curve + 1) % ENGINE_AUTOMATION_CURVE_COUNT;
        } else if (control == 9)
            a->before = (a->before + 1) % ENGINE_AUTOMATION_BOUNDARY_COUNT;
        else
            a->after = (a->after + 1) % ENGINE_AUTOMATION_BOUNDARY_COUNT;
    } else if (control >= 4 && control <= 6) {
        uint64_t frame;
        float value;
        if (control != 6 && !inputs(ui, &frame, &value))
            ok = false;
        if (ok && (control == 5 || control == 6)) {
            if (ui->auto_point < 0 || ui->auto_point >= a->count)
                ok = false;
            else {
                EngineAutomationCurve curve = a->points[ui->auto_point].curve;
                for (int i = ui->auto_point; i < a->count - 1; ++i)
                    a->points[i] = a->points[i + 1];
                --a->count;
                if (control == 5) {
                    if (a->count >= ENGINE_MIX_AUTOMATION_POINTS)
                        ok = false;
                    else {
                        int i = 0;
                        while (i < a->count && a->points[i].frame < frame)
                            ++i;
                        if (i < a->count && a->points[i].frame == frame)
                            ok = false;
                        else {
                            for (int j = a->count; j > i; --j)
                                a->points[j] = a->points[j - 1];
                            a->points[i] = (EngineAutomationPoint){frame, value, curve};
                            ++a->count;
                            ui->auto_point = i;
                        }
                    }
                }
            }
        }
        if (ok && control == 4) {
            if (a->count >= ENGINE_MIX_AUTOMATION_POINTS)
                ok = false;
            else {
                int i = 0;
                while (i < a->count && a->points[i].frame < frame)
                    ++i;
                if (i < a->count && a->points[i].frame == frame)
                    ok = false;
                else {
                    for (int j = a->count; j > i; --j)
                        a->points[j] = a->points[j - 1];
                    a->points[i] =
                        (EngineAutomationPoint){frame, value, ENGINE_AUTOMATION_CURVE_LINEAR};
                    ++a->count;
                    ui->auto_point = i;
                }
            }
        }
        if (!a->count)
            *a = (EngineMixerAutomation){0};
    } else
        ok = false;
    if (ok) {
        DawActionStatus status;
        DawFxSnapshot effect_out;
        if (ui->effect_mode) {
            EngineFxChainRequest request = {.op = ENGINE_FX_CHAIN_AUTOMATION,
                                            .id = ui->effect_capture.chain.items[ui->auto_lane].id,
                                            .automation = candidate};
            status = daw_action_edit_fx(state, &ui->effect_capture, &request, &effect_out);
        } else {
            *lane(ui, next) = candidate;
            status = daw_action_set_routing(state, &ui->captured, next, out);
        }
        if (accepted(ui, status)) {
            if (ui->effect_mode)
                ui->effect_capture = effect_out;
            else
                ui->captured = *out;
            point_text(ui);
        }
    } else
        SDL_strlcpy(
            ui->message,
            ui->effect_mode
                ? "Invalid point: distinct frames; Gain -96..24 dB, Pan -1..1, maximum 32 points."
                : "Invalid point: frames distinct; group gain 0..10, send 0..1, maximum 32 points.",
            sizeof(ui->message));
    free(next);
    free(out);
}
// Gives curve fields and guarded matching-release edits priority over the underlying timeline.
bool daw_mixer_automation_event(AppState* state, const SDL_Event* event) {
    DawRoutingUI* ui = &state->routing_ui;
    DawMixerAutomationLayout l;
    daw_mixer_automation_layout(state, &l);
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE) {
        if (ui->effect_mode) {
            daw_routing_panel_release(state);
            return true;
        }
        ui->automation_mode = false;
        ui->focused = -1;
        kit_ui_text_cancel_composition(&ui->text_edit);
        daw_text_resume_input(state);
        return true;
    }
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        ui->pressed = hit(&l, event->button.x, event->button.y);
        SDL_Point p = {event->button.x, event->button.y};
        if (l.editable && (SDL_PointInRect(&p, &l.frame) || SDL_PointInRect(&p, &l.value))) {
            ui->focused = SDL_PointInRect(&p, &l.frame) ? 0 : 1;
            char* text = ui->focused ? ui->auto_value : ui->auto_frame;
            ui->cursor = strlen(text);
            daw_text_edit_begin(&ui->text_edit, text, 32, &ui->cursor, KIT_UI_TEXT_SINGLE_LINE);
            SDL_StartTextInput();
        }
        return true;
    }
    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        int p = ui->pressed;
        ui->pressed = -1;
        if (p >= 0 && p == hit(&l, event->button.x, event->button.y))
            activate(state, p);
        return true;
    }
    if (ui->focused >= 0) {
        char* text = ui->focused ? ui->auto_value : ui->auto_frame;
        KitUiTextEventResult result = daw_text_edit_event(&ui->text_edit, text, 32, &ui->cursor,
                                                          KIT_UI_TEXT_SINGLE_LINE, event);
        if (result.changed)
            ui->pressed = -1;
        if (result.submit && l.editable)
            activate(state, ui->auto_point < 0 ? 4 : 5);
    }
    return true;
}
// Draws fixed per-control text storage that remains valid until queued rendering completes.
void daw_mixer_automation_render(SDL_Renderer* renderer, const AppState* state) {
    DawRoutingUI* ui = (DawRoutingUI*)&state->routing_ui;
    DawMixerAutomationLayout l;
    daw_mixer_automation_layout(state, &l);
    DawThemePalette theme;
    daw_shared_theme_resolve_palette(&theme);
    SDL_SetRenderDrawColor(renderer, theme.inspector_fill.r, theme.inspector_fill.g,
                           theme.inspector_fill.b, 255);
    SDL_RenderFillRect(renderer, &l.panel);
    EngineMixerAutomation* a = lane(ui, &ui->captured);
    snprintf(ui->lane_label, sizeof(ui->lane_label), "Mix automation: %s / %s",
             ui->effect_mode    ? "FX"
             : ui->selected < 0 ? "No track"
                                : ui->names[ui->selected],
             ui->auto_lane ? "Send" : "Group volume");
    if (ui->effect_mode)
        snprintf(ui->lane_label, sizeof(ui->lane_label), "Effect automation: %s / %s",
                 ui->effect_capture.track_id ? "Track" : "Master",
                 a && ui->effect_capture.chain.items[ui->auto_lane].type == 1 ? "Gain dB" : "Pan");
    ui_draw_text_clipped(renderer, l.panel.x + 8, l.panel.y + 7, ui->lane_label, theme.text_primary,
                         .85f, l.panel.w - 16);
    if (!l.editable) {
        ui_draw_text_clipped(renderer, l.panel.x + 8, l.panel.y + 40,
                             "Enlarge pane to 360 x 275 for automation.", theme.text_muted, .8f,
                             l.panel.w - 16);
        return;
    }
    const char* labels[] = {"Lane",  "Refresh", "Prev point", "Next point", "Add",
                            "Set",   "Delete",  "Clear",      "Curve",      "Before",
                            "After", "Undo",    "Redo",       "Back"};
    for (int i = 0; i < 14; ++i) {
        snprintf(ui->auto_labels[i], sizeof(ui->auto_labels[i]), "%s", labels[i]);
        if (a && i == 0)
            snprintf(ui->auto_labels[i], sizeof(ui->auto_labels[i]),
                     ui->auto_lane ? "Send %d" : "Group", ui->auto_lane);
        if (a && i == 0 && ui->effect_mode)
            snprintf(ui->auto_labels[i], sizeof(ui->auto_labels[i]), "Insert %d",
                     ui->auto_lane + 1);
        if (a && i == 8 && ui->auto_point >= 0 && ui->auto_point < a->count)
            snprintf(ui->auto_labels[i], sizeof(ui->auto_labels[i]), "%s",
                     (const char*[]){"Linear", "Hold", "Smooth"}[a->points[ui->auto_point].curve]);
        if (a && (i == 9 || i == 10))
            snprintf(ui->auto_labels[i], sizeof(ui->auto_labels[i]), "%s:%s",
                     i == 9 ? "Before" : "After",
                     (i == 9 ? a->before : a->after) ? "Hold" : "Base");
        daw_control_frame(renderer, &l.buttons[i], theme.control_fill, theme.control_border);
        ui_draw_text_clipped(renderer, l.buttons[i].x + 4, l.buttons[i].y + 4, ui->auto_labels[i],
                             theme.text_primary, .75f, l.buttons[i].w - 8);
    }
    daw_control_frame(renderer, &l.frame, theme.control_fill, theme.control_border);
    daw_control_frame(renderer, &l.value, theme.control_fill, theme.control_border);
    snprintf(ui->auto_labels[14], sizeof(ui->auto_labels[14]), "Frame: %s", ui->auto_frame);
    snprintf(ui->auto_labels[15], sizeof(ui->auto_labels[15]),
             ui->effect_mode ? "Value: %s" : "Gain: %s", ui->auto_value);
    if (ui->focused == 0)
        daw_text_edit_draw(renderer, &ui->text_edit, ui->auto_frame, 32, ui->cursor, l.frame,
                           theme.text_primary, theme.control_fill, .8f);
    else
        ui_draw_text_clipped(renderer, l.frame.x + 4, l.frame.y + 4, ui->auto_labels[14],
                             theme.text_primary, .8f, l.frame.w - 8);
    if (ui->focused == 1)
        daw_text_edit_draw(renderer, &ui->text_edit, ui->auto_value, 32, ui->cursor, l.value,
                           theme.text_primary, theme.control_fill, .8f);
    else
        ui_draw_text_clipped(renderer, l.value.x + 4, l.value.y + 4, ui->auto_labels[15],
                             theme.text_primary, .8f, l.value.w - 8);
    daw_control_frame(renderer, &l.preview, theme.timeline_fill, theme.control_border);
    if (a && a->count && l.preview.h > 0) {
        uint64_t end = a->points[a->count - 1].frame;
        if (!end)
            end = 1;
        float baseline = !ui->effect_mode && ui->auto_lane
                             ? ui->captured.tracks[ui->selected].sends[ui->auto_lane - 1].gain
                             : 1;
        if (ui->effect_mode)
            baseline = ui->effect_capture.chain.items[ui->auto_lane].params[0];
        const EngineTrack* tracks = engine_get_tracks(state->engine);
        if (!ui->effect_mode && !ui->auto_lane && tracks &&
            ui->selected < engine_get_track_count(state->engine) &&
            tracks[ui->selected].runtime_id == ui->captured.tracks[ui->selected].track_id)
            baseline = tracks[ui->selected].gain;
        float maximum = fmaxf(1, baseline);
        for (int i = 0; !ui->auto_lane && i < a->count; ++i)
            if (a->points[i].value > maximum)
                maximum = a->points[i].value;
        float minimum = 0;
        if (ui->effect_mode)
            bounds(ui, &minimum, &maximum);
        SDL_SetRenderDrawColor(renderer, theme.text_primary.r, theme.text_primary.g,
                               theme.text_primary.b, 255);
        int old_y = l.preview.y + l.preview.h - 1;
        for (int x = 0; x < l.preview.w; ++x) {
            uint64_t f = (uint64_t)((long double)x * end / (l.preview.w > 1 ? l.preview.w - 1 : 1));
            float v = engine_mixer_automation_eval(a, f, baseline);
            int y = l.preview.y + l.preview.h - 1 -
                    (int)((v - minimum) / (maximum - minimum) * (l.preview.h - 1));
            if (x)
                SDL_RenderDrawLine(renderer, l.preview.x + x - 1, old_y, l.preview.x + x, y);
            old_y = y;
        }
    }
    ui_draw_text_clipped(renderer, l.status.x, l.status.y, ui->message, theme.text_muted, .7f,
                         l.status.w);
}
