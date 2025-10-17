# Directory: src/input/timeline

Purpose: Focused helpers that keep timeline interaction code small and modular.

## Files
- `timeline_input.c`
  - `timeline_input_init`: Reset timeline selection/drag state and hover bookkeeping.
  - `timeline_input_handle_event`: Respond to clicks/drags, maintain multi-selection, handle track add/remove, loop buttons, and drop targets.
  - `timeline_input_update`: Advance in-flight drags, apply snapping, sync inspector/fade adjustments, and update destination track hints.
- `timeline_selection.c`
  - Selection utilities (`timeline_selection_add/remove/clear/update_index/delete`) used by the input manager and keyboard shortcuts.
- `timeline_drag.c`
  - Cross-track move helpers (`timeline_move_clip_to_track`, overlap resolution, sampler lookups) shared by the input and selection modules.
