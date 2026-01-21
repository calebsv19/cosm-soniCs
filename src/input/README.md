# Directory: src/input

Purpose: Translate SDL events and pointer state into engine/UI actions.

## Files
- `input_manager.c`
  - `input_manager_init`: Seed mouse/keyboard state and initialise subsystem-specific handlers.
  - `input_manager_handle_event`: Dispatch SDL events to transport, inspector, and timeline modules.
  - `input_manager_update`: Poll mouse state, update hover/drag zones, invoke per-module updates, and process shortcuts (play/stop, load, delete).
- `inspector_input.c`
  - `inspector_input_init/show/set_clip`: Manage which clip is being edited and expose inspector UI state.
  - `inspector_input_handle_event`: React to mouse/text events for renaming clips, adjusting gain, and routing fades.
- `inspector_fade_input.c`
  - Handles fade selection, dragging, and curve cycling in the clip inspector panel.
- `automation_input.c`
  - Captures automation snapshots and manages undo drag lifecycle for automation edits.
- `inspector_input_handle_gain_drag/stop_gain_drag`: Continue or stop gain scrubbing.
- `inspector_input_commit_if_editing/sync`: Persist edits back to the engine and pull latest clip data.
- `effects_panel_input.c`
  - Initializes the master effects panel state, synchronises effect snapshots, and handles the category/effect overlay navigation (including scroll wheel support), slider drags, and delete interactions when the mixer rack is active.
- `transport_input.c`
  - `transport_input_init`: Clear slider drag flags.
  - `transport_input_handle_event`: Handle clicks that toggle grid mode or begin slider drags.
  - `transport_input_update`: Continue slider adjustments while the mouse button stays down.

## Subdirectories
- `timeline/`: Timeline-specific input helpers (selection, drag operations, and the main event/update loop).
