# Directory: tests

Purpose: Lightweight smoke/integration checks for DAW subsystems. Coverage now
spans session persistence, media cache behavior, MIDI regions/instruments,
audio capture, audio recording, package contracts, layout, tracing, and shared
adapter lanes, plus parser diagnostics.

## Files
- `test_assert.h`: Shared fatal assertion helpers for DAW tests that want
  consistent `test_name: message` failure output without repeating local
  `fail`/`expect` boilerplate.
- `test_midi_editor_harness.h`: Shared MIDI editor shell test harness for
  `AppState` setup/teardown and SDL mouse/key/wheel dispatch helpers used by
  scenario tests.
- `test_session_engine_stubs.c` / `test_session_engine_stubs.h`: Link-only
  engine/UI stubs for session serialization tests and the default-off
  memory-check audit, keeping parser/persistence assertions separate from the
  fake runtime surface.
- `session_serialization_test.c`: Builds a minimal `SessionDocument`, runs validation, writes it to `build/tests/sample_session.json`, reloads it via the JSON parser, and verifies an audio+MIDI clip round-trip including the MIDI instrument preset and parameters. Run via `make test-session`.
- `media_cache_stress_test.c`: Repeatedly acquires/releases cached clips across duplicate and mixed sample-rate scenarios to shake out refcount bugs. Run via `make test-cache`.
- `clip_overlap_priority_test.c`: Ensures clips with identical start frames retain deterministic ordering after timeline edits. Run via `make test-overlap`.
- `midi_model_test.c`: Verifies model-only MIDI note ordering, MIDI clip creation, full note-list replacement, and audio clip kind defaults. Run via `make test-midi-model`.
- `midi_instrument_render_test.c`: Bounces a MIDI clip through the built-in instrument source, verifies silence outside the note plus non-zero audio during the note, checks preset render differences and parameter clamping/output effect, verifies live audition renders without adding clip notes, moving transport while stopped, or leaking saved region notes into stopped Test audition, and proves a MIDI-rendered bounce can be written under the library root then inserted as a normal audio clip on a new bounce track. Run via `make test-midi-instrument-render`.
- `timeline_midi_region_test.c`: Verifies timeline `+ MIDI` region creation, selection defaults, undo/redo reconstruction, MIDI region resize bounds around note content, and preview X-position stability when the right edge extends. Run via `make test-timeline-midi-region`.
- `midi_editor_shell_test.c`: Verifies selected MIDI clips route to the bottom-pane editor, compute non-overlapping compact piano-roll geometry/header controls, time-ruler, editor viewport, and compact instrument-panel knob layout with usable preview space, capture lower-pane input, support time-ruler playhead seek without note creation, route hover-local MIDI editor zoom/pan without mutating the arrangement timeline viewport, support create/delete/drag note edits with undo, require selected-first note body move and edge resize, support selected-group click-drag movement with click-release collapse, support shift-drag velocity edits with undo, support Shift-click multi-selection, Shift-empty-grid marquee preview/commit, selected-set quantize/delete, selected-note copy/paste/duplicate with undo, selected-group velocity drag, note hover/click slop and hover cleanup after delete, record timed QWERTY key pairs into notes with default velocity/octave controls, quantize selected notes with undo, apply the selected quantize grid to snap-enabled create and QWERTY record timing, run Test-mode QWERTY audition without recording, select per-region instrument presets from the MIDI editor header dropdown without mutating notes, switch to the instrument subview from the editor header, and edit per-region instrument params from that subview without mutating notes. Run via `make test-midi-editor-shell`.
- `audio_capture_device_test.c`: Verifies the DAW-local SDL capture wrapper
  opens, queues, drains, pauses, resumes, and closes against its fake backend
  without requiring live hardware. Run via `make test-audio-capture-device`.
- `audio_recording_test.c`: Verifies the recording coordinator can arm,
  transport-gate captured input, preview the active waveform, finalize a WAV
  recording, insert it as an undoable selected-track audio clip, preserve it
  through session capture, and keep empty solo record-target routing audible.
  Run via `make test-audio-recording`.
- `config_diagnostics_test.c`: Verifies invalid `engine.cfg` entries and loop
  policy environment values emit bounded parser diagnostics while preserving
  existing default, clamp, and partial-parse behavior. Run via
  `make test-config-diagnostics`.
- `engine_smoke_test.c`: Headless engine exercise that loads a clip, applies fades, toggles loop/playback, and verifies transport state transitions. Run via `make test-smoke`.
