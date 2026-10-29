# Instrument controls and exercised creative loop

Current behavior: see [agent music capabilities](agent_music_capabilities.md). Tool counts, initial scope and later-slice forecasts below describe their dated implementation checkpoint; region creation, expression, arrangement, effects, routing and percussion have since been adopted.

## 1. Implementation slice

1. Identify supported controls from the DSP dependency of each preset.
2. Improve physical-time knob precision without changing saved values or automation.
3. Plot the actual isolated voice and gate-aware ADSR through shared engine evaluation.
4. Exercise existing MCP edits against a real rendered C-major phrase, including exact undo/redo.

The initial MCP/action baseline remains the owner of edits and history. This slice
adds instrument capability metadata and UI precision; it does not add a second
synthesizer or a new remote rendering surface.

## 2. Supported controls

Every preset supports Level, Tone and Attack/Decay/Sustain/Release. Additional
controls follow the code that actually generates that preset's voice:

| Preset | Additional controls | Total |
| --- | --- | --- |
| Pure Sine, Soft Square, Saw Lead, Simple Bass | None | 6 each |
| Synth Lab / Custom Synth | Osc Mix, Detune, Sub, Drive, Vibrato Rate/Depth | 12 |
| Soft Pad | Osc Mix, Detune, Sub | 9 |
| Pluck | Osc Mix, Drive | 8 |
| Bright Lead | Drive | 7 |
| Warm Keys | Sub | 7 |
| Sub Drone | Sub, Drive | 8 |
| Rounded Keys, Rounded Pluck, Rounded Pad | Osc Mix, Detune, Sub, Drive | 10 each |

Empty groups are omitted; a selected group that disappears after a preset change
falls back to Output. Envelope controls follow ADSR order using the original
stable parameter IDs. Existing dormant stored values remain intact for compatibility.
Common actions still accept them, but they have no effect in presets that do not
consume them. Supported controls can also have conditional influence: detune
needs a nonzero second-oscillator mix, and vibrato rate needs nonzero depth.

## 3. Control mapping and compatibility

Attack, Decay and Release use `log1p(milliseconds) / log1p(maximum)` for knob
position. The inverse uses `expm1(position * log1p(maximum))`. Zero and the maximum
are exact reachable endpoints; short times occupy useful pointer travel. Shift
held at pointer-down latches one-tenth drag sensitivity until release. Releasing
Shift mid-gesture does not jump the value. Times below 10 ms display one decimal;
frequency and detune values display Hz and cents (`ct`).

Physical ranges remain Attack 0–250 ms, Decay 0–800 ms, Release 0–500 ms.
Preset defaults, session schema, serialized units, stable IDs and linear
normalized automation deltas are unchanged. This avoids silently changing saved
music. The additive rounded oscillator family is described in [rounded synthesizer](rounded_synth.md).
Larger ranges, filters and modulation semantics still require separate
compatibility and listening decisions.

## 4. Diagnostic plots

The previous illustrative wave and synthetic envelope formulas are removed.
The source renderer and diagnostic note evaluator share the same voice kernel;
ADSR diagnostics call the same envelope implementation as the source.

- ADSR uses a 500 ms gate, the current physical attack/decay/sustain/release,
  and 50 ms of space after release. A vertical guide marks note-off. Early
  note-off releases from the actual envelope level, including during decay.
- The oscillator plot samples a held C4 at velocity 1 over 20 ms after
  attack+decay+50 ms, using the current effective preset/parameters and sample
  rate. Its fixed vertical range is +/-1; level changes are not auto-normalized.
- Each plot uses at most 512 points. Evaluation is allocation-free and does not
  read mutable audio-worker storage or change transport, notes or project state.

These are isolated voice diagnostics. They exclude note polyphony, track/master
gain, automation, effects and physical output. Sparse display points are not a
full-band spectral analysis or a live output-meter capture. The different plots
have explicitly different time windows; the held-note plot is not the 500 ms
gate experiment. Existing vibrato behavior is preserved, not requalified as a
physically bounded pitch-modulation design by this change.

## 5. MCP and musical exercise

Existing `capabilities` returns the preset-specific `visible_parameters` and a
new additive `control_scale` parameter field (`linear` or `log1p_ms`). Write values
remain physical units; agents must not send knob positions as milliseconds.
Tool names, requests and target/history/receipt guarantees are unchanged.

