# Transactional MIDI patterns

Current behavior: see [agent music capabilities](agent_music_capabilities.md). Tool counts, initial scope and later-slice forecasts below describe their dated implementation checkpoint; region creation, expression, arrangement, effects, routing and percussion have since been adopted.

The first MIDI authoring slice is implemented in persistent DAW Main Edit. Copied region snapshots, validated append/replace actions, tempo-aware major-key patterns and the editor's Patterns popup share one application workflow. The MCP server now adopts this workflow through five MIDI tools (nineteen total); see [MIDI MCP control](midi_mcp.md).

## 1. Editor workflow

Select a MIDI region and open its piano-roll editor. **Patterns** appears at the lower right. The popup provides **Melody**, **I - vi - IV - V**, and **Arpeggio** in C major, rooted at C4 with velocity 0.65. Each phrase spans four quarter-note beats, independent of the current time signature. Melody and arpeggio contain eight eighth-note onsets; the chord progression contains four triads. Gates occupy 85 percent of each step, allowing the instrument's release to shape the sound.

The default **Append after existing notes** places the phrase on the next region-relative whole-quarter-beat boundary after the latest note end, or at zero for an empty region. **Replace all notes from region start** is an explicit choice. A completed pattern is one undo step; repeating an identical replacement adds no history. Append never removes existing notes. Region length stays fixed, and insufficient space is reported without a partial phrase. Escape or Close dismisses the popup. Enlarge very small panes to enable it.

Work while transport is stopped, recording/Test are off and no note gesture, marquee or instrument edit is active. The popup remains bound to the selected region's identity and closes if selection/project ownership changes. Factory-browser input remains with its existing owner. Generic existing mouse/clipboard editing retains its current implementation; this slice adds the shared pattern transaction rather than migrating all legacy gestures.

## 2. Common application contract

`include/daw/midi_actions.h` exposes these owners:

| API | Contract |
| --- | --- |
| `daw_action_get_midi` | Owner-thread copy of the generation/track/region target, timeline start, duration, source offset, full ordered notes and exact tempo events |
| `daw_midi_snapshot_free` | Releases owned copied notes/events and resets the snapshot |
| `daw_action_edit_midi` | Exact-state append or complete replacement; one atomic note publication and one pre-reserved history command |
| `daw_midi_history_restore` | Replay guard for common-action history; validates identity, bounds and the opposing note list before restoration |

Initialize snapshots to zero and free them before reusing the output. Reads own independent storage and never expose engine pointers. Notes use the existing region-relative frame model: pitches 0–127, finite velocities 0–1, positive durations and checked containment within the region. A zero-count replacement explicitly clears notes. Same-pitch events at the same start and duration are rejected as duplicates, including differing velocities. The existing engine limit is 4,096 notes; copied tempo maps are bounded to 1,024 valid events. The action sorts a private candidate through the existing MIDI list helper, validates the whole result and recognizes no-ops before history reservation.

Expected state compares exact semantic note values, region start/duration/offset and all tempo event beat/BPM values plus sample rate. Concurrent notes, trims, moves or tempo changes require a fresh snapshot. Instrument, mixer and effect settings remain outside this notes-only precondition and transaction. Handles include the engine/project generation and stable track/clip IDs; changed array positions do not redirect the edit, and removed/replaced project objects reject it. Off-thread calls reject before mutable project inspection.

The existing engine note-list replacement prepares the render revision privately and restores old notes on rejection. The existing undo reservation prepares final storage and independent before/after note copies before publication; completion transfers that reservation without a second history allocation. Failed preparation/publication retains original notes/history and cancels only the transaction's reservation. New common-action history refuses stale replay without consuming its command. Legacy history entries preserve their prior path.

## 3. Musical pattern generation

`daw_midi_pattern_build` is a pure bounded generator over a copied snapshot. Its API accepts a major-key tonic MIDI pitch, finite nonnegative region-relative start beat and positive normalized velocity. Every onset and gate end converts separately through the captured project tempo map, including changes within a phrase. Timeline frames are checked before conversion/rounding; pitch overflow or a phrase end beyond the region rejects the entire candidate.

