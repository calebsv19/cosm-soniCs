# UI module notes

- Sub/Driven 808 expose Mono and Glide; Open Hat and Hat Kit join Drums with a Hat Choke switch. The existing isolated-note preview shows onset timbre; multi-note glide/choke requires a pattern.

- Percussion factory voices add the Drums category; 808 voices remain in Bass. The instrument pane shows only applicable controls, an onset waveform and the production body/noise decay preview. See [percussion controls](../../docs/percussion_synth.md).

- `font.h` / `font.c`: public UI font facade; active Vulkan draw/measure now routes through shared `kit_render_external_text.*`.
- `font_bridge.c`: bounded logical-font cache and shared font-source registration for the UI text lane.
- `text_draw.c`: shared text draw/measure wrapper plus the remaining local clipped-draw seam.
- `layout.c`: Pane layout and rendering helpers.
- `transport.c`: Transport bar (play/stop, time readout, grid toggle, zoom sliders).
- `timeline_view.c`: Timeline lanes, clips, header bar controls, selection drawing, and automation-mode overlays. It consumes the same vertical viewport and offset as input, clips lanes below fixed chrome, and paints the bounded scrollbar and Height buttons.
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
- `midi_instrument_panel.c`: Bottom-pane instrument subview for the selected MIDI region, replacing the MIDI editor when opened from the editor header and owning the per-region preset browser, DAW-local parameter group tabs, compact knob-style controls routed by stable instrument parameter IDs, and bounded plots of the actual isolated voice and gate-aware ADSR, preset-supported groups, ADSR control order, and logarithmic physical-time knobs with latched Shift fine adjustment. See [instrument controls](../../docs/instrument_controls.md).
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


### Discrete exceptions (2026-10-06)

`editor_control_exceptions.c` collects track-header, snapshot, EQ/meter and spec
controls without painting another widget layer. It shares exact specialized
geometry/clips with the existing painters and command owners. Hidden detail
controls are excluded, preset overlays own their rows, and time-mode/spec/view
changes cancel pending presses. The pinned shared API remains unchanged. See
`docs/shared_ui_rollout.md`; pane composition and full window lifecycle remain next.

## Pane composition adapter

`pane_composition.c` maps the fixed product panes to pinned kit_pane shell/header/content regions and preserves enclosing native Vulkan clips. Product layout and painters remain local; resize cancellation is separate from explicit workspace authoring. See `docs/shared_ui_rollout.md` for qualification and limits.

## Control chrome and initial rollout closeout

`control_chrome.c` owns thin slider rails/markers and their independent input geometry. Rounded frames reuse the existing kit_ui-backed `daw_ui_button` painter with product colors. It is an adapter, not a second rendering layer. Rack/title/parameter and specialized EQ/meter controls use one replacement frame. Plots and continuous command/undo owners remain product-specific. The initial macOS rollout is source-qualified; human Main Edit comparison and OS IME/platform/physical acceptance remain separate. See `docs/shared_ui_rollout.md`.

`daw_control_text` centralizes rounded-frame caption insets, measured alignment, vertical fitting and horizontal clipping. Parameter names/values use leading alignment and explicit vertical clearance; action/toggle captions use centered alignment. It paints once using the existing font facade and pane clips, without altering semantic hit or editing rectangles.

Filtered Keys/Pad add logarithmic cutoff and physical resonance controls to the existing Tone group. Their waveform display caches independently streamed engine samples; it does not inspect worker filter state. See `docs/filtered_synth.md`.

`preset_panel.c` and `preset_panel_input.c` own cached My Presets search, naming/update, loaded-sound feedback and finite audition controls. They reuse shared rounded buttons and UTF-8 editing; the instrument panel and discrete-control adapter provide small routing hooks.

`sound_panel.c` and `sound_panel_input.c` own cached instrument inspection, fixed-scale plots, immutable references and finite comparison auditions. Rendering never synthesizes audio. See [sound inspection](../../docs/sound_inspection.md) for calibration and shortcuts.

`midi_pattern_panel.c` owns the piano-roll Patterns entry and popup, explicit append/replace choice and fresh common-action dispatch. It shares geometry with input and reuses shared rounded button tokens; the actual piano-roll painter retains note rendering.

The sound inspector now supports actual Region captures alongside isolated instrument analysis, explicit dry/processed processing, independent reference samples and exact captured WAV saving. Closing/shutdown releases the private buffers; analysis stays outside painters and project history.

`fx_library_panel.c` owns the effects-panel Chains browser, shared UTF-8 name input, revision-bound Save As/Update, explicit Refresh and guarded recall. `midi_pattern_panel.c` adds whole-region expression shortcuts with matched pointer release and cancellation.

`routing_panel.c` owns the Effects-header Routing form: copied complete guards, staged destinations/physical percentages/roles, return creation, explicit Refresh, shared text editing, matched releases and focus/project teardown. Common routing actions own publication and history; painters perform no engine writes.

Region inspection includes direct captured/reference PCM listening, with matched pointer releases, finite completion feedback, stop/focus/project cancellation and immutable device ownership. Production output emits the exact capture without applying insert processing again.

`automation_status.c` displays selected-point units, exact frames, shape and shortcuts using persistent queued text storage. `automation_view.h` draws curves through the engine evaluator and shares physical-unit mapping with point hit tests.

- `automation_menu.c`: region-aware target dropdown, overlay input ownership and full target labels.

Sampled Acoustic Piano, Viola Section, Double Bass and Violin Section extend the acoustic palette. Piano has finite velocity-selected strikes and voice-specific automation; bowed strings reuse sustain/dynamics playback. Audition notes follow each recorded range. See `docs/sampled_piano_strings.md`.

Nested/collapsible audio bus groups, common history and MCP support are described in [track groups](../../docs/track_groups.md). Engine group projection/mixing and the native group panel have focused signal, persistence, rollback and SDL/Vulkan tests.

[Collection-scoped Load/Save](../../docs/project_collections.md) now isolates folder lists, previews monotonically versioned checkpoints and preserves exact current-project save paths. Native collection and publication tests cover the shared modal geometry and atomic no-replace writer.

[Compact track UI](../../docs/compact_track_ui.md) provides aligned headers/meters/FX access, derived descendant MIDI/audio overviews and fold-only playback-safe selection/history. New `timeline_track_header` and `timeline_group_overview` modules share production geometry and source preview components.

[Header mixer and lane density](../../docs/header_mixer.md) adds guarded gain/pan scalar history, standard relative gestures, exact-value inspector synchronization, smaller gutters and subtle persistent lane tints. The application mixer/history boundary and input gesture owner are separate small modules.

Clipped labels retain a 512-entry dense-arrangement working set. Cache entries distinguish font identity, owning renderer and raster density; font replacement invalidates its entries. Eviction and cache resets retire GPU textures through the existing frame-fence queue, with idle synchronization for out-of-frame invalidation. This prevents later region labels from reusing resources referenced by earlier transport/header draws.