`test-mcp-instrument-loop` uses the official MCP client and production bridge
with the real engine in an isolated owner-thread fixture. The fixture authors
eight 180 ms note gates on a 250 ms grid at 48 kHz, playing
C4–E4–G4–C5–A4–G4–E4–C4 with alternating velocities. It exports 2.5 seconds per variation:

1. Inherited sine baseline.
2. MCP-authored pluck with a short attack, 180 ms decay and 80 ms release.
3. Undo restores both inheritance and byte-identical baseline float WAV output.
4. Redo restores byte-identical pluck output.
5. A quieter Synth Lab oscillator/sub/detune/vibrato patch produces distinct audio.
6. Each rounded preset selects its advertised factory defaults, renders distinctly,
   and survives exact undo/redo readback and WAV restoration.
7. Rounded Pad also renders a held four-note C-major chord.

All renders use no effects and disable peak normalization. Each run writes a
new task-owned artifact directory, a parameter/capability/measurement report,
float WAVs for exact comparisons and PCM16 `*-listen.wav` files for listening.
The fixture's render command is test-only stdin control, not a new MCP tool.
The tests establish finite, non-silent, unclipped output and exact history
restoration. Musical satisfaction remains a listening judgment.

## 6. Verification and reuse

`test-instrument-controls` covers mappings and endpoints, preset dependency sets,
exact single-voice preview/source parity across all presets and three rates,
and real early-release timing. `test-midi-scheduling` retains exact comparison
against the pre-refactor polyphonic renderer, including automation, sample-rate,
channel and render-partition cases. The shared voice kernel retains accumulation
inside the original arithmetic expression to preserve floating-point contraction.
MIDI input tests cover hidden groups, ADSR order, latched fine drag and undo.
The native Vulkan harness captures the actual instrument painters with validation.

Reuse decisions: existing engine voice/envelope evaluation, action/history,
main-thread control/wake, `core_theme`, `core_font` and `kit_ui` are reuse-adopted.
`core_time`, `core_queue`, `core_sched`, `core_jobs`, `core_workers`, `core_kernel`,
`core_data`, `core_pack`, `core_memdb`, `core_scene`, `core_space`, `core_trace`,
`core_math` and `kit_viz` need no new adoption or extension here. DAW-specific
control mapping and isolated-voice policy stay app-owned; direct bounded plot
segments retain the existing instrument-painter pattern. No shared API, version,
subtree pin or compatibility/adoption metadata is changed.

## 7. Retained acceptance evidence (2026-10-08)

The normal build and complete `run-headless-smoke` stable lane passed in
`build/synth-controls-20261008`. Focused controls, MIDI editor, scheduling and
instrument lifecycle tests also passed with AddressSanitizer, as did both
official-client MCP exercises against the sanitizer fixture. The native Vulkan
fixture passed with validation enabled; `receipts/selected-instrument-*.png`
show explicit Env/Osc selections using actual product painters.

`build/synth-controls-20261008/receipts/acceptance.json` records source and
artifact hashes, commands and retained logs. The musical run is under
`receipts/instrument-loop-ftqjb4b_/`: all five renders contain 120,000 stereo
frames at 48 kHz, with finite, non-silent output below clipping. Undo and redo
restore the corresponding float WAV bytes exactly. PCM16 `*-listen.wav` files
provide the same phrase for subjective comparison.

This is source/build, isolated MCP/engine and native renderer evidence. It does
not establish physical-device listening, human pointer acceptance, a refreshed
installed application, or autonomous remote MIDI authoring/rendering. The
fixture owns the phrase and exports; MCP owns the tested instrument edits.

## Relative knob adjustment

Pressing effects or instrument knobs preserves the current value. Drag upward to increase and downward to decrease; 1000 pixels covers the normalized range, independent of knob size; dB knobs instead use a consistent 0.04 dB per pixel. Hold Shift before pressing for tenfold finer adjustment. Release and drag again to continue from the new value. Gain readouts show three decimal places in dB. Track snapshot gain and pan use the same vertical gesture. Existing parameter limits and beat-sync quantization still apply.

The MIDI editor claims sampled timeline presses over its surface; hidden track clips and headers cannot receive those clicks. Pane dividers claim the event press and retain it through release, using a 10-pixel border hit band.

Piano expression extension: [piano_expression.md](piano_expression.md) describes the three piano-only controls, binary region/live sustain, 88 string modes, recorded key/pedal mechanics, 32-field v6 presets and session v25. Older preset versions 1–5 and session v24 remain readable; existing non-piano voices keep their behavior.
