# UI module notes

- `font.h` / `font.c`: public UI font facade; active Vulkan draw/measure now routes through shared `kit_render_external_text.*`.
- `font_bridge.c`: bounded logical-font cache and shared font-source registration for the UI text lane.
- `text_draw.c`: shared text draw/measure wrapper plus the remaining local clipped-draw seam.
- `layout.c`: Pane layout and rendering helpers.
- `transport.c`: Transport bar (play/stop, time readout, grid toggle, zoom sliders).
- `timeline_view.c`: Timeline lanes, clips, header bar controls, selection drawing, and automation-mode overlays.
- `timeline_waveform.c`: Waveform cache for per-clip rendering.
- `waveform_render.c`: Waveform renderer helpers shared by timeline/inspector views.
- `kit_viz_fx_preview_adapter.c`: Shared plotting adapter for effects previews using `kit_viz` segment generation.
- `effects_panel.c`: Effects UI rendering and layout.
- `effects_panel/spec_panel.c`: Spec-driven effects panel widgets and layout (new panel mode).
- `effects_panel/meter_detail_*.c`: Meter detail views (correlation, vectorscope, peak/RMS, LUFS, spectrogram).
- `clip_inspector.c`: Clip inspector panel (gain, fades, naming, waveform fade overlays).
- `library_browser.c`: Asset browser panel for audio files.
