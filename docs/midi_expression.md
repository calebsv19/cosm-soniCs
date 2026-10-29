# MIDI expression

Current behavior: see [agent music capabilities](agent_music_capabilities.md). Tool counts, initial scope and later-slice forecasts below describe their dated implementation checkpoint; region creation, expression, arrangement, effects, routing and percussion have since been adopted.

## 1. Shared editing behavior

`daw_action_transform_midi` transforms all notes in one untrimmed MIDI region against a complete copied state. It reuses the common MIDI owner, busy checks, full note/bounds/tempo precondition, history reservation and atomic publication. It never resizes the region or changes note durations. A boundary crossing or duplicate resulting pitch/start/duration refuses the entire edit. No-ops create no history. Velocity outputs explicitly saturate to 0–1.

Quantize uses quarter-note beat units and the captured complete tempo map. Grid positions are anchored at region start. `grid_beats` accepts 1/32 through 4 quarter notes; a value of .5 means eighth notes. Swing delays odd subdivisions by `swing * grid_beats`, with swing 0–.75. Each onset moves toward its nearest swung position by `strength` 0–1; ties choose the earlier point. Note durations remain frame lengths, including across tempo changes.

Velocity scaling multiplies each captured velocity by `velocity_scale` 0–4. Humanize adds deterministic bounded timing variation of up to `timing_ms` 0–100 and velocity variation of up to `velocity_amount` 0–.5, using an explicit uint32 seed. Seed zero selects a fixed documented implementation seed, not global randomness. A second application varies the then-current notes; it is not an idempotent musical transformation. Retrying the same accepted MCP request returns its original receipt. Timing transforms reject timelines beyond exact 53-bit floating-point frame conversion; velocity-only transforms preserve original integer onsets exactly.

## 2. Native workflow

Open **Patterns** in the MIDI Notes editor. **Quantize eighths** applies full-strength straight eighths, **Swing eighths** applies a one-third delay on alternate eighths, and **Vary velocity** applies .08 velocity variation with seed 123 and zero timing variation. These shortcuts affect the whole region independently of the append/replace pattern choice. Each actual edit is one undo step. The popup requires enough pane space to display its controls. Pointer actions wait for matching release; focus/geometry changes cancel a pending press.

## 3. MCP workflow

The local server exposes 36 tools. Read `get_midi_region` and call `transform_midi` with target, endpoint instance, a fresh request ID and exact `expected_state`.

| operation | Required settings |
| --- | --- |
| `quantize` | `grid_beats`, `strength`, `swing` |
| `velocity` | `velocity_scale` |
| `humanize` | `timing_ms`, `velocity_amount`, `seed` |

Omit unrelated settings, even when zero. The result contains accepted notes and a new/reused exact state token; `undo_midi`/`redo_midi` own its history. All 4,096 bounded region notes can be transformed in one operation without sending a replacement payload. Existing 16 KiB/128 KiB transport limits, immutable request receipts, state-token lifetime and read-only admission remain in force.

## 4. Verification and scope

`test-midi-expression` checks independent tempo-change/swing expectations, fixed lengths, exact undo/redo, no-op strength, bounded repeatable variation, velocity-only shaping, stale/busy/invalid refusal, quantize collisions and timing boundary preservation. `test-midi-authoring` exercises native expression press/release and focus cancellation through the real popup owner. `test-mcp-midi` qualifies official discovery/schema, expression edits/receipt replay, stale/busy/irrelevant-field refusal, deterministic variation and common undo; it also retains existing note/page/production-render/reopen checks. The native Vulkan harness paints swung and varied notes with clip and validation checks.

This extends expression. Region duplication/movement/trim/delete, compound arrangement edits, arbitrary automation and sends/buses remain separate work in the active roadmap. No session schema, shared dependency, VERSION, installed app, global MCP registration or release change is implied.

Actual native App_Run qualification also performs velocity variation and history, effects-chain Save As/recall/history, composition analysis/WAV publication, normal SDL shutdown/save and real reopening after deleting the private chain document. Notes, effect settings, captured metrics/plots and WAV bytes match after restart. This exposed the session writer's previous six-decimal float rounding; it now writes `FLT_DECIMAL_DIG` significant digits without collapsing small values. Focused serialization tests preserve adjacent float velocities and values below 1e-6 exactly. Session schema and reader compatibility remain unchanged.

Piano expression extension: [piano_expression.md](piano_expression.md) describes the three piano-only controls, binary region/live sustain, 88 string modes, recorded key/pedal mechanics, 32-field v6 presets and session v25. Older preset versions 1–5 and session v24 remain readable; existing non-piano voices keep their behavior.
