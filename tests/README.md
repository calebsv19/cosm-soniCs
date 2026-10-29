# Directory: tests

- `make test-track-hat-choke` checks track-local cross-region routing, tail spill, ordering, edits, export boundaries and seek/block/loop parity. `make test-mcp-track-hat-choke` verifies adjacent-region authoring, exact rendered undo/redo and project reopen through the real SDK. The production renderer test is included in `test-stable`.
- `make test-instrument-articulation` checks independent pitch integration, mono selection, hat choke, export boundaries and seek/block consistency. `make test-mcp-articulation` verifies real SDK control, saved sounds and exact undo/redo/reopen audio; the renderer test is in `test-stable`.

- `percussion_render_test.c` (`make test-percussion-render`, included in `test-stable`) checks all five new voices, short-trigger tails, seek/block parity, physical pitch sweep and parameter effects. `make test-mcp-percussion` exercises all five through the real SDK, saves presets and verifies exact rendered undo/redo and project reopen.

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
- `application_instrument_action_test.c`: Exercises common actions with publication/history failure injection, exact undo/redo, gesture ownership, generation/identity targeting and control-thread rejection. Run via `make test-application-instrument-action`; included in `test-stable`.

- `mcp_control_fixture.c` and `mcp_control_test.py`: Run the production bridge and
  real engine in an isolated owner-thread fixture, then initialize the official
  MCP SDK client and verify all seven tools, strict input, guarded writes,
  inheritance, scoped undo/redo, replay/lost responses, UI/project conflicts,
  read-only admission, receipt limits and shutdown. Install the isolated SDK
  environment using [MCP setup](../tools/mcp/README.md), then run
  `make test-mcp-control`. Local Unix-socket binding permission is required;
  this optional lane is separate from Python-free `test-stable`.

Invoke `run-headless-smoke` and `test-legacy` sequentially; their recursive shared
build lanes clean shared intermediate directories and can interfere when run
concurrently as top-level make targets.

- `instrument_controls_test.c`: Checks control mapping/roundtrips, supported DSP
  parameter sets, exact diagnostic/source sample parity and gate-aware ADSR timing.
  Rounded oscillator tests check independent Fourier amplitudes, upper-note
  harmonic limits, and real-sample influence of every advertised control.
  Run `make test-instrument-controls`; included in `test-stable`.
- `mcp_instrument_loop_test.py`: Uses official MCP calls to edit a real C-major
  phrase, export finite non-silent WAVs, and prove exact baseline/edited WAV
  restoration after undo/redo, rounded factory-default selection and history,
  plus a held pad chord. Run `make test-mcp-instrument-loop` after MCP SDK
  setup with local socket permission. Artifacts are retained under the selected
  build's `receipts/instrument-loop-*` directory. No user project or audio device
  is opened and no production render tool is added.

The filtered synth checks add independent frequency-response measurements, preview/source parity, extreme coefficient changes, exact render partitioning in mono/stereo at 44.1/48/96 kHz, reused-note reset and zero render heap calls. The official SDK musical loop includes Filtered Keys/Pad and a held Pad chord. Native captures include their Tone/Env groups. See `docs/filtered_synth.md` for actual-app acceptance and retained evidence.

`make BUILD_DIR=<fresh-root> test-preset-library test-mcp-presets` verifies portable library storage, optimistic updates/backups, library-independent session recall, undo, browser input, finite/repeated auditions and reproducible WAVs through the official SDK and production bridge. Tests use private temporary libraries. Native editor qualification includes browser/save frames and nested clip containment; actual native App_Run acceptance separately exercises save/preview/apply and reopen after fixture-library deletion.

`test-sound-inspection` calibrates silence/sine/clipping, production source parity, fixed windows and reference/audition ownership. `test-mcp-sound-analysis` qualifies both read-only SDK tools, exact preset/instrument/WAV metric parity and filter/attack/drive WAV comparisons in private runtime roots. Native editor qualification adds actual inspection painters and reference controls.

