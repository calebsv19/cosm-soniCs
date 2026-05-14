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
- `midi_editor_input.c`
  - Captures lower-pane MIDI editor input and applies time-ruler playhead seek, editor-local hover-routed zoom/pan viewport controls, click-create, hover/click slop, selected-first drag-move, selected-first edge-resize, selected-group click-drag movement with click-release collapse, Shift-click multi-note selection, Shift-empty-grid marquee selection, immediate/group shift-drag velocity edits, Delete/Backspace, selected-set quantize, selected-note copy/paste/duplicate, snap-enabled quantized create/drag/resize/QWERTY timing, `R`-armed QWERTY note recording with undo snapshots, non-recording Test-mode QWERTY audition, default velocity/octave controls for QWERTY input, per-region instrument preset dropdown selection, and the explicit instrument-panel open affordance.
- `midi_instrument_panel_input.c`
  - Captures the selected MIDI region's instrument subview input, including return-to-notes routing, preset menu selection, and per-region Level/Tone/Attack/Release slider drags without mutating MIDI notes.
- `timeline/`
  - Timeline drag helpers keep MIDI right-edge resizing bounded by existing note content and leave left-edge MIDI trim deferred until offset semantics are explicit.
- `transport_input.c`
  - `transport_input_init`: Clear slider drag flags.
  - `transport_input_handle_event`: Handle clicks that toggle grid mode or begin slider drags.
  - `transport_input_update`: Continue slider adjustments while the mouse button stays down.

## Subdirectories
- `timeline/`: Timeline-specific input helpers (selection, drag operations, MIDI region creation/resizing, and the main event/update loop).
