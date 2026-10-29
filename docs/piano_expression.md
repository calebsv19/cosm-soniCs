# Piano Pedal, Resonance and Mechanics

Sampled Acoustic Piano adds three piano-only controls in the Mod group: Sustain Pedal, String Resonance and Release Noise. Factory defaults are pedal up (0), resonance .25 and mechanical noise .35. Set resonance and noise to 0 for the original dry foundation. Level, Attack and Release remain available. Wind-style Dynamics and Velocity Timbre remain unavailable; velocity still chooses a recorded soft, medium or strong strike at .35/.75 boundaries.

## Sustain

Sustain is binary: values at least .5 open the dampers. A key released while down continues the original finite recording; it does not loop or restart. Pedal-up applies the release envelope at its actual timeline frame. Notes released before pedal-down are not revived. This first implementation does not support half-pedal damping, sostenuto, or catching an already damping note with a late pedal. A finite region or export boundary raises the pedal and bounds the tail, including when the last authored value is down. Pedal is scoped to each prepared region; continuous cross-region pedaling is not yet a shared track-wide voice pool.

In the automation dropdown choose Sustain Pedal (stable target 28). The existing offset domain adds half the lane value to the base knob. With the base at 0, use hold points of 1 for down and 0 for up; with the base at .5, use 1/-1. Hold curves give explicit switching times. Linear and smooth curves also resolve the first downward threshold at sample precision. Avoid combining opposing track and region pedal ramps; a single hold lane makes the intended pedaling unambiguous.

For computer-keyboard audition or QWERTY recording, hold semicolon (`;`) as the pedal. Released keys retain their original key-off time while their damper gates remain open. Pedal-up enters the worker command queue with safety admission; panic, focus-loss cleanup and leaving audition clear sustain. This key auditions pedal behavior; it does not record a pedal automation lane. External hardware MIDI CC64 ingestion is not implemented by the app's current input system.

## Resonance and release layers

String Resonance (target 29) controls 88 stable damped modes tuned to the piano keys. Recorded strike audio excites strings whose dampers are lifted by held keys or the pedal. Closed strings damp quickly when the pedal rises. This is a modest sympathetic-string model, not a full soundboard, cabinet or multi-harmonic physical simulation. Reset and transport discontinuities clear its stored energy. Audio rendering performs no sample loading or allocation.

Release Noise (target 30) mixes recorded chromatic mechanical key releases and alternating recorded pedal-down/up actions. Key mechanics trigger at key-off independently of the sustained pitched sound, with velocity and held-time attenuation. Pedal variants alternate deterministically for reproducible exports. These are recordings from Alexander Holm's Salamander Grand Piano v3 under CC BY 3.0, pinned to the same upstream commit as the strike bank. They retain originals, URLs and source/derived SHA256 manifests in `assets/instruments/salamander_piano_v1/expression/`. Preparation centers to mono 24-bit/48 kHz, keeps at most one second, and fades both endpoints; it never rewrites strike assets.

The three controls can produce a dry close piano, subtle mechanical detail, or more open pedal resonance. The original three strike layers still set playing intensity; this pass does not replace them with synthesized piano tones.

## Compatibility and verification

Preset schema v6 appends controls 29–31 to the 29 stable v5 values, for 32 total. Versions 1–5 remain readable and keep the new controls at zero to preserve older piano sounds. Session version 25 writes the new controls/automation; version 24 projects remain readable. Existing parameter objects without the new fields keep them at zero; explicit v25 values recall the extension. Non-piano voices ignore these controls and expose no piano-only automation. Isolated waveform analysis shows strike and key mechanics; shared sympathetic resonance is measured in the full source/preview render rather than the stateless note waveform.

`make test-piano-expression` checks binary and interpolated pedal timing, live note-list updates, key/damper separation, full chromatic release coverage, resonance damping and transport cleanup. Existing sampler, instrument-controls, lifecycle, sound-inspection, preset-library, session and MCP checks remain separate regression gates. Candidate/installed package identity, native control rendering and actual packaged playback are verified separately. Human listening remains the sound-quality acceptance step.

The comparison projects and WAVs are in `daw/artifacts/piano-expression/20261012/`. Both use the same notes and levels without normalization; the before version disables the three extensions and the after version adds authored hold-pedal phrases, string resonance and mechanical detail. No comparison project replaces the user's song.

Architecture reuse decision: app-local piano semantics reuse the existing sampler, automation, MIDI source and worker command queue. `kit_audio` is still planned in the shared catalog; no shared module implements this instrument voice. Shared adoption/version contracts are unchanged. Atlas topology and active plan state are unchanged, so no Atlas rebuild is needed for this bounded instrument extension.