The patterns use scale intervals from the tonic: melody 0/4/7/9/7/4/2/0, arpeggio 0/4/7/12/7/4/2/7, and I/vi/IV/V triads. The UI intentionally offers C major as a small initial workflow; the application generator can transpose to other major tonics. Four quarter-note beats do not imply a bar in other signatures. Initially the generator rejects regions with nonzero source offsets so trimmed-source timing is not guessed; generic frame-note actions still follow the existing engine note model. Region/track creation, automatic extension, random composition, scale metadata and minor modes are later slices.

## 4. Verification and listening

`make BUILD_DIR=<fresh-root> test-midi-authoring` covers one publication/history step, complete undo/redo, no-op replacement, append/clear, exact stale-state refusal, wrong-thread/removed/project identities, rejected history/copy/engine preparation, nonfinite/overflow/duplicate events, active editor/recording ownership, tempo changes, major-key pitches and the editor popup. It also compares production-rendered float samples exactly after undo/redo and project reopen. The test is part of the stable headless lane.

An optional output directory passed directly to the test executable retains a four-second production WAV arrangement, separate pad/chord, keys/melody and pluck/arpeggio stems, and `c-major-project.json`. The example uses 100 BPM and complete project-owned sound settings. Actual file write/read/reopen is qualified in that output run; normal stable runs exercise in-memory session capture/reopen without leaving files.

Native editor qualification uses actual Vulkan product painters and pattern controls, tests complete phrase undo/redo, and checks validation plus enclosing clip containment. Selected frames, audio/project and receipts are retained under `build/receipts/midi-authoring-20261009/`. The qualified source executable is `build/midi-authoring-20261009/targets/macOS-arm64/toolchains/clang/bin/daw_app`; disposable compiler/test/sanitizer/BMP output is retired after consumers finish. Physical listening preference and sustained audio-device performance remain separate acceptance.

## 5. Adoption boundary

MIDI adoption now provides bounded reads with exact retained state tokens and guarded note/pattern/history writes through these owners. It reuses bridge payload limits and immutable request receipts, with independent readback/render/reopen proof. See [the MIDI MCP contract](midi_mcp.md). No client registration is changed.

The DAW's existing tempo-map conversions, MIDI list/publication engine and undo reservation are reused, together with shared kit_ui button tokens. `core_time` provides monotonic timing rather than musical tempo semantics; `core_math`/`core_data` do not own DAW note scheduling/history policy. No shared implementation, dependency minimum, adoption edge or session schema changes. Atlas was considered: these supporting modules remain within the existing MIDI editing workflow, without a new top-level plan or shared topology change.

This additive feature merits a minor-release review when release work is authorized. VERSION remains 0.4.0; no commit, release, installed-app replacement or global MCP configuration is part of this development slice.

## Pointer note editing

Click and drag a note body to move it on the first press. Drag a highlighted left or right handle to change that endpoint while keeping the other endpoint fixed. The middle remains a move target on short notes, and visible note bodies take priority over nearby hit slop. Shift-click toggles selection; Shift-drag adjusts selected velocity; Shift-drag on empty space selects a group. Group body drags preserve spacing and pitch intervals.

Timing follows the current snap/grid setting. Hold **Alt/Option during a pointer gesture** for continuous placement between grid lines, then release it to resume snapping. This does not change the global snap setting; pitch remains in semitones. **Q** explicitly quantizes selected notes, and **Q-/Q+** change the editing grid.

**Vel-/Vel+**, or keyboard **minus/plus**, change selected-note velocities by five percentage points in one undo step. Values clamp at 0–100%; unselected notes are untouched. With no selection, these controls change the velocity used for new notes. The header identifies selected velocity bounds separately from the new-note velocity.

Piano expression extension: [piano_expression.md](piano_expression.md) describes the three piano-only controls, binary region/live sustain, 88 string modes, recorded key/pedal mechanics, 32-field v6 presets and session v25. Older preset versions 1–5 and session v24 remain readable; existing non-piano voices keep their behavior.
