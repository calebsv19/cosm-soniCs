# Directory: src/ui

Purpose: Render the DAW UI, manage pane layout, and draw widget chrome.

## Files
- `clip_inspector.c`
  - `clip_inspector_compute_layout`: Calculate rectangles for the inspector panel controls.
  - `clip_inspector_render`: Draw labels, editable name fields, gain slider, and clip metadata.
- `effects_panel.c`
  - `effects_panel_compute_layout`: Lay out master-rack columns, dynamically size slider rows, and position the two-layer overlay dropdown with scroll state.
  - `effects_panel_render`: Draw effect headers, delete buttons, sliders, and the category/effect overlay (including scrollbar/thumb) when no clip is selected.
- `font5x7.c`
  - `ui_draw_text`: Render uppercase characters with a 5x7 bitmap font scaled per call.
- `layout.c`
  - `ui_init_panes`: Seed pane structures, colours, and default visibility.
  - `ui_layout_panes`: Compute pane rectangles from window size and ratios.
  - `ui_ensure_layout`: Recompute layout when the renderer output size changes.
  - `ui_render_panes/ui_render_controls/ui_render_overlays`: Paint pane backgrounds, widgets, and status overlays.
  - `ui_layout_update_zones/ui_layout_handle_pointer/ui_layout_handle_hover`: Define resize hit areas, process drag interactions, and update hover state.
  - `ui_layout_get_pane`: Convenience accessor used by other modules.
- `layout_config.c`
  - `ui_layout_config_get`: Provide default ratios and minimum sizes for panes.
- `library_browser.c`
  - `library_browser_init/scan`: Prepare directory state and load `.wav` entries.
  - `library_browser_render`: Draw the list, highlighting hovered/selected rows.
  - `library_browser_hit_test`: Map mouse coordinates back to an item index.
- `panes.c`
  - `pane_manager_init`: Register panes for hover tracking.
  - `pane_manager_update_hover`: Flag whichever pane currently sits under the cursor.
- `timeline_view.c`
  - `timeline_view_render`: Paint lane-specific grids, headers (with rename editing caret), clips, selection handles, playhead, track-aware drag/drop ghosts, add/remove/loop controls, and per-track mute/solo buttons.
- `transport.c`
  - `transport_ui_init`: Initialise button/slider/seek rectangles and flags.
  - `transport_ui_layout`: Position controls inside the panel and size the seek bar/time readout.
  - `transport_ui_update_hover/transport_ui_sync`: Update hover state and align slider/seek handles with timeline settings and playhead position.
  - `transport_ui_render`: Draw buttons, sliders, seek bar, and labels (including current playback time).
  - `transport_ui_click_play/stop`: Hit-testing helpers used by the input system.
