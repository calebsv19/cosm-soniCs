# Filtered synthesizer controls

## 1. Delivered sequence

1. Reconcile the accepted instrument/MCP source into persistent Main Edit and exercise the production native `App_Run` bridge.
2. Add a real low-pass oscillator filter, useful starting presets, native controls and existing MCP adoption.
3. Choose reusable user-library versus project-only preset storage before implementing custom preset persistence.
4. Add bounded production render/analysis actions and MCP tools.
5. Establish transactional MIDI authoring before exposing notes and musical patterns through MCP.
6. Extend arrangement, effects and media-import actions in focused slices.

Steps 1 and 2 are implemented in `/Users/calebsv/Desktop/CodeWork/_worktrees/daw_main_edit`. The checkout was fast-forwarded to the existing 0.4.0 commit and received the exact previously accepted 51-file source slice; no new commit or package installation was made. The original canonical dirty checkout is preserved. Step 3 is now implemented through the [reusable user preset library](user_presets.md), with full project-owned settings and narrow standardized note/chord WAV rendering. That was the historical checkpoint. General signal analysis, MIDI/arrangement adoption and the later modulation/preset extension are now implemented; current contracts appear below and in [current truth](current_truth.md).

## 2. Sound and controls

Filtered Keys and Filtered Pad reuse the rounded sine/triangle oscillator, detuned blend, sub and saturation, followed by a per-note topology-preserving state-variable low-pass and the existing amplitude envelope. The original thirteen voices retain their original audible settings and DSP path. Preset and parameter identifiers are appended, preserving existing serialized IDs.

| Starting preset | Cutoff | Resonance | Tone | Envelope source |
| --- | --- | --- | --- | --- |
| Filtered Keys | 1800 Hz | 0.8 Q | 0.8 | Rounded Keys |
| Filtered Pad | 900 Hz | 1.0 Q | 0.8 | Rounded Pad |

Both advertise twelve controls: Level, Tone, Drive, Cutoff, Resonance, Osc Mix, Detune, Sub and ADSR. Cutoff uses a logarithmic 80–18000 Hz knob; runtime evaluation additionally limits cutoff to 45% of sample rate. Resonance uses Q 0.5–2.0. Tone continues to control oscillator harmonics. The Tone panel fits all four controls and shows physical Hz/Q values. Vibrato is hidden for this family.

Each voice owns separate double-precision filter integrators and coefficients. During contiguous rendering, changing automation targets smooths coefficients over five milliseconds without render allocations. Source preparation, explicit format/reset, note onset and non-contiguous transport reset filter memory. A seek into a held note starts a fresh filter at that position; it does not reconstruct note-on history or promise the same transient as a slice of an uninterrupted render. Instrument edits that rebuild a source use this preparation-reset policy. There is no filter-envelope/LFO routing or oversampling in this slice.

The isolated waveform preview streams from note onset through the exact same filter recurrence using private diagnostic state, then caches the display by sound settings, sample rate and point count. It never reads live audio-worker integrators. The API allows at most 512 points, two seconds of initial note age and one second of displayed span, at rates up to 384 kHz. The single-sample diagnostic helper shares this bound for filtered voices; production playback has no such duration bound. The preview represents a held C4 voice, not a live mixed spectrum. Amplitude ADSR still gates the filtered signal and defines voice retirement.

## 3. Existing MCP adoption

The original seven instrument tools retain their contracts; the [user-preset slice](user_presets.md) adds five library/preview tools. Capabilities now report fifteen factory presets, fourteen registered parameters, per-preset supported controls and `log_hz` for cutoff. Existing guarded `set_instrument`, readback, scoped undo/redo and immutable request receipts apply the new values through the same application actions. No fixture export command is advertised as a production MCP render tool.

## 4. Verification and retained results

Compact evidence lives in `build/receipts/agent-synth-20261009/` in Main Edit. It includes native MCP and saved-session JSON, test logs, selected validation-clean UI frames, measured musical-loop results and three PCM listening WAVs. The final native binary hash is recorded in the actual-app receipt. Disposable compiler/test/ASAN output is retired after all consumers finish. The qualified source-built native executable remains under `build/agent-next-20261009/targets/macOS-arm64/toolchains/clang/bin/daw_app` for the next user audition and native MCP/preset iteration; the reusable existing MCP Python environment remains linked under `build/mcp-venv` for future local MCP use.

- Frequency-response tests compare the filter to an independent bilinear response at 44.1, 48 and 96 kHz; extreme automated cutoff/Q changes remain finite.
- Scheduling tests require exact whole-buffer versus split-buffer output at those rates in mono/stereo, independent sources, discontinuous resets and reused note preparation, with zero instrument render heap calls.
- Preview samples match production source rendering. Factory phrases/chords remain finite and below the fixture peak ceiling without normalization.
- Official SDK tests cover guarded writes, stale state, read-only admission, retries, receipt capacity, inheritance and exact undo/redo. Musical WAV hashes restore exactly across both filtered and rounded presets.
- AddressSanitizer repeats controls, scheduling, application actions and both official SDK exercises.
- Actual native `App_Run` performs instrument write, undo, redo, window lifecycle and normal shutdown. Independent saved-session readback confirms Filtered Keys with 1300 Hz cutoff and 1.2 Q.
- Native product painters pass Vulkan validation and selected images have been inspected. The broader existing headless regression gate and final `make` also pass.

Physical listening quality, sustained real-device performance, installed Desktop delivery, live frequency analysis, general project render and production MIDI authoring tools remain separate acceptance work. The retained listening examples are intended for the next human audition.

## Filter motion and focused presets

Filtered voices now expose four additive Mod controls: `filter_env_octaves` (-4 to +4 octaves), `filter_decay_ms` (5 to 2000 ms), `filter_lfo_hz` (.05 to 12 Hz), and `filter_lfo_octaves` (0 to 3 octaves). The decay envelope begins at its full signed octave shift on each note, then approaches zero exponentially; at one decay time its amount is 1/e of the onset amount. The bipolar sine LFO retriggers at zero phase on each note. Both offsets sum in octave space around the base cutoff, then clamp to 80 Hz through .45 times sample rate before the existing five-millisecond coefficient smoothing. Amplitude ADSR still owns gate/release retirement. LFO rate is physical Hz, without tempo sync in this slice.

Depth zero preserves the previous static filter. Legacy presets keep zero modulation depths. Sweep Bass adds a short downward brightness sweep and sub support; Filter Pluck uses a faster sweep with a short amplitude decay; Moving Pad uses slow bipolar cutoff movement. Stable preset IDs and parameter IDs are appended, preserving all earlier values. Capabilities expose eighteen presets and eighteen parameters, with sixteen supported controls on filtered voices. The native Mod tab, generic `set_instrument`, personal preset recall, project settings, common history and instrument automation share these IDs. The local MCP tool count remains 45.

`instrument_catalog.c` owns stable identities/defaults/physical metadata, extracted from the oscillator renderer without changing existing algorithms. Serialization tests now link this production catalog rather than duplicating its parameter model in stubs. Personal preset format two stores all eighteen physical values; format-one documents with their complete fourteen parameters remain readable with disabled modulation defaults. Unknown/incomplete versions still refuse.

Independent tests cover octave-domain decay/LFO positions, physical cutoff bounds, note retriggering and additive automation identities. Production render/diagnostic parity covers all eighteen presets. Official SDK tests exercise new preset audio and explicit modulation edits with exact undo/redo. Project serialization checks every new physical value exactly; library tests cover legacy recall and complete new settings. Native painter and actual App_Run save/restart qualification are retained in the development evidence lane; these do not claim installed delivery or human listening preference.
