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
  - Thin lower-pane MIDI editor event/update router. Owns instrument header clicks, grouped preset browser click/scroll routing, pointer/key dispatch order, and panel visibility transitions while delegating note selection, timing, commands, QWERTY capture, and gestures to focused sibling modules.
- `midi_editor_input_selection.c`
  - Owns selected MIDI region lookup, note snapshot/free helpers, selection masks, selection clearing, marquee/pending clear helpers, and note undo begin/commit/push helpers.
- `midi_editor_input_timing.c`
  - Owns MIDI editor frame helpers, quantize step/snap math, transport-relative frame mapping, time-ruler seek, horizontal clip-local viewport pan/zoom/fit helpers, modified-wheel pitch viewport scroll/zoom helpers, and selected-note pitch fit/recenter helpers used by the editor input router.
- `midi_editor_input_commands.c`
  - Owns selected-note quantize, Delete/Backspace note removal, copy/paste/duplicate, selected-pattern collection, and inserted-note selection remapping.
- `midi_editor_input_qwerty.c`
  - Owns QWERTY key-to-note mapping, active note slots, record/test toggles, audition note-off cleanup, default velocity/octave controls, and QWERTY event handling.
- `midi_editor_input_gestures.c`
  - Owns hover, marquee preview/commit, Shift-note pending state, note press slop, note create/drag/move/resize/velocity updates, selected-group velocity edits, and selected-group movement.
- `midi_instrument_panel_input.c`
  - Captures the selected MIDI region's instrument subview input, including return-to-notes routing, preset menu selection, parameter group tab selection, and per-region grouped knob drags by stable instrument parameter ID without mutating MIDI notes.
- `timeline/`
  - Timeline drag helpers keep MIDI right-edge resizing bounded by existing note content and leave left-edge MIDI trim deferred until offset semantics are explicit.
- `transport_input.c`
  - `transport_input_init`: Clear slider drag flags.
  - `transport_input_handle_event`: Handle clicks that toggle grid mode or begin slider drags.
  - `transport_input_update`: Continue slider adjustments while the mouse button stays down.

## Subdirectories
- `timeline/`: Timeline-specific input helpers (selection, drag operations, MIDI region creation/resizing, and the main event/update loop).
