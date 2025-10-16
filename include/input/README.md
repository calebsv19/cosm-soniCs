# Directory: include/input

Purpose: Input handling contracts that connect SDL events to the app state.

## Files
- `input_manager.h`: Aggregates mouse/keyboard state and exposes `input_manager_init`, `_handle_event`, and `_update`.
- `inspector_input.h`: Inspector panel hooks—show/edit clip metadata and gain via `inspector_input_*` helpers.
- `timeline_input.h`: Timeline drag/trim logic entry points (`timeline_input_init/handle_event/update`).
- `transport_input.h`: Slider and button handlers driving the transport UI (`transport_input_*`).
