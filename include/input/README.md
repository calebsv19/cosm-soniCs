# Directory: include/input

Purpose: Input handling contracts that connect SDL events to the app state.

## Files
- `input_manager.h`: Aggregates mouse/keyboard state and exposes `input_manager_init`, `_handle_event`, and `_update`.
- `inspector_input.h`: Inspector panel hooks—show/edit clip metadata and gain via `inspector_input_*` helpers.
- `inspector_fade_input.h`: Inspector panel fade-specific handlers and selection helpers.
- `automation_input.h`: Automation snapshot and undo helpers for timeline/inspector editing.
- `effects_panel_input.h`: Effects panel mouse/keyboard dispatch, including track snapshot interaction and grouped preset browser scrolling for track instrument defaults.
- `transport_input.h`: Slider and button handlers driving the transport UI (`transport_input_*`).
- `timeline_input.h`: Main timeline interaction surface—initialisation, event handling, and per-frame updates.
- `midi_editor_input.h`: Bottom-pane MIDI editor input capture contract for selected MIDI regions, including QWERTY capture-state queries, multi-note selection/marquee state, editor-local viewport/ruler interaction, selected-note command routing for transpose/grid nudge/grid duration resize, quantize/default-velocity/octave UI state, grouped preset browser selection/scroll, and the instrument-panel open path.
- `midi_instrument_panel_input.h`: Bottom-pane instrument subview input contract for selected MIDI regions, including grouped preset browser selection/scroll and per-region parameter slider state.
- `timeline_selection.h`: Selection bookkeeping helpers used by input handlers and keyboard shortcuts.
- `timeline_drag.h`: Clip move/overlap utilities consumed by the timeline input flow.
