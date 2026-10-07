#include "ui/editor_controls.h"
#include "app_state.h"
#include "input/input_manager.h"
#include "input/library_input.h"
#include "input/inspector_input.h"
#include "input/project_modal_input.h"
#include "input/timeline/timeline_input_mouse_click.h"
#include "input/midi_editor_input.h"
#include "input/midi_instrument_panel_input.h"
#include "input/effects_panel_input.h"
#include "input/effects_panel_input_helpers.h"
#include "input/effects_panel_track_snapshot.h"
#include "input/effects_panel_eq_detail_input.h"
#include "kit_ui_interaction_sdl.h"
#include "ui/shared_theme_font_adapter.h"

// Keeps text/modal/authoring and active continuous gestures ahead of discrete buttons.
static bool blocked(const AppState* state) {
    return !state->engine || state->bounce_active || project_modal_input_active(state) ||
        state->tempo_ui.editing || state->track_name_editor.editing ||
        library_input_is_editing(state) || inspector_input_has_text_focus(state) ||
        state->undo.active_drag_valid || state->layout_runtime.drag.active ||
        daw_workspace_authoring_host_active(&state->workspace_authoring);
}

// Calls the original product owner at its current accepted control rectangle.
static void activate(InputManager* manager, AppState* state, KitUiSurfaceKey key, SDL_Rect rect) {
    int x = rect.x + rect.w / 2, y = rect.y + rect.h / 2;
    switch (key.domain) {
    case 10: (void)library_input_handle_primary_click(state, x, y); break;
    case 11: (void)timeline_controls_activate_at(state, x, y); break;
    case 12: (void)midi_editor_input_activate_at(state, x, y); break;
    case 13: (void)midi_instrument_panel_input_activate_at(state, x, y); break;
    case 15: timeline_track_control_activate(state, key.value); break;
    case 14:
    case 16:
    case 17:
    case 18: {
        // Legacy effects commands remain in their owner; modifiers are bound to the press.
        SDL_Event click = {.type = SDL_MOUSEBUTTONDOWN};
        click.button.button = SDL_BUTTON_LEFT; click.button.clicks = 1;
        click.button.x = x; click.button.y = y;
        if (key.domain == 16 || key.domain == 17) {
            EffectsPanelLayout layout; effects_panel_compute_layout(state, &layout);
            if (key.domain == 16) (void)effects_panel_track_snapshot_handle_mouse_down(state, &layout, &click);
            else (void)effects_panel_eq_detail_handle_mouse_down(state, &layout, &click);
        } else effects_panel_input_activate_control(manager, state, &click, state->editor_controls.modifiers);
        // Meter presentation follows the accepted parameter even if publication/history refused.
        if (key.domain == 18) {
            int slot = find_slot_index_by_id(&state->effects_panel, (FxInstId)(key.value / 128));
            if (slot >= 0) sync_meter_modes_from_slot_params(&state->effects_panel, &state->effects_panel.chain[slot]);
        }
        break;
    }
    }
}

bool daw_editor_controls_at(AppState* state, int x, int y) {
    daw_editor_controls_sync(state);
    KitUiSurface* surface = &state->editor_controls.surface;
    for (unsigned i = 0; i < surface->count; ++i) {
        KitRenderRect r = surface->controls[i].bounds;
        if (x >= r.x && x < r.x+r.width && y >= r.y && y < r.y+r.height) return true;
    }
    return false;
}

bool daw_editor_controls_event(InputManager* manager, AppState* state, const SDL_Event* event) {
    daw_editor_controls_sync(state);
    DawEditorControls* ui = &state->editor_controls;
    KitUiInteractionEvent input; KitUiInteractionResult result;
    if (!kit_ui_interaction_event_from_sdl(event, &input)) return false;
    if (input.type == KIT_UI_INTERACTION_KEY_DOWN &&
        (blocked(state) || !ui->keyboard_focus ||
         (event->key.keysym.sym != SDLK_TAB && event->key.keysym.sym != SDLK_RETURN &&
          event->key.keysym.sym != SDLK_KP_ENTER))) return false;
    if (input.type == KIT_UI_INTERACTION_POINTER_DOWN) {
        ui->keyboard_focus = daw_editor_controls_at(state, event->button.x, event->button.y);
        if (!ui->keyboard_focus) ui->surface.interaction.focused_id = 0;
        ui->modifiers = SDL_GetModState();
    } else if (input.type == KIT_UI_INTERACTION_KEY_DOWN) ui->modifiers = event->key.keysym.mod;
    (void)kit_ui_surface_route(&ui->surface, &input, &result);
    if (result.activated_id && kit_ui_surface_take_activation(&ui->surface, result.activated_id)) {
        KitUiSurfaceKey key;
        if (kit_ui_surface_key(&ui->surface, result.activated_id, &key))
            for (unsigned i = 0; i < ui->surface.count; ++i) if (ui->surface.controls[i].id == result.activated_id) {
                KitRenderRect r = ui->surface.controls[i].bounds;
                activate(manager, state, key, (SDL_Rect){r.x,r.y,r.width,r.height});
                break;
            }
        daw_editor_controls_sync(state);
    }
    return result.consumed != 0;
}

void daw_editor_controls_draw_focus(SDL_Renderer* renderer, AppState* state) {
    daw_editor_controls_sync(state);
    DawEditorControls* ui = &state->editor_controls;
    KitRenderRect marker;
    if (!ui->keyboard_focus || !kit_ui_interaction_focus_marker(&ui->surface.interaction,
        ui->surface.controls, ui->surface.count, &marker)) return;
    DawThemePalette theme;
    if (!daw_shared_theme_resolve_palette(&theme)) return;
    SDL_SetRenderDrawColor(renderer, theme.text_primary.r, theme.text_primary.g,
        theme.text_primary.b, theme.text_primary.a);
    SDL_Rect rect = {marker.x,marker.y,marker.width,marker.height};
    SDL_RenderFillRect(renderer, &rect);
}
