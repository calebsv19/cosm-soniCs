# Acoustic Piano and String Foundations

Main Edit adds four stable factory identities: `sampled_acoustic_piano` (ID 35), `sampled_viola_section` (36), `sampled_double_bass` (37), and `sampled_violin_section` (38). Existing preset IDs remain unchanged. Preset schema v6 now appends three piano controls; v5 remains readable. See [piano expression](piano_expression.md).

| Voice | Sounding MIDI range | Recorded roots | Layers | Material |
| --- | --- | --- | --- | --- |
| Acoustic Piano | 21–108 / A0–C8 | 30 | 3 | Acoustic grand strikes, natural decay |
| Viola Section | 48–86 / C3–D6 | 13 | 2 | Bowed vibrato section sustains |
| Double Bass | 28–59 / E1–B3 | 14 | 2 | Bowed non-vibrato solo double bass |
| Violin Section | 55–86 / G3–D6 | 11 | 2 | Bowed vibrato section sustains |

## Piano behavior and controls

Piano uses finite recorded strikes rather than a repeated sustain loop. Note velocity selects a recorded soft, medium or strong take (source velocity layers 1, 8 and 16, selected at normalized velocities below .35, below .75 and above), with continuous velocity loudness within each zone. The recording decays naturally while the key remains held, and note-off applies the protected release envelope. Original strikes are centered to mono, trimmed with attack preroll and faded to a zero endpoint; one bank gain preserves relative recorded levels across layers and registers.

Piano supports Level, Attack, Release, Sustain Pedal, String Resonance and Release Noise. The pedal separates key-off mechanics from damper-off timing. Resonance uses 88 damped modes driven by recorded strikes, and recorded mechanical layers supply all 88 key releases and two variants each for pedal-down/up. See [piano expression](piano_expression.md) for automation values, live semicolon audition, scope and limitations. Dynamics and Velocity Timbre remain unavailable because wind-style controls must not morph an already struck note. Presets now store 32 physical fields in v6 while v1–v5 remain readable.

## String behavior

The three bowed banks reuse the existing pitch-centered sustain sampler, coordinated root/layer loops, protected note gates and bounded prepared energy/covariance correction. Level, Attack, Release, Dynamics and Velocity Timbre remain supported. Viola and double bass use original centered recordings with measured static pitch correction; violin-section stronger sustains additionally follow the soft take's measured harmonic phase clock after the attack to avoid layer cancellation. Original source WAVs remain untouched. String overlaps are 240 ms and the .75–1.35 blend gain limits are unchanged. Defaults are Attack/Release 18/260 ms for viola, 18/280 for double bass and 16/240 for violin section; Level is .8.

Audition note selection now places an octave of C within the bank range. Double bass therefore auditions C3 rather than silent C4. Live audition, WAV preview and analysis note metadata share that selection. Chords retain the existing whole-voicing octave placement. UI labels use Audition Note and Held Note. Other instruments keep their prior note choices and playback arithmetic.

## Sources and preparation

Viola, violin-section and double-bass originals come from VSCO 2 Community Edition commit `440300901dfe9275fd84e0b7763af1f8443ae62e` under CC0; all banks retain LICENSE, source URLs, original byte counts and SHA256 provenance. The string banks contain 26, 22 and 28 original WAVs respectively.

Piano material is Salamander Grand Piano v3 by Alexander Holm, from sfzinstruments/SalamanderGrandPiano commit `3382bf9496bba2486f5ab0de55a264d1dfc38404`, remapped by kinwie. It is used under CC BY 3.0. Original FLACs, license, upstream README and attribution are included alongside the 90 derived mono 24-bit/48 kHz strikes. Prepared-file manifests link each derived SHA to its original source. These are derived sample assets, not an implementation of the source library's ARIA/SFZ extension engine.

With NumPy and ffmpeg available, run `python tools/prepare_string_sections.py assets/instruments` and `python tools/prepare_acoustic_piano.py assets/instruments`. The wrappers select only these new banks. Existing tool defaults and existing bank assets remain unchanged. Run `tools/verify_string_sections_assets.py` and `tools/verify_acoustic_piano_assets.py` with the assets/instruments root to validate source/derived identity, coverage, paired layers, loop bounds and finite piano endpoints.

## Verification and deliverables

`make test-sampled-piano-strings test-instrument-controls test-sound-inspection test-session test-mcp-sampled-piano-strings` covers complete note ranges at 44.1/48/96 kHz, sustained string blend bounds, recorded piano velocity timbre, natural decay without loops, damped note-off, short retriggers, overlapping notes, callback partitions, supported controls/automation, preset saving and session/MCP recall. Native UI and candidate/installed package checks are separate evidence layers. Human listening remains the musical acceptance step.

Editable comparison projects and four 27-second production demo WAVs are retained in `daw/artifacts/sampled-piano-strings/20261012/`. Demos use original project levels without normalization and do not modify the user's song. Piano shows velocity strikes, a held decay, keyboard traversal and a short chord; strings show velocities, held Dynamics expression and their ranges.
