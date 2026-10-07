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
  - Timeline drag helpers keep MIDI right-edge resizing bounded by existing note content. MIDI left-edge trim shifts/clips notes relative to the new region start; both current-state and gesture-baseline helpers publish notes, duration and position atomically.
- `transport_input.c`
  - `transport_input_init`: Clear slider drag flags.
  - `transport_input_handle_event`: Handle existing tempo fields and slider gestures; shared transport actions dispatch through `transport_input_activate_control`.
  - `transport_input_update`: Continue slider adjustments while the mouse button stays down.

## Subdirectories
- `timeline/`: Timeline-specific input helpers (selection, drag operations, MIDI region creation/resizing, and the main event/update loop).

Play/Stop and Space transport changes dispatch on SDL events so quick presses survive between frames. Save/load modals consume their complete event stream to prevent global recording or transport shortcuts from leaking through text entry. Other gesture polling remains subject to the S5 interaction audit.

S5 compound movement/slip previews share `timeline_apply_compound_preview`: derive absolute target states from complete initial history, publish once, then resolve selection/inspector indices by clip identity. Cross-track placement now shares the batch path; integrated overlap and generated-track history now use retained content; other compound edit entry points remain migration work; see the S5 ledger.

Single/multi slide drops, including compound drops that append tracks, use retained content history and one placement/overlap publication. Only actual slide gestures trigger the destructive overlap pass; click-only selection and slip/ripple release do not. Native gesture acceptance remains open.

Ripple movement captures downstream audio and MIDI by stable clip identity. Audio left trim publishes region bounds and timeline position together and restores selection by identity after sorting. The current gesture map has ordinary edge trim, Alt audio-edge fade and Alt body ripple movement; it has no ripple-trim gesture. Unreachable incremental ripple trim/move code has been removed.

Single-clip slide and MIDI left-trim sorting remap the entire selection and primary index, including crossed neighbors. Compound reorders use their separate stable-identity reconciliation path.

Inspector numeric clip bounds/position edits reserve history before applying, retain typed input on rejection, and refresh the correct clip after sorting. Finite frame conversion is checked before integer casts; source-bound clamping retains existing engine semantics. Playback rate remains inspector-local rather than an implemented engine rate control.

Inspector rename reserves history before changing audio/MIDI names and resolves undo/redo by clip creation identity across sorting and track moves. Inspector mouse-up only finalizes history when its own gain/fade drag was active; unrelated timeline history is left for its owner.

Inspector gain/fade drags and fade-curve commands require a complete reserved history entry before editing. Fade history includes MIDI notes/instrument state and stable track identity. Release records scalar readback in the existing after-state without another allocation; no-op or rejected commands discard only their reservation.

Timeline Ctrl/Cmd-D and selection delete now use complete retained-content actions across audio/MIDI with one pre-reserved undo entry. Failure preserves selection and redo. The shortcut reads event-local modifiers. Undo/redo restores explicit original/copy/deleted selection identities and aligns primary selection with the inspector. Clipboard paste now uses the complete insertion transaction; other history families remain follow-ups.

Clipboard copy replaces the prior clipboard only after the complete selection is prepared; rejected copy preserves the last usable contents. Paste stops on rejected/no-progress destination-track creation. Paste now reserves one history entry and publishes complete inserted clips/properties and required tracks together. Generated-track undo guards preserve later authored edits; timing and destination policy are unchanged.

Effects slider, track gain/pan and EQ gesture startup now require a successful undo reservation. Effects gestures retain a reservation serial, so stale motion/release cannot take ownership of a newer command. EQ toggles/reset also reserve before editing; pending curve publication precedes release history capture, and rejection restores the last accepted preview. Numeric inspector and rename buffers record their starting clip identity and retain text without mutation when the current target differs. Effects target binding, discrete mixer actions and history rejection feedback are extended by the recovery continuation below; native acceptance remains open.


Recovery continuation binds active effects gestures and mixer/EQ/FX history to the original track runtime identity. Discrete effect add/remove, bypass, reorder, parameter/mode edits, mute/solo and instrument preset selection reserve history before mutation. Native pointer/keyboard acceptance remains unverified; see S5 implementation section 24.

Migrated transport/menu buttons now use shared matched-release activation and scope/geometry cancellation. Global Space/Shift+Space retain their engine shortcut meanings. Modal/text/authoring owners block background button scope.

## 2026-10-06 bounded text/modal adoption

Main Edit now adopts the pinned kit_ui 0.18.0 text editing/presentation contract in
six existing owner families and shared Load/Cancel focus. Product publication,
validation, retry and cancel policies remain local. Fresh compile, targeted real
owner tests and native dark/light Vulkan overlay checks qualify this subset.
Remaining editor controls, pane composition and actual-loop fullscreen lifecycle
are separate rollout slices. Native OS IME and physical audio acceptance are not
claimed. See `docs/shared_ui_rollout.md`. VERSION remains 0.3.0; no release action.

### Editor controls rollout (2026-10-06)

Five discrete-control groups now share release/cancellation/focus through sibling
`editor_controls` adapters: library modes, timeline toolbar, MIDI editor,
instrument navigation/presets and effects header/slot/overlay controls. Existing
command owners and continuous gestures remain local. The common button frame
uses shared rounded tokens; product palette, status and geometry remain owned by
Sonics. See `docs/shared_ui_rollout.md` for exact coverage and retained exceptions.
