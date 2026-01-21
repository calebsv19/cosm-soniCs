# UI module notes

- `font.h` / `font.c`: SDL_ttf-backed font helper (float scaling, cached sizes).
- `layout.c`: Pane layout and rendering helpers.
- `transport.c`: Transport bar (play/stop, time readout, grid toggle, zoom sliders).
- `timeline_view.c`: Timeline lanes, clips, header bar controls, selection drawing, and automation-mode overlays.
- `timeline_waveform.c`: Waveform cache for per-clip rendering.
- `waveform_render.c`: Waveform renderer helpers shared by timeline/inspector views.
- `effects_panel.c`: Effects UI rendering and layout.
- `effects_panel/meter_detail_*.c`: Meter detail views (correlation, vectorscope, peak/RMS, LUFS, spectrogram).
- `clip_inspector.c`: Clip inspector panel (gain, fades, naming, waveform fade overlays).
- `library_browser.c`: Asset browser panel for audio files.
