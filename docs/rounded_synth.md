# Rounded synthesizer family

## 1. Focused implementation

1. Add one rounded oscillator voice with sine-to-triangle brightness.
2. Establish three editable starting presets in the existing browser.
3. Exercise selection, real rendering and history through existing MCP tools.
4. Retain software, native frame and listening artifacts separately.

This slice adds an oscillator family. Legacy vibrato remains unchanged: replacing
its time-dependent frequency-times-time phase formula requires a separate saved
sound/automation compatibility decision. No filter or new modulation behavior is
introduced here.

## 2. Oscillator and controls

Rounded Keys, Rounded Pluck and Rounded Pad share one DSP branch:

- Tone blends a sine into a triangle with aligned positive zero crossings.
  It controls harmonic shape, not filter cutoff in Hz.
- Osc Mix blends a second copy at Detune cents relative to the main oscillator.
  At zero detune both copies coincide; detune becomes audible when Osc Mix is
  above zero, and beating is strongest when both oscillators contribute.
- Sub adds a sine one octave below, at half the sub parameter's gain.
- Drive crossfades continuously from the dry blend to normalized `tanh(3*x)`
  saturation. Zero drive is an exact dry bypass. The oscillator stage scales
  its result by 0.18 before the existing Level, velocity and ADSR stages.
- Level and physical Attack/Decay/Sustain/Release reuse the existing controls.
  These ten parameters are advertised; dormant vibrato controls remain hidden.

The triangle reuses the bounded harmonic helper: only odd harmonics below
Nyquist, up to harmonic 63, contribute. At Tone 1 and Drive 0 its third harmonic
has -1/9 the fundamental amplitude and fifth has +1/25. At Tone 0, Drive 0 and
Sub 0, a single oscillator is a pure sine. Saturation can generate higher
harmonics; the final driven signal is not claimed to be alias-free. There is no
oversampling, resonant filter, live spectral analyzer, or modulation matrix.
The renderer remains allocation-free with fixed bounded oscillator work.

## 3. Starting presets

| Preset / browser category | Intended starting role | ADSR (ms / sustain) | Tone / mix / detune / sub / drive | Level |
| --- | --- | --- | --- | --- |
| Rounded Keys / Keys | Defined onset with a decaying body and short tail | 6 / 260 / 0.32 / 180 | 0.32 / 0.18 / +3 ct / 0.06 / 0.08 | 0.85 |
| Rounded Pluck / Pluck | Short melodic or arpeggiated parts | 2 / 150 / 0.08 / 90 | 0.80 / 0.08 / -2 ct / 0 / 0.12 | 0.90 |
| Rounded Pad / Pads | Held chords with a gradual onset and release | 180 / 420 / 0.80 / 450 | 0.45 / 0.45 / +6 ct / 0.12 / 0.04 | 0.70 |

Select one from the existing instrument preset browser. Use Tone to explore
shape, Osc Mix and Detune for beating, and Drive for additional harmonics. Hold
Shift at the start of a knob drag for fine adjustment. The existing isolated
C4 waveform and ADSR plots use the actual new voice automatically. For Pad,
use longer note gates to reach its sustain portion; short gates release during
attack/decay by design. These are suggested starting roles, not listening approval.

## 4. Compatibility and MCP

The new preset IDs are appended after the original ten IDs:
`rounded_keys`, `rounded_pluck`, `rounded_pad`. Existing IDs, default parameters,
voice branches, ranges, automation targets and session schema remain unchanged.
New files containing these IDs require a build that recognizes them; older
binaries cannot reproduce the new voice. Existing projects retain their prior
sounds. Parameters and automation use the same physical units as before.

Capability discovery exposes thirteen presets and each new preset's ten
`visible_parameters`. Existing guarded `set_instrument`, inspection, request
receipts and scoped undo/redo apply without tool signature changes. Selecting a
different preset with an empty parameter object initializes its factory defaults;
selecting an already-active preset does not reset edited parameters. The MIDI
phrase/chord authorship and WAV export in tests are fixture-only operations,
not new MCP tools.

## 5. Validation and retained evidence

`test-instrument-controls` verifies independent Fourier amplitudes, omitted
upper-note triangle harmonics, stable preset string IDs, actual sample changes
for all ten controls, extreme-value per-voice headroom, and exact diagnostic/
source parity across all thirteen presets and three sample rates.
`test-midi-scheduling` retains exact full-scan comparisons, automation, seeks,
render partitions and guarded zero heap calls. Factory rendering includes a
four-note chord headroom check for the new presets. UI tests exercise real
Rounded Keys browser selection, factory defaults and undo/redo. Session
serialization iterates all preset IDs, including the additions.

The official MCP client selects all three presets, checks exact factory-default
readback, renders each, undoes/redoes selection, and checks byte-identical WAV
restoration. It also renders a held C3/G3/C4/E4 pad chord. All outputs are 2.5 s,
48 kHz stereo with normalization disabled. The original sine/pluck/Synth Lab
patch renders remain byte-identical to the preceding implementation slice.

Normal artifacts: `build/rounded-synth-20261008/receipts/`.
Final musical run: `instrument-loop-4047pil5/instrument-loop.json` and matching
PCM16 `*-listen.wav` files. Native `native-rounded-*.png` frames use actual
Vulkan product painters with validation enabled; PNGs are lossless conversions
of the original BMP artifacts. Native frames and input harnesses are not human
pointer or physical audio-device acceptance. See `acceptance.json` for check
results and source/artifact hashes. No commit, installation or global MCP
configuration change accompanies this slice.

## 6. Reuse and ownership

Reuse-adopted: existing bounded sine/triangle helpers, engine ADSR/source
scheduling, application actions/history, session serialization, preset browser,
MCP capability registry, `core_theme`, `core_font` and `kit_ui`.
Reuse-deferred: no new purpose for `core_time`, `core_queue`, `core_sched`,
`core_jobs`, `core_workers`, `core_wake`, `core_kernel`, `core_data`, `core_pack`,
`core_memdb`, `core_scene`, `core_space`, `core_trace`, `core_math` or `kit_viz`.
The rounded blend and preset policy remain DAW-owned; generic shared numeric,
runtime and presentation APIs need no extension. No shared module version,
adoption minimum, subtree pin or compatibility metadata changes.
