#include "app_state.h"
#include "ui/routing_panel.h"
#include "ui/mixer_automation_panel.h"
#include "ui/effects_panel.h"
#include "ui/midi_editor.h"
#include "ui/text_edit.h"
#include "ui/daw_ui_button.h"
#include "ui/font.h"
#include "ui/control_chrome.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Releases only this modal's staged values and shared text ownership.
void daw_routing_panel_release(AppState* state) {
    if (!state) return;
    DawRoutingUI* ui = &state->routing_ui;
    ui->automation_mode=false;ui->effect_mode=false;
    ui->open = false; ui->focused = ui->pressed = -1;
    kit_ui_text_cancel_composition(&ui->text_edit); daw_text_resume_input(state);
}
// Loads the selected lane's complete staged values from the retained capture.
static void stage(AppState* state) {
    DawRoutingUI* ui = &state->routing_ui;
    ui->focused = ui->pressed = -1; kit_ui_text_cancel_composition(&ui->text_edit);
    if (ui->selected >= ui->captured.track_count) ui->selected = ui->captured.track_count - 1;
    if (ui->selected < 0 && ui->captured.track_count) ui->selected = 0;
    ui->staged_return = ui->selected >= 0 && ui->captured.tracks[ui->selected].is_return;
    for (int send = 0; send < ENGINE_SEND_CAP; ++send) {
        EngineSend value = ui->selected >= 0 ? ui->captured.tracks[ui->selected].sends[send] : (EngineSend){0};
        ui->destinations[send] = value.return_id;
        snprintf(ui->gains[send], sizeof(ui->gains[send]), "%.9g", (double)value.gain * 100);
    }
    if (ui->selected >= 0) {
        state->effects_panel.target = FX_PANEL_TARGET_TRACK;
        state->effects_panel.target_track_index = ui->selected;
        effects_panel_sync_from_engine(state);
    }
    daw_text_resume_input(state);
}
// Refreshes routing and copied names explicitly rather than adopting external edits while typing.
static bool refresh(AppState* state) {
    DawRoutingUI* ui = &state->routing_ui;
    if (daw_action_get_routing(state, &ui->captured) != DAW_ACTION_UNCHANGED) {
        SDL_strlcpy(ui->message, "Routing capture unavailable.", sizeof(ui->message)); return false;
    }
    const EngineTrack* tracks = engine_get_tracks(state->engine);
    for (int t = 0; t < ui->captured.track_count; ++t) SDL_strlcpy(ui->names[t], tracks[t].name, sizeof(ui->names[t]));
    stage(state); SDL_strlcpy(ui->message, "Choose return destinations and send percentages, then Apply.", sizeof(ui->message)); return true;
}
// Opens a copied routing precondition without changing the project or history.
bool daw_routing_panel_open(AppState* state) {
    if (!state || !state->engine || state->fx_library_ui.open || state->inspector.visible || midi_editor_should_render(state)) return false;
    DawRoutingUI* ui = &state->routing_ui;
    ui->selected = state->effects_panel.target == FX_PANEL_TARGET_TRACK ? state->effects_panel.target_track_index : 0;
    if (!refresh(state)) return false;
    state->effects_panel.overlay_layer = FX_PANEL_OVERLAY_CLOSED;
    ui->open = true; ui->focused = ui->pressed = -1; return true;
}
// Fits discrete controls to the pane and disables authoring when complete rows cannot fit.
void daw_routing_panel_layout(const AppState* state, DawRoutingLayout* out) {
    *out = (DawRoutingLayout){0}; EffectsPanelLayout base; effects_panel_compute_layout(state, &base); out->panel = base.panel_rect;
    int x = out->panel.x + 8, y = out->panel.y + 8, w = out->panel.w - 16;
    if (w < 0) w = 0;
    int button_width = (w - 20) / 6; if (button_width < 0) button_width = 0;
    for (int i = 0; i < 5; ++i) out->buttons[i] = (SDL_Rect){x + i * (button_width + 4), y + 26, button_width, 24};
    out->buttons[7]=(SDL_Rect){x+5*(button_width+4),y+26,button_width,24};
    out->editable = out->panel.w >= 360 && out->panel.h >= 275;
    if (out->editable) {
        for (int send = 0; send < ENGINE_SEND_CAP; ++send) {
            out->destinations[send] = (SDL_Rect){x, y + 66 + send * 34, w - 86, 26};
            out->gains[send] = (SDL_Rect){x + w - 80, y + 66 + send * 34, 80, 26};
        }
        out->buttons[5] = (SDL_Rect){x, y + 207, (w - 4) / 2, 25};
        out->buttons[6] = (SDL_Rect){x + (w - 4) / 2 + 4, y + 207, (w - 4) / 2, 25};
    }
    out->status = (SDL_Rect){x, y + (out->editable ? 241 : 58), w, 22};
}
// Accepts only fully visible controls using the same panel boundary as rendering.
static bool visible(const DawRoutingLayout* layout, SDL_Rect rect) {
    return rect.w > 0 && rect.h > 0 && rect.x >= layout->panel.x && rect.y >= layout->panel.y &&
        rect.x + rect.w <= layout->panel.x + layout->panel.w && rect.y + rect.h <= layout->panel.y + layout->panel.h;
}
// Resolves one control only within its complete visible hit region.
static int hit(const DawRoutingUI* ui, const DawRoutingLayout* layout, int x, int y) {
    SDL_Point point = {x,y};
    for (int i = 0; i < 8; ++i) if (visible(layout, layout->buttons[i]) && SDL_PointInRect(&point, &layout->buttons[i])) {
        if (i != 4 && !layout->editable) return -1;
        if ((i == 0 || i == 1 || i >= 5) && ui->selected < 0) return -1;
        return i;
    }
    if (!layout->editable || ui->selected < 0 || ui->staged_return) return -1;
    for (int send = 0; send < ENGINE_SEND_CAP; ++send)
        if (SDL_PointInRect(&point, &layout->destinations[send])) return 8 + send;
    return -1;
}
// Starts shared native input on one explicitly selected send percentage.
static void focus(AppState* state, int send) {
    DawRoutingUI* ui = &state->routing_ui; daw_text_cancel_composition(state);
    ui->focused = send; ui->pressed = -1; ui->cursor = strlen(ui->gains[send]);
    daw_text_edit_begin(&ui->text_edit, ui->gains[send], sizeof(ui->gains[send]), &ui->cursor, KIT_UI_TEXT_SINGLE_LINE); SDL_StartTextInput();
}
// Reports typed failures while retaining staged user input until an explicit Refresh.
static bool accepted(DawRoutingUI* ui, DawActionStatus status) {
    static const char* const messages[] = {"Routing applied as one undo step.","Routing already matches.","Invalid routing: check empty return roles and unique destinations.","Wrong owner.","Routing changed; Refresh before applying.","Stop playback/recording and finish the active edit.","History unavailable.","Engine rejected routing."};
    SDL_strlcpy(ui->message, messages[status], sizeof(ui->message)); return status <= DAW_ACTION_UNCHANGED;
}
// Parses complete physical percentages without silently clamping or accepting trailing junk.
static bool gain(const char* text, float* out) {
    errno = 0; char* end; double value = strtod(text, &end);
    if (end == text || errno || !isfinite(value) || value < 0 || value > 100) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end) return false;
    *out = (float)(value / 100); return true;
}
// Applies staged edits, creation or destination selection through the common guarded owners.
static void activate(AppState* state, int control) {
    DawRoutingUI* ui = &state->routing_ui;
    if(control==7){daw_mixer_automation_open(state);return;}
    if (control == 4) { daw_routing_panel_release(state); return; }
    if (control == 3) { refresh(state); return; }
    if (control == 0 || control == 1) {
        if (ui->captured.track_count) ui->selected = (ui->selected + (control ? 1 : ui->captured.track_count - 1)) % ui->captured.track_count;
        stage(state); return;
    }
    EngineRoutingSnapshot output;
    if (control == 2) {
        DawActionStatus status = daw_action_create_return(state, &ui->captured, &output);
        if (accepted(ui, status)) { ui->selected = output.track_count - 1; refresh(state); accepted(ui, status); }
    } else if (control == 5) {
        ui->staged_return = !ui->staged_return;
        if (ui->staged_return) for (int send = 0; send < ENGINE_SEND_CAP; ++send) { ui->destinations[send] = 0; strcpy(ui->gains[send], "0"); }
        ui->focused = -1; kit_ui_text_cancel_composition(&ui->text_edit); daw_text_resume_input(state);
        SDL_strlcpy(ui->message, "Lane role staged; Apply validates the complete graph.", sizeof(ui->message));
    } else if (control == 6) {
        EngineRoutingSnapshot next = ui->captured; EngineRoutingTrack* row = &next.tracks[ui->selected]; row->is_return = ui->staged_return;
        for (int send = 0; send < ENGINE_SEND_CAP; ++send) {
            float level; if (!gain(ui->gains[send], &level)) { SDL_strlcpy(ui->message, "Send percentages must be numbers from 0 to 100.", sizeof(ui->message)); return; }
            if(row->sends[send].return_id!=ui->destinations[send] && row->sends[send].automation.count){SDL_strlcpy(ui->message,"Clear this send curve before rebinding its destination.",sizeof(ui->message));return;}
            row->sends[send].return_id=ui->destinations[send];row->sends[send].gain=level;
        }
        DawActionStatus status = daw_action_set_routing(state, &ui->captured, &next, &output);
        if (accepted(ui, status)) { refresh(state); accepted(ui, status); }
    } else if (control >= 8 && control < 8 + ENGINE_SEND_CAP) {
        ui->focused = -1; kit_ui_text_cancel_composition(&ui->text_edit); daw_text_resume_input(state);
        int send = control - 8; uint64_t options[ENGINE_RETURN_CAP + 1] = {0}; int count = 1, index = 0;
        for (int t = 0; t < ui->captured.track_count; ++t) if (ui->captured.tracks[t].is_return && t != ui->selected) options[count++] = ui->captured.tracks[t].track_id;
        for (int i = 0; i < count; ++i) if (options[i] == ui->destinations[send]) index = i;
        ui->destinations[send] = options[(index + 1) % count];
        if (!ui->destinations[send]) strcpy(ui->gains[send], "0");
        SDL_strlcpy(ui->message, "Destination staged; edit its percentage, then Apply.", sizeof(ui->message));
    }
}
// Gives modal text and matching pointer releases priority over underlying authoring.
bool daw_routing_panel_event(AppState* state, const SDL_Event* event) {
    if (!state || !event || !state->routing_ui.open) return false;
    DawRoutingUI* ui = &state->routing_ui;
    if (state->inspector.visible || midi_editor_should_render(state)) { daw_routing_panel_release(state); return false; }
    if (event->type == SDL_WINDOWEVENT) {
        ui->pressed = -1;
        if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST || event->window.event == SDL_WINDOWEVENT_HIDDEN) daw_routing_panel_release(state);
        return false;
    }
    if(ui->automation_mode)return daw_mixer_automation_event(state,event);
    DawRoutingLayout layout; daw_routing_panel_layout(state, &layout);
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        ui->pressed = hit(ui, &layout, event->button.x, event->button.y);
        SDL_Point point = {event->button.x,event->button.y};
        if (layout.editable && ui->selected >= 0 && !ui->staged_return) for (int send = 0; send < ENGINE_SEND_CAP; ++send)
            if (SDL_PointInRect(&point, &layout.gains[send])) { focus(state, send); daw_text_edit_click(&ui->text_edit, ui->gains[send], sizeof(ui->gains[send]), &ui->cursor, KIT_UI_TEXT_SINGLE_LINE,.8f,layout.gains[send].x+5,layout.gains[send].w-10,event->button.x); }
        return true;
    }
    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        int pressed = ui->pressed; ui->pressed = -1;
        if (pressed >= 0 && pressed == hit(ui, &layout,event->button.x,event->button.y)) activate(state, pressed);
        return true;
    }
    if (event->type == SDL_KEYDOWN && event->key.repeat) return true;
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE) { daw_routing_panel_release(state); return true; }
    if (ui->focused >= 0) {
        KitUiTextEventResult result = daw_text_edit_event(&ui->text_edit, ui->gains[ui->focused], sizeof(ui->gains[ui->focused]), &ui->cursor, KIT_UI_TEXT_SINGLE_LINE,event);
        if (result.changed) ui->pressed = -1;
        if (result.submit && layout.editable) { ui->pressed = -1; activate(state, 6); }
    }
    return event->type == SDL_KEYDOWN || event->type == SDL_KEYUP || event->type == SDL_TEXTINPUT || event->type == SDL_TEXTEDITING ||
        event->type == SDL_MOUSEWHEEL || event->type == SDL_MOUSEMOTION || event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP;
}
// Draws one shared product button with clipped persistent text.
static void button(SDL_Renderer* renderer, SDL_Rect rect, const char* label, const DawThemePalette* theme) {
    if (rect.w <= 0 || rect.h <= 0) return;
    DawUiButtonSpec spec; daw_ui_button_spec_init(&spec,label); DawUiButtonStyle style; daw_ui_button_style_resolve(theme,&spec,&style);
    daw_ui_button_draw_frame(renderer,&rect,&style); ui_draw_text_clipped(renderer,rect.x+5,rect.y+3,label,theme->text_primary,.8f,rect.w-10);
}
// Paints cached routing using the existing effects-pane clipping and shared control/text primitives.
void daw_routing_panel_render(SDL_Renderer* renderer, const AppState* state) {
    if (!state->routing_ui.open) return;
    if(state->routing_ui.automation_mode){daw_mixer_automation_render(renderer,state);return;}
    DawRoutingLayout layout; daw_routing_panel_layout(state,&layout); DawRoutingUI* ui = (DawRoutingUI*)&state->routing_ui;
    DawThemePalette theme; daw_shared_theme_resolve_palette(&theme);
    SDL_SetRenderDrawColor(renderer,theme.inspector_fill.r,theme.inspector_fill.g,theme.inspector_fill.b,255); SDL_RenderFillRect(renderer,&layout.panel);
    snprintf(ui->lane_label,sizeof(ui->lane_label),"Routing: %s",ui->selected < 0 ? "No lanes" : ui->names[ui->selected]);
    ui_draw_text_clipped(renderer,layout.panel.x+8,layout.panel.y+8,ui->lane_label,theme.text_primary,.9f,layout.panel.w-16);
    const char* labels[] = {"Previous","Next","New Return","Refresh","Close",ui->staged_return?"Role: Return":"Role: Source","Apply","Auto"};
    for (int i = 0; i < 8; ++i) if (visible(&layout,layout.buttons[i])) button(renderer,layout.buttons[i],labels[i],&theme);
    if (layout.editable && !ui->staged_return) ui_draw_text_clipped(renderer,layout.gains[0].x,layout.panel.y+59,"Level %",theme.text_muted,.75f,80);
    if (layout.editable && !ui->staged_return) for (int send = 0; send < ENGINE_SEND_CAP; ++send) {
        const char* name = "Off";
        for (int t = 0; t < ui->captured.track_count; ++t) if (ui->captured.tracks[t].track_id == ui->destinations[send]) name = ui->names[t];
        snprintf(ui->send_labels[send],sizeof(ui->send_labels[send]),"Send %d: %s",send+1,name); button(renderer,layout.destinations[send],ui->send_labels[send],&theme);
        daw_control_frame(renderer,&layout.gains[send],theme.control_fill,theme.control_border);
        if (ui->focused == send) daw_text_edit_draw(renderer,&ui->text_edit,ui->gains[send],sizeof(ui->gains[send]),ui->cursor,layout.gains[send],theme.text_primary,theme.control_fill,.8f);
        else ui_draw_text_clipped(renderer,layout.gains[send].x+5,layout.gains[send].y+4,ui->gains[send],theme.text_primary,.8f,layout.gains[send].w-10);
    }
    if (layout.editable && ui->staged_return)
        ui_draw_text_clipped(renderer,layout.panel.x+8,layout.panel.y+82,"Return receives source sends. Close Routing to edit its effects.",theme.text_muted,.8f,layout.panel.w-16);
    ui_draw_text_clipped(renderer,layout.status.x,layout.status.y,layout.editable?ui->message:"Enlarge this pane to edit routing (360 x 275).",theme.text_muted,.75f,layout.status.w);
}
