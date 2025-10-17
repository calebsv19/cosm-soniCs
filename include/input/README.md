# Directory: include/input

Purpose: Input handling contracts that connect SDL events to the app state.

## Files
- `input_manager.h`: Aggregates mouse/keyboard state and exposes `input_manager_init`, `_handle_event`, and `_update`.
- `inspector_input.h`: Inspector panel hooks—show/edit clip metadata and gain via `inspector_input_*` helpers.
- `transport_input.h`: Slider and button handlers driving the transport UI (`transport_input_*`).
- `timeline_input.h`: Main timeline interaction surface—initialisation, event handling, and per-frame updates.
- `timeline_selection.h`: Selection bookkeeping helpers used by input handlers and keyboard shortcuts.
- `timeline_drag.h`: Clip move/overlap utilities consumed by the timeline input flow.
