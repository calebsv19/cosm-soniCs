#include "app_state.h"
#include "input/input_manager.h"
#include "input/timeline_input.h"
#include "input/inspector_input.h"
#include "input/effects_panel_input.h"
#include "input/midi_editor_input.h"
#include "input/midi_instrument_panel_input.h"
#include "ui/pane_composition.h"
#include "ui/text_edit.h"
static void cancel_surface(KitUiSurface* surface) {
    KitUiInteractionEvent cancel={.type=KIT_UI_INTERACTION_CANCEL};
    KitUiInteractionResult ignored;
    (void)kit_ui_surface_route(surface,&cancel,&ignored);
    kit_ui_surface_set_scope(surface,0);
}
// End accepted continuous edits through their existing release/undo owners.
// Discrete controls cancel first, so a native transition cannot activate a button.
void input_manager_cancel_window_gestures(AppState* state) {
    if(!state)return;
    cancel_surface(&state->editor_controls.surface);
    cancel_surface(&state->transport_ui.controls);
    cancel_surface(&state->project_modal_controls.surface);
    state->editor_controls.keyboard_focus=false;
    daw_text_cancel_composition(state);
    daw_pane_resize_cancel(state);
    state->timeline_drag.pending_shift_select=false;
    SDL_Event release={.type=SDL_MOUSEBUTTONUP};release.button.button=SDL_BUTTON_LEFT;
    release.button.x=state->mouse_x;release.button.y=state->mouse_y;
    InputManager* manager=&state->input_manager;
    inspector_input_handle_event(manager,state,&release);
    effects_panel_input_handle_event(manager,state,&release);
    midi_instrument_panel_input_handle_event(manager,state,&release);
    midi_editor_input_handle_event(manager,state,&release);
    timeline_input_handle_event(manager,state,&release);
    timeline_input_update(manager,state,true,false);
    midi_instrument_panel_input_update(manager,state,true,false);
    midi_editor_input_update(manager,state,true,false);
    effects_panel_input_update(manager,state,true,false);
    state->dragging_library=false;state->drag_library_index=-1;
    manager->previous_buttons=manager->current_buttons=0;
    manager->window_wait_release=true;
}