`test-midi-authoring` is part of stable smoke and checks atomic notes/history, invalid/stale/busy/removed/off-thread cases, preparation failures, musical tempo/key behavior, popup controls, exact production-rendered undo/redo and session reopen. Pass an existing private output directory directly to the test executable to retain stems, arrangement and a file-roundtripped project. Native editor qualification also exercises the actual pattern controls and painters.


`test-mcp-midi` verifies the five official-SDK MIDI tools, full-capacity consistent pagination, state/identity/busy/payload conflicts, immutable receipts, token expiry, exact production WAV undo/redo and saved-project reopen. See [MIDI MCP](../docs/midi_mcp.md).

### Guarded MIDI region authoring

`test-midi-regions` covers zero-track creation, extension, identity replay, publication/history failures, neighbor/busy/overflow conflicts and owner-thread refusal. `test-mcp-midi-regions` exercises the official SDK, full mixed undo to empty/redo, read-only admission, deterministic audio and file reopen.

### Effects authoring development

`test-fx-actions`, `test-mcp-fx` and `test-composition` qualify exact chain history/failure boundaries and production processed composition behavior; common C targets are part of the stable lane.

`test-mcp-composition` qualifies immutable processed renders, production PCM parity, overload and occupied-file refusal, receipt replay and both count/byte cache bounds.

Run `make BUILD_DIR=<task-root> test-fx-library` for portable-chain storage, all 54 registered effect types, tempo-aware independent recall, revision/backup and malformed/link/lock refusals. `test-sound-inspection` also checks processed Region captures, immutable references and exact PCM saving. Both participate in focused/sanitizer checks; library MCP/editor adoption is implemented.

Run `test-fx-library-ui test-mcp-fx-library` for native library ownership and official SDK complete settings/history/project independence. Run `test-midi-expression test-midi-authoring test-mcp-midi` for tempo-aware swing, deterministic variation, velocity scaling, atomic refusal and native/SDK adoption. The native editor harness paints chain library/save and expressive piano-roll states.

Session serialization now checks exact adjacent-float and tiny MIDI velocity persistence; native expression/library App_Run acceptance verifies bit-identical processed WAVs after real save/reopen.

Guarded arrangement is documented in [arrangement](../docs/arrangement.md). The common action, MCP adapter and native MIDI duplicate/delete share exact content guards and one-step batch history; native Arrange fields and actual-window acceptance now pass; advanced routing remains pending.

`make test-fx-return-alignment` qualifies the source/return/master compensation foundation with independent impulse DSP, dry/wet phase, stereo partition invariance, compatible history transfer, resets and zero render allocations. It does not yet qualify application send/bus routing adoption.

`make test-engine-routing` is included in the stable suite. It uses production bounce/render plans and independent expected signal factors to verify two-source shared returns, two independent returns, dry/source/return isolation, sparse sends, solo/mute/fader behavior and actual delayed-signal PDC. It rejects stale/invalid topology, protected deletion, MIDI insertion and instrument audition on returns. Publication and prepared-allocation failures preserve exact authored routing; prepared rendering adds no instrumented heap calls. Normal and ASan checks qualify the engine boundary, not project routing storage or native/MCP bus controls.

Routing persistence extends session serialization/transaction tests with fixed-slot parsing, legacy defaults, exact send gains, fresh destination binding and bit-identical processed audio after disk save/reopen. Routed candidate failure and seven allocation boundaries preserve the live project. Actual App_Run acceptance also covers existing MCP return-chain edit/undo and saved routing/audio restoration; it does not establish route-creation tools.

`make test-routing-actions` qualifies common complete routing edits and scoped history, including foreign-thread/busy/stale/no-op refusals, history reservation and publication failure, preservation of unrelated mixer settings and refusal to skip unrelated history. A musical source verifies the expected processed return sum and exact production audio restoration through undo/redo. The target is included in the stable suite.

`test-routing-actions` now also qualifies empty-project return creation, stable-ID redo, one publication/history, capacity and creation/undo/redo rollback, and later mixer/effect protection. `test-mcp-routing` verifies 45-tool official SDK discovery, empty return creation, guarded sends/history, independent wet/dry ratios, exact disk/audio reopening, token eviction, immutable receipts, malformed graph refusal and read-only admission.

