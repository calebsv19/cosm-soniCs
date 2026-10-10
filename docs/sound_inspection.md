# Instrument sound inspection and comparison

The five-step sound-inspection sequence is implemented in persistent DAW Main Edit: common production sound rendering, calibrated inspection, an immutable comparison reference, two read-only MCP adapters, and signal/native/listening qualification. This is source development; VERSION remains 0.4.0. A later release should review this as a minor feature. No commit, installed-app replacement, release or global MCP client registration is implied.

## 1. Common bounded production rendering

`sound_render.c` captures a complete base voice and all fourteen physical parameters, rejects nonfinite/out-of-range values, and renders through the existing production instrument source into private memory. `preset_preview.c` uses the same float render for WAV publication. The standardized sound is three seconds at 48 kHz, stereo, velocity 0.5, original level without normalization, a 1.5-second gate, and either C4 or the C-major pitches 48/55/60/64. No track effects, mixer gain, project automation, live transport or device state enter this isolated render. One render owns 1,152,000 bytes of sample storage; temporary storage is freed on success and failure.

Analysis reports peak, stereo RMS, stereo mean/DC and the number of float channel samples with absolute magnitude at least one. WAV preview rejects samples above full scale and converts to stereo PCM16; its reported metrics are measured before PCM quantization. Analysis can report clipping without publishing a file. The existing legacy instrument preview remains available beside the Inspect entry point.

## 2. Inspection plots

Open **Inspect** in the selected MIDI region's instrument panel. **Refresh** explicitly measures its effective sound; painting only displays cached results. **C4/Chord** chooses the standardized stimulus. The three views use fixed scales, so changing a sound does not silently rescale its level:

- **Waveform:** 256 sampled points from a 20 ms mono mean-of-stereo interval; fixed -1 to +1. This is a downsampled display, not an alias-free reconstruction.
- **RMS Envelope:** 256 contiguous stereo RMS blocks covering all three seconds; fixed zero to one.
- **Spectrum:** 128 logarithmically spaced tonal measurements from 20 Hz to 20 kHz. A 2048-frame Hann interval (42.67 ms at 48 kHz) uses the existing engine analysis plan and coherent-gain calibration. Values are tonal peak-amplitude dBFS, floored at -160 dB; the UI displays -100 to +6 dB.

**Attack**, **Sustain** and **Release** select fixed starts at 0, 1.0 and 1.5 seconds for waveform and spectrum. They are repeatable probe intervals, not dynamically detected ADSR stages. Whole-render metrics and the three-second envelope do not change with that selection. This tonal transform is not a power spectral density, LUFS measurement or uniform FFT-bin plot; closely spaced low frequencies cannot be resolved precisely within a short window.

## 3. Reference comparison and audition

**Capture Ref** refreshes first and retains the complete sound, measurements and plots in transient editor state. Edit instrument controls, then Refresh to overlay current cyan against reference purple. Mode/window changes recompute plots from the captured reference values rather than replacing those values. Peak/RMS and cutoff/attack/drive settings appear beside the plots. Selection or sound changes mark the current capture stale until Refresh. Closing the inspector retains the reference; project persistence does not store it.

**Play Current** refreshes before auditioning; **Play Ref** auditions captured values without applying them to the project. Both clear the prior audition and use the same finite 1.5-second note gate. Playback, keyboard Test, recording, bounce and active instrument gestures reject conflicting actions. Live auditions run at the actual device rate through the selected track, including its mixer/effects, so their output can differ from isolated analysis/WAVs.

Escape closes the panel. While keyboard Test and MIDI record arming are off, inspector shortcuts are 1/2/3 for views, A/S/L for windows, C for note/chord, R for Refresh, B for Capture Ref, P/O for current/reference audition and Delete for clearing the reference. Control/Alt/Command combinations retain their existing routing.

## 4. Read-only MCP adoption

The official SDK server now exposes fourteen tools. `analyze_instrument(target, mode='note', window='sustain')` returns the exact captured instrument and opaque expected state plus analysis. `analyze_preset(preset_id, preset_revision, mode='note', window='sustain')` requires a current library revision and returns the same analysis structure. Modes are `note`/`chord`; windows are `attack`/`sustain`/`release`. Both tools advertise read-only annotations and work at a read-only bridge endpoint.

Analysis includes the complete sound settings/synthesis version, stimulus/render/window metadata, explicit calibration labels, four whole-render metrics and bounded waveform/envelope/frequency/dB arrays. It does not create a library, publish files, consume mutation receipts, change history or alter project/device state. Busy owner workflows reject analysis rather than racing state. Existing strict target identity and revision conflicts remain applicable. The native bridge performs bounded work on its owner thread; analysis is request-driven rather than continuously streamed.

## 5. Qualification and retained evidence

`make BUILD_DIR=<fresh-root> test-sound-inspection test-mcp-sound-analysis` calibrates silence, known sine amplitude, clipping/nonfinite rejection, selected windows, exact production sample parity, immutable reference and finite auditions. The SDK test proves repeatability, read-only endpoints, exact preset/instrument analysis parity, unchanged authored state and metric parity with original-level WAVs. Three pairs isolate cutoff, attack and drive edits; WAVs are listening examples, not a claim of human preference.

Native editor qualification uses the actual Vulkan painters and comparison controls with validation and nested clip containment. Actual App_Run qualification separately checks the running native owner and persisted project. Retained receipts, selected native frames and six listening WAVs belong in `build/receipts/sound-inspection-20261009/`; the qualified source executable belongs under `build/sound-inspection-20261009/targets/macOS-arm64/toolchains/clang/bin/daw_app`. Successful disposable compiler, fixture and sanitizer output is retired after consumers finish.

The engine's existing Hann tonal transform and shared UI button tokens are reused. `core_math` currently has no equivalent signal transform; no new shared library, dependency minimum, adoption edge or project schema is introduced. Reference capture and isolated inspection remain app-specific workflows.

Next focus: bounded transactional MIDI authoring using common application actions, followed by MIDI MCP adoption. Future analysis can add project/track taps, longer spectral windows, filter-envelope modulation and additional synth behaviors as separate slices. Physical listening preference, sustained device performance and cross-platform native qualification remain separate acceptance layers.
