# UI module notes

- `font.h` / `font.c`: public UI font facade; active Vulkan draw/measure now routes through shared `kit_render_external_text.*`.
- `font_bridge.c`: bounded logical-font cache and shared font-source registration for the UI text lane.
- `text_draw.c`: shared text draw/measure wrapper plus the remaining local clipped-draw seam.
- `layout.c`: Pane layout and rendering helpers.
- `transport.c`: Transport bar (play/stop, time readout, grid toggle, zoom sliders).
- `timeline_view.c`: Timeline lanes, clips, header bar controls, selection drawing, and automation-mode overlays.
- `timeline_midi_clip_preview.c`: Timeline-region mini-renderer for velocity-colored MIDI clip note blocks, scaled by note timing and clip-local pitch bounds, with stable full-clip-width X mapping for right-edge resize.
- `timeline_waveform.c`: Waveform cache for per-clip rendering.
- `waveform_render.c`: Waveform renderer helpers shared by timeline/inspector views.
- `kit_viz_fx_preview_adapter.c`: Shared plotting adapter for effects previews using `kit_viz` segment generation.
- `effects_panel.c`: Effects UI rendering and layout.
- `effects_panel/spec_panel.c`: Spec-driven effects panel widgets and layout (new panel mode).
- `effects_panel/meter_detail_*.c`: Meter detail views (correlation, vectorscope, peak/RMS, LUFS, spectrogram).
- `clip_inspector.c`: Clip inspector panel (gain, fades, naming, waveform fade overlays).
- `midi_editor.c`: Bottom-pane MIDI editor for selected MIDI regions, including piano lanes with full-width sharp-note shading and C-row markers, tempo-map quantize timing grid, note geometry/hit-testing with small note hit slop, selected-region summary with selected-note count, an `Instrument` preset browser, compact QWERTY record/test, quantize, default-velocity, octave, and instrument-panel header controls, active-key lane highlighting, velocity-colored existing-note drawing, multi-note selected-set highlighting, marquee preview drawing, and hover/selected-note highlighting.
- `midi_editor_pitch_view.c`: Editor-local pitch viewport state helpers for selected MIDI clips, including clip-identity matching, row-count clamping, top-note clamping, default range resolution, and visible-range note/row conversion.
- `midi_preset_browser.c`: Shared grouped/scrollable factory preset browser renderer and hit-test helper used by both current MIDI instrument preset surfaces.
- `midi_instrument_panel.c`: Bottom-pane instrument subview for the selected MIDI region, replacing the MIDI editor when opened from the editor header and owning the per-region preset browser, DAW-local parameter group tabs, compact knob-style controls routed by stable instrument parameter IDs, and a parameter-aware waveform/envelope preview for the selected preset.
- `library_browser.c`: Asset browser panel for audio files.

`ui_fade_curve_eval` delegates to the UI-independent engine curve helper so existing arrangement/inspector overlays and audio playback share the same shape definitions. Curve choices and overlay layout remain unchanged; see [S3.1](../../docs/improvement/S3-FADE-PARITY.md).

Spectrum labels distinguish mid-signal dBFS from EQ gain and identify post-EQ/pre-pan or post-master-FX taps. Its display uses the full fixed dBFS range and no frame-rate-dependent smoothing. Spectrogram labels identify the meter insert and relative sample-window age; equal levels retain equal colors across history. See [S3.2](../../docs/improvement/S3-ANALYSIS-CALIBRATION.md).

S3.3 labels limiter/compressor scopes `GR peak` for the most negative applied block gain before makeup. Other gain-ratio scopes are labeled `RMS delta`; their history scale is bipolar. Audio diagnostics include the latest worker DSP delay separately from queue delay. These labels do not claim hardware latency or external sidechain routing.


S4.7 scans keep version-bound metadata and schedule owned background probing/content hashing. Drag previews reuse that metadata. The library status reports pending, completed, canceled or failed import; Ctrl/Cmd+Escape cancels pending work. See [S4.7](../../docs/improvement/S4.7-MEDIA.md).


The rack spectrogram card now subscribes to the existing worker analyzer and renders its calibrated history directly. The selected spectrogram takes precedence when multiple meters are present; inactive cards explain how to select them, and bypassed cards are labeled. Raw lifetime audio totals are shown only with enable_timing_logs; recording status and edit rejection messages remain visible normally.

Rack spectrogram cards expose W/B, B/W and Heat palette buttons. Palette choices use the existing effect parameter and undo transaction; display colors read back the accepted parameter, including after undo and project reload.

- `transport_controls.c`: shared surface/focus and single rounded transport painter; product rectangles/palette/status and direct commands remain app-owned. See `docs/shared_ui_rollout.md`.

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
