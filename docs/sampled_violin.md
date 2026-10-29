# Sampled Solo Violin

The Main Edit development instrument `sampled_violin` plays real VSCO 2 Community
Edition solo bowed-vibrato recordings. Choose **Strings > Sampled Solo Violin**
in the instrument preset browser. Its usable range is MIDI 55–96 (G3–C7, using
middle C = C4); notes outside this range are silent. Fifteen recorded roots cover
this range with no more than two semitones of transposition between roots.

## Playing and expression

Each root has separate soft and strong recordings. Note velocity changes both
the recorded-layer blend and amplitude. **Dynamics** is a continuous 0–1 control:
0 favors soft playing and 1 favors strong playing. **Velocity Timbre** controls
how much note velocity moves that blend (0 = Dynamics alone; 1 = full velocity
influence). The exact blend is `clamp(Dynamics + (velocity - 0.5) * Velocity Timbre)`.
Level remains a separate amplitude control. Dynamics also supplies a modest
amplitude swell; steady soft/strong layer levels are matched per pitch with explicit transient headroom,
and equal-power blending avoids the average midpoint loudness sag. Separate recordings can produce some chorus
or beating during blends; this foundation does not claim phase-aligned layers.

The original bow onset plays once. A 120 ms raised-cosine crossfade repeats
selected steady bow material until note-off. Attack and Release use smooth S curves. Independent 3 ms onset and 4 ms release
click-protection floors also apply when their controls are set to zero. Long MIDI notes and live held keys sustain,
and region-end release may continue into following time. Natural recorded
vibrato remains; synthetic vibrato, oscillator and percussion controls are not
advertised for this voice. Dynamics changes during rendering are smoothed over
approximately 20 ms per note. It is polyphonic, with no true recorded legato
transitions or additional articulations in this first instrument.

## Automation and MCP

The full automation dropdown remains stable. This voice enables Volume/Pan,
Instrument Level, Attack, Release, Dynamics and Velocity Timbre. Other targets
remain visible and disabled with a reason. Dynamics and Velocity Timbre append
stable automation IDs 26 and 27. Instrument automation follows the existing
normalized offset convention: an offset of -1/+1 moves a parameter by half its
physical range. With default Dynamics 0.5, those endpoints reach 0/1.

MCP capabilities expose the bank identity, range, 15 roots, two layers and
articulation, plus the actual visible controls. Existing guarded `set_instrument`,
`set_automation`, user preset, audition, analysis and preview operations apply.
Preset format v5 stores all 29 physical parameters; v1–v4 presets still read with
their original parameter counts and compatible defaults. Projects embed the
voice ID, parameters and automation independently of the personal preset library.
The fixed `vsco2_solo_violin_v1` bank is bundled with the app; this is not a generic
user-sample import or SFZ loader. Retain this bank unchanged for reproducible recall.

## Assets and runtime ownership

Source: https://versilian-studios.com/vsco-community/
Upstream revision: `440300901dfe9275fd84e0b7763af1f8443ae62e`.
License: CC0-1.0; original license and all 30 file hashes accompany the bank in
`assets/instruments/vsco2_solo_violin_v1`. Source WAVs total 74,355,212 bytes.
`preparation.json` retains onset, loop, gain and approximate pitch audit metadata;
`sample_map.inc` and `preparation.json` retain the original foundation mapping.
The stabilization uses `sample_map_stabilized.inc` and `preparation_stabilized.json`:
longer phase-matched loops and per-pitch matched dynamic layers; WAV bytes remain unchanged. Original WAV bytes are preserved.

The existing audio WAV decoder loads the complete bank once on the control thread,
before publication of any instrument source. Decoded stereo samples occupy approximately 142 MiB and remain immutable and resident
for process lifetime. Rendering does no file I/O, locking or heap allocation.
Recorded stereo is averaged to a centered mono instrument; the track pan and
routing system controls its placement. The loader refuses a missing/invalid bank;
package assembly verifies all source checksums. The packaged launcher uses its
own bundled asset path. `DAW_INSTRUMENT_ASSET_ROOT` is a development override.

## Verification and listening

`make test-sampled-violin` checks every supported pitch at 44.1/48/96 kHz, ten-second
holds, release, distinct recorded timbre, region-end tails and block-independent
expression. `make test-mcp-sampled-violin` checks discovery, guarded apply/replay,
voice-aware lanes, preset previews and saved-project recall. Run preset, session,
controls and automation regressions plus the headless smoke and Main Edit package
self-test before refresh. Acoustic preference and musical realism still require
listening; numerical checks do not establish those judgments.

The separate editable **Sampled Violin Foundation** demo has three velocity levels
at 0/2/4 seconds, a ten-second swell from 6 seconds, and a pitch-range melody from
16 seconds. Its WAV contains 24 seconds of music plus a three-second tail.

## Stabilization pass — 2026-10-11

The user-authorized stabilization refines playback DSP and preparation metadata.
The original bank, original mapping and first demo remain retained for comparison;
existing sampled-violin presets keep their IDs and controls and use the refined DSP.
This is a deliberate sound refinement, not a promise of bit-identical older renders.
Recorded layer blends can still produce natural beating; no synthetic or recorded
legato transitions are introduced. See the same-project before/after WAVs in the
violin stabilization evidence.

Preparation is reproducible with `tools/prepare_violin_stabilization.py <sample-bank-root>` (offline NumPy required). It preserves original WAVs and original metadata, searches positive waveform correlation and boundary-level agreement over long sustain sections, and calibrates each soft/strong pair with a transient headroom limit. Runtime overlap energy uses the measured seam correlation. The new loop periods span 1.88–6.58 seconds at recorded pitch. No generic denoising, pitch retuning or true legato is applied.
