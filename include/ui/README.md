# Directory: include/ui

Purpose: UI layout and widget-facing headers shared between renderer code and input systems.

## Files
- `clip_inspector.h`: Layout calculator (`clip_inspector_compute_layout`) and renderer (`clip_inspector_render`) for the clip properties pane.
- `font.h`: SDL_ttf-backed font helper (`ui_draw_text`) used across panels.
- `layout.h`: Master UI surface—initialisation, layout, rendering, and resize zone helpers (`ui_init_panes`, `ui_layout_panes`, `ui_render_*`, etc.).
- `layout_config.h`: Accessor `ui_layout_config_get` exposing tweakable ratios and minimum sizes.
- `library_browser.h`: File picker view primitives (init/scan/render/hit-test).
- `midi_editor.h`: Bottom-pane MIDI editor layout, header affordance/dropdown rectangles, quantize/default-velocity/octave control rectangles, instrument-panel open rectangle, note geometry/hit testing, selection lookup, and render contract for selected MIDI regions.
- `midi_instrument_panel.h`: Bottom-pane instrument subview layout/render contract for the selected MIDI region, including preset menu rectangles and per-region parameter slider rectangles.
- `panes.h`: `Pane` structs and `pane_manager_*` functions for hover/highlight state.
- `resize.h`: Enumerations and structs describing resizable zones and drag tracking.
- `timeline_view.h`: `timeline_view_render` contract for drawing tracks and clips.
- `transport.h`: `TransportUI` description plus helpers for layout, hover, rendering, and click tests.
