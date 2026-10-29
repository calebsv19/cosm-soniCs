# Sampled flute and cello section

Main Edit includes `sampled_flute` (Woodwinds) and `sampled_cello_section` (Strings). These are recording-based sustain voices on the existing fixed acoustic sampler.

| Voice | Sounding range | Recorded roots | Layers | Source material |
| --- | --- | --- | --- | --- |
| Sampled Flute | MIDI 60–93, C4–A6 | 9 | soft / strong | non-vibrato sustain |
| Sampled Cello Section | MIDI 36–77, C2–F5 | 13 | soft / strong | bowed ensemble sustain with recorded vibrato |

The cello is a section recording, not a solo cello. Both banks use pinned VSCO 2 Community Edition CC0 recordings, commit `440300901dfe9275fd84e0b7763af1f8443ae62e`. The 44 original 24-bit stereo 44.1 kHz WAVs remain unchanged; `provenance.json` records source URLs, sizes and SHA256 values. Playback uses a centered mono downmix, matching the existing fixed sampler. The source filename octave convention is one below soniCs C4 = MIDI 60.

## Playing and expression

Notes sustain until their MIDI gates end, then follow Release. Level, Attack, Release, Dynamics and Velocity Timbre are the supported instrument controls. Velocity changes the blend between recorded soft and strong timbres; Dynamics can evolve that blend during a held note. The existing automation dropdown retains all options and explains unavailable synthesis controls. Track gain/pan, clip gain/pan, instrument level, Dynamics and Velocity Timbre are available; oscillator, filter and synthetic vibrato controls are unavailable for these recordings.

Flute defaults are Level 0.75, Attack 6 ms and Release 140 ms. Cello section defaults are Level 0.8, Attack 20 ms and Release 280 ms. Existing voices, defaults and preset IDs are preserved. The catalog has 31 voices; v5 presets continue to store 29 physical parameters. Neither a new preset schema nor a project migration is required.

## Preparation and stabilization

`tools/prepare_flute_cello_samples.py` verifies pitch families (including harmonic-rich low cello), trims only leading silence, measures each take's steady pitch center, and prepares paired dynamics. Each recording receives a fixed pitch-center ratio, at most 3.91 cents for flute and 9.06 cents for cello. Only these two banks use the optional ratio-aware playback path; original WAVs and the prior four banks retain their existing paths.

`tools/stabilize_flute_cello_samples.py` coordinates loop boundaries across paired layers, aligns their sustained phase within one source cycle, and applies conservative correlation-aware blend normalization. It produces common reference-coordinate loops with 120 ms flute or 240 ms cello crossfades. Loop periods are 0.80–2.99 s for flute and 0.82–4.32 s for cello. Loop overlap is accounted for when wrapping. Original attacks play once; note release is an envelope fade of the sustained recording.

`tools/refine_flute_cello_samples.py` performs the subsequent stabilization pass. Runtime now uses `sample_map_refined.inc` and `blend_envelopes.inc`; the initial stabilized maps remain available for comparison. Three cello loops (MIDI 50, 53 and 71) receive qualified boundary changes. Wider overlaps are evaluated but retained only when seam correlation, natural level variation and corrected layer mixing all qualify; the selected maps retain 120 ms flute and 240 ms cello crossfades.

For both voices, slow 100 ms measurements of soft/strong energy and covariance produce immutable onset and cyclic lookup curves at intervals no greater than 10 ms. Playback interpolates the curves and corrects interference in the current dynamics blend. Correction is bounded to 0.75–1.35 gain, approximately -2.5 to +2.6 dB. The original onset calculation runs for the first 300 ms of reference sample travel, followed by a 300 ms smooth transition into the correction. Pure layer endpoints and their natural envelope movement are preserved; this does not flatten the recorded cello vibrato or change Attack/Release settings. Curve wrap endpoints match, rendering allocates no curve storage, and older banks retain their original paths.

Native-root analysis at quarter, middle and three-quarter dynamics over 100 ms sustain windows measured flute blend RMS at 0.989–1.060 of the expected uncorrelated layer level (previously 0.932–1.175). Cello measured 0.844–1.092 (previously 0.669–1.292). These characterize interference reduction, not subjective listening acceptance or constant loudness. The natural cello section movement and the absence of recorded legato transitions remain unchanged.

Flute's sustained layer blend is very steady; cello retains natural ensemble vibrato and level movement. Recorded vibrato cannot be independently removed or speed-controlled. This first foundation does not include true recorded legato transitions, staccato, pizzicato, breath changes, keyswitch articulations or round robins. Those require additional recordings and explicit articulation behavior.

## Verification

Run `make test-sampled-flute-cello test-mcp-sampled-flute-cello` plus existing sampler/control/session regressions. The audio harness checks all MIDI pitches in each advertised range, long holds, release and overlapping/retriggered note behavior, dynamics endpoints and tightened full-range blending bounds, callback partition invariance through repeated loop wraps, and 44.1/48/96 kHz rendering. The MCP test checks voice metadata, supported automation, guarded preset save/apply/replay, previews and project round trips. Native painter captures cover the expression controls. Packaging runs `tools/verify_flute_cello_assets.py` against bundled originals and preparation tables. Actual candidate and installed-app playback is checked independently of source tests.