`make test-routing-ui` qualifies native send/return authoring, empty-project creation, staged shared text, graph/stale/busy refusal and compact/focus cancellation. `build-native-editor-controls-proof` includes actual Effects-header opening, creation and percentage application plus validation-clean routing frames.

Instrument checks now include independent filter motion, all eighteen preset render/preview parity, explicit MCP modulation history, complete eighteen-parameter user presets and exact project storage. Serialization links the production catalog to avoid metadata drift in engine stubs.

`test-audio-preview` checks immutable PCM copying, exact frame partitions, final padding/completion, refusal preservation and callback lock exclusion. `test-audio-output-device` checks captured samples through the actual SDL trampoline. `test-sound-inspection` exercises native current/reference listening with a paused private dummy endpoint and verifies transport/close cancellation.

`make test-midi-note-editing` verifies real matched-release velocity buttons, selected-only changes, saturation, keyboard parity, undo/redo, grab-offset movement, Alt/Option snap bypass, returning a group to its baseline, edge hover targets, and endpoint-preserving left resize. The MIDI shell suite also checks movement and right resize on the first press.

- `make test-automation-model`: legacy/physical units, curves, point collisions, stable undo, session roundtrip, and production gain/balance rendering.
- `make test-mcp-automation`: official SDK automation reads/writes, exact replay, stale state, shared history, paging, audio and MIDI targets, read-only admission, and save/reopen sound parity.

`test-sampled-violin` covers actual multiroot assets, long held notes, releases, recorded timbre and block-independent expression. `test-mcp-sampled-violin` covers SDK discovery, guarded changes, presets, dynamics lanes and project recall.

Violin stabilization additionally checks zero-time protected onset/release, early note-off continuity and live retirement of the four-ms protective release; instrument lifecycle checks cover prepared retriggers and bounded voices.

test-sampled-winds and test-mcp-sampled-winds cover actual trumpet/clarinet banks, all supported pitches/rates, three-layer continuity, long holds, gates, Dynamics, presets and project recall. The stabilized violin demo is byte-regressed separately.


`test-sampled-winds` also verifies a smooth clarinet middle-layer timbre slope and exact protected release endpoints across both instruments’ complete pitch ranges.


`test-sampled-saxophone` covers all three wind banks; `test-mcp-sampled-saxophone` checks the appended saxophone voice, actual capabilities, Dynamics, presets, previews and project recall.


The wind sampler test additionally bounds sax midpoint blend energy over every supported pitch and held-note windows, catching cancellation and correlated-layer gain bulges.

`test-sampled-flute-cello` extends the acoustic harness to both new voices; `test-mcp-sampled-flute-cello` checks metadata, voice-aware automation and preset/project round trips. Packaging validates 44 additional original CC0 recordings.

Flute/cello checks now tighten sustained blend RMS bounds, exercise long-loop callback partition invariance with overlapping/retriggered notes, and require release tails to reach silence. The package validator checks generated energy curves, covariance bounds and matching wrap endpoints.

Sampled Oboe and French Horn extend the existing acoustic sampler with paired recorded dynamics and held-note expression. Complete audition chords follow each bank's playable range. See `docs/sampled_oboe_horn.md` for ranges, provenance, preparation and verification.

Oboe/French horn stabilization refines five qualified common loops and regenerates their bounded layer-energy curves; tighter all-pitch blend and note-transition checks cover the existing sampler. See `docs/sampled_oboe_horn.md`.

Sampled Bassoon and Tenor Trombone add two paired-layer recorded sustain banks through the existing sampler, supported controls and guarded preset/automation tools. See `docs/sampled_bassoon_trombone.md` for ranges, source provenance and verification.

Sampled Acoustic Piano, Viola Section, Double Bass and Violin Section extend the acoustic palette. Piano has finite velocity-selected strikes and voice-specific automation; bowed strings reuse sustain/dynamics playback. Audition notes follow each recorded range. See `docs/sampled_piano_strings.md`.

