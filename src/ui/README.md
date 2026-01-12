# UI module notes

- `font.h` / `font.c`: SDL_ttf-backed font helper (float scaling, cached sizes).
- `layout.c`: Pane layout and rendering helpers.
- `transport.c`: Transport bar (play/stop, time readout, grid toggle, zoom sliders).
- `timeline_view.c`: Timeline lanes, clips, headers, selection drawing.
- `timeline_waveform.c`: Waveform cache for per-clip rendering.
- `waveform_render.c`: Waveform renderer helpers shared by timeline/inspector views.
- `effects_panel.c`: Effects UI rendering and layout.
- `clip_inspector.c`: Clip inspector panel (gain, fades, naming).
- `library_browser.c`: Asset browser panel for audio files.
