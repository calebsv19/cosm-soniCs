#include "ui/automation_menu.h"
#include "app_state.h"
#include "ui/font.h"
#include "ui/shared_theme_font_adapter.h"
#include "engine/engine.h"
#include <string.h>

static const char* const labels[ENGINE_AUTOMATION_TARGET_COUNT] = {
    "Volume / Gain", "Stereo Balance / Pan", "Instrument Level", "Tone", "Attack", "Release",
    "Decay", "Sustain", "Oscillator Mix", "Oscillator Detune", "Sub Mix", "Drive",
    "Vibrato Rate", "Vibrato Depth", "Filter Cutoff", "Filter Resonance", "Filter Envelope",
    "Filter Decay", "Filter LFO Rate", "Filter LFO Depth", "Body Decay", "Pitch Drop",
    "Pitch Decay", "Noise Mix", "Noise Decay", "Click Mix", "Dynamics", "Velocity Timbre"
};
// Enables instrument targets only when the selected region can render them.
static bool target_available(const AppState* state, int target) {
    if (target < 0 || target >= ENGINE_AUTOMATION_TARGET_COUNT) return false;
    if (target < 2) return true;
    const EngineTrack* tracks = state && state->engine ? engine_get_tracks(state->engine) : NULL;
    int t = state ? state->selected_track_index : -1, c = state ? state->selected_clip_index : -1;
    if (tracks && t >= 0 && t < engine_get_track_count(state->engine) && c >= 0 &&
        c < tracks[t].clip_count && tracks[t].clips[c].kind == ENGINE_CLIP_KIND_MIDI) {
        EngineInstrumentParamId param;
        EngineInstrumentPresetId preset = engine_clip_midi_effective_instrument_preset(state->engine,t,c);
        return engine_instrument_param_from_automation_target(target,&param) &&
            engine_instrument_preset_param_active(preset,param);
    }
    return false;
}
// Fits all targets in two columns within the current window.
SDL_Rect automation_menu_target_rect(const AppState* state, int target) {
    if (!state || target < 0 || target >= ENGINE_AUTOMATION_TARGET_COUNT) return (SDL_Rect){0};
    int rows = (ENGINE_AUTOMATION_TARGET_COUNT + 1) / 2;
    int h = ui_font_line_height(1.0f) + 14;
    if (h < 28) h = 28;
    int footer_h = ui_font_line_height(1.0f) + 14;
    if (rows * h + footer_h > state->window_height - 12)
        h = (state->window_height - footer_h - 12) / rows;
    if (h < 1) h = 1;
    int w = state->window_width / 2 - 8;
    if (w > 245) w = 245;
    if (w < 1) w = 1;
    SDL_Rect anchor = state->timeline_controls.automation_target_rect;
    int x = anchor.x, y = anchor.y + anchor.h + 4;
    if (x + 2 * w > state->window_width - 4) x = state->window_width - 2 * w - 4;
    if (x < 4) x = 4;
    if (y + rows * h + footer_h > state->window_height - 4)
        y = state->window_height - rows * h - footer_h - 4;
    if (y < 4) y = 4;
    return (SDL_Rect){x + (target / rows) * w, y + (target % rows) * h, w, h};
}
// Opens the menu without changing the selected lane or creating history.
void automation_menu_open(AppState* state) {
    if (!state) return;
    state->automation_ui.menu_open = !state->automation_ui.menu_open;
    state->automation_ui.menu_hover = (int)state->automation_ui.target < ENGINE_AUTOMATION_TARGET_COUNT ? state->automation_ui.target : 0;
}
// Selects one target and clears point selection belonging to the previous lane.
static void choose(AppState* state, int target) {
    if (!target_available(state, target)) return;
    state->automation_ui.target = target;
    state->automation_ui.point_index = -1;
    state->timeline_automation_mode = true;
    state->automation_ui.menu_open = false;
}
// Prevents a menu selection or dismissal from falling through to notes and timeline points.
bool automation_menu_event(AppState* state, const SDL_Event* event) {
    if (!state || !event) return false;
    if (event->type == SDL_WINDOWEVENT && (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
        event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) state->automation_ui.menu_open = false;
    if (state->automation_ui.menu_release && event->type == SDL_MOUSEBUTTONUP) {
        state->automation_ui.menu_release = false; return true;
    }
    if (!state->automation_ui.menu_open) return false;
    if (event->type == SDL_KEYDOWN) {
        int n = ENGINE_AUTOMATION_TARGET_COUNT;
        switch (event->key.keysym.sym) {
        case SDLK_ESCAPE: state->automation_ui.menu_open = false; break;
        case SDLK_UP: state->automation_ui.menu_hover = (state->automation_ui.menu_hover + n - 1) % n; break;
        case SDLK_DOWN: state->automation_ui.menu_hover = (state->automation_ui.menu_hover + 1) % n; break;
        case SDLK_RETURN: case SDLK_KP_ENTER:
            if (state->automation_ui.menu_hover >= 0 && state->automation_ui.menu_hover < n)
                choose(state, state->automation_ui.menu_hover);
            break;
        default: break;
        }
        return true;
    }
    if (event->type == SDL_MOUSEMOTION || event->type == SDL_MOUSEBUTTONDOWN) {
        SDL_Point p = event->type == SDL_MOUSEMOTION ? (SDL_Point){event->motion.x,event->motion.y} :
            (SDL_Point){event->button.x,event->button.y};
        int hit = -1;
        for (int i = 0; i < ENGINE_AUTOMATION_TARGET_COUNT; ++i) {
            SDL_Rect rect = automation_menu_target_rect(state,i);
            if (SDL_PointInRect(&p,&rect)) { hit = i; break; }
        }
        if (event->type == SDL_MOUSEMOTION) state->automation_ui.menu_hover = hit;
        else {
            if (event->button.button == SDL_BUTTON_LEFT && hit >= 0) {
                state->automation_ui.menu_hover = hit;
                choose(state,hit);
            }
            else state->automation_ui.menu_open = false;
            state->automation_ui.menu_release = true;
        }
        return true;
    }
    return event->type == SDL_MOUSEBUTTONUP || event->type == SDL_MOUSEWHEEL || event->type == SDL_KEYUP;
}
// Paints full target names and distinct selected and hovered rows over existing panes.
void automation_menu_render(SDL_Renderer* renderer, AppState* state) {
    if (!renderer || !state || !state->automation_ui.menu_open) return;
    DawThemePalette theme;
    if (!daw_shared_theme_resolve_palette(&theme)) return;
    for (int i = 0; i < ENGINE_AUTOMATION_TARGET_COUNT; ++i) {
        SDL_Rect rect = automation_menu_target_rect(state,i);
        bool available = target_available(state,i);
        SDL_Color fill = available && i == (int)state->automation_ui.target ? theme.selection_fill :
            i == state->automation_ui.menu_hover ? theme.control_hover_fill : theme.control_fill;
        SDL_SetRenderDrawColor(renderer,fill.r,fill.g,fill.b,255); SDL_RenderFillRect(renderer,&rect);
        SDL_SetRenderDrawColor(renderer,theme.control_border.r,theme.control_border.g,theme.control_border.b,255);
        SDL_RenderDrawRect(renderer,&rect);
        ui_draw_text(renderer,rect.x+9,rect.y+(rect.h-ui_font_line_height(1.0f))/2,labels[i],available ? theme.text_primary : theme.text_muted,1);
    }
    SDL_Rect first = automation_menu_target_rect(state,0);
    SDL_Rect last = automation_menu_target_rect(state,ENGINE_AUTOMATION_TARGET_COUNT-1);
    SDL_Rect footer = {first.x,last.y+last.h,first.w*2,ui_font_line_height(1.0f)+14};
    SDL_SetRenderDrawColor(renderer,theme.control_fill.r,theme.control_fill.g,theme.control_fill.b,255);
    SDL_RenderFillRect(renderer,&footer);
    SDL_SetRenderDrawColor(renderer,theme.control_border.r,theme.control_border.g,theme.control_border.b,255);
    SDL_RenderDrawRect(renderer,&footer);
    const char* reason = !target_available(state,2) ?
        "Instrument parameters disabled: select a MIDI region" :
        state->automation_ui.menu_hover >= 2 && !target_available(state,state->automation_ui.menu_hover) ?
        "Disabled: selected instrument does not use this control" :
        "Dimmed controls are not used by the selected instrument";
    ui_draw_text(renderer,footer.x+9,footer.y+7,reason,theme.text_muted,1);
}