- `timeline_navigation_test.c` (`make test-timeline-navigation`) checks 32-track reachability across row heights/window sizes, wheel axes/direction/precision, anchored zoom, height-button release, scrollbar bounds/capture, selection visibility and track-count shrink. An optional capture prefix produces three validation-clean native timeline frames.

Nested/collapsible audio bus groups, common history and MCP support are described in [track groups](../../docs/track_groups.md). Engine group projection/mixing and the native group panel have focused signal, persistence, rollback and SDL/Vulkan tests.

[Collection-scoped Load/Save](../../docs/project_collections.md) now isolates folder lists, previews monotonically versioned checkpoints and preserves exact current-project save paths. Native collection and publication tests cover the shared modal geometry and atomic no-replace writer.

[Compact track UI](../../docs/compact_track_ui.md) provides aligned headers/meters/FX access, derived descendant MIDI/audio overviews and fold-only playback-safe selection/history. New `timeline_track_header` and `timeline_group_overview` modules share production geometry and source preview components.

[Header mixer and lane density](../../docs/header_mixer.md) adds guarded gain/pan scalar history, standard relative gestures, exact-value inspector synchronization, smaller gutters and subtle persistent lane tints. The application mixer/history boundary and input gesture owner are separate small modules.

`make test-mcp-track-controls` qualifies SDK discovery, sampled violin/clarinet under nested buses, base mixer factors/stereo balance, names/IDs and exact audio Save/Load, native/MCP history interleavings, stale values/lifetimes, token expiry, receipts and read-only refusal through task-owned fixtures.

`test-eq-meter` calibrates known sine amplitude, mono/stereo asymmetry, silence and exact full-scale/overload thresholds; verifies Unicode decoding and independent production EQ DSP/history parity. `test-mcp-eq` qualifies private SDK native/MCP EQ interleavings, target scope/receipts, sampled voices, captured channel numbers and raw/escaped saved-session/audio parity.

- `make test-text-cache-lifetime` (included in `test-stable`) exercises the production clipped-text path with real TTF rasterization and a safe descriptor-lifetime model: 648 distinct labels, repeated dense playback frames, font address reuse, renderer ownership, Retina density and in-frame reset. It submits no invalid GPU work.
- `make build-dense-text-native`, followed by the resulting `dense_text_native_test <capture-prefix>`, renders repeated production panes with 15 tracks and 240 named MIDI regions in a hidden native Vulkan window. A separate 640-label eviction case compares unchanged control pixels and requires clean Vulkan validation. On the local Mac, add `/opt/homebrew/lib` to `DYLD_LIBRARY_PATH` for the installed validation layer. The fixture uses a synthetic engine and dummy audio, never the active project or app.

`make test-mix-capture` (stable lane) checks calibrated in-context child/group/return/master levels, time buckets, latency, exact production parity, mute/solo and state preservation with private sine/sampled fixtures. `make test-mcp-mix-capture` checks ordinary read-only SDK capture and immutable paging; see [schema and boundaries](../docs/mix_diagnostics.md).

Mix diagnostic insert pages now expose per-insert input/output levels and actual
per-channel Compressor/Limiter attenuation with output-latency alignment; see
`docs/mix_diagnostics.md` for paging, calibration and remaining measurement limits.

Immutable mix capture schema v3 adds per-channel spectral windows for processed
child/group/return/master outputs. `get_mix_capture` independently pages
`frequency_window` and `frequency_channel`; see `docs/mix_diagnostics.md` for
calibration, window coverage and remaining loudness/true-peak limits.

Immutable mix captures now include prepared note-ending controls/fences and
threshold-qualified aggregate MIDI source tails. `get_mix_capture` independently
pages eight note rows with `note_offset`; see `docs/mix_diagnostics.md` section 7.

## Contained release fixtures

C fixture paths use `test_temp_path.h` to honor the caller's `TMPDIR`; ordinary local runs retain `/tmp` as their fallback. Release preparation supplies an attempt-owned temporary root. Assertions, signal cases and synthetic audio inputs are unchanged. Relative `tmp/` fixtures run in the producer's owned working copy, never the frozen input tree.
