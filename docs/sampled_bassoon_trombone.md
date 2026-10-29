# Sampled Bassoon and Tenor Trombone

Main Edit includes `sampled_bassoon` (ID 33, Bb1–C5 / MIDI 34–72, twelve recorded roots) and `sampled_tenor_trombone` (ID 34, Bb1–F4 / MIDI 34–65, eleven roots). Both use the existing resident acoustic sampler with complete pairs of genuinely recorded v1/v2 dynamics. Unsupported pitches are silent. Bassoon's unpaired upper Eb5 is excluded; additional trombone v3 and short articulation recordings are not used by this initial two-layer sustain bank.

## Playing and expression

MIDI duration controls sustain and release. Velocity changes recorded timbre and loudness; Dynamics can vary timbre during a held note, and Velocity Timbre controls velocity's contribution. Supported controls are Level, Attack, Release, Dynamics and Velocity Timbre. Existing track/clip gain and pan plus supported instrument automation remain available. Synthesis controls that do not affect these voices remain disabled with a reason. Preset recall uses the existing v5, 29-parameter schema.

Defaults are Level .8, Attack 10 ms and Release 180 ms for bassoon; Level .8, Attack 8 ms and Release 200 ms for trombone. Both retain the existing click-protected envelope, continuous layer mixing and bounded .75–1.35 prepared energy correction. Bassoon uses 120 ms loop overlap; trombone uses 240 ms. Neither includes sampled legato transitions, slide articulations or separate staccato material yet.

## Source and preparation

Original stereo 16-bit/44.1 kHz WAVs come from VSCO 2 Community Edition commit `440300901dfe9275fd84e0b7763af1f8443ae62e`, under CC0. The banks retain original recordings, LICENSE, source URLs, byte counts and SHA256 hashes. Bassoon has 24 originals; trombone has 22. Prepared mono 24-bit WAVs retain source-hash links and derived hashes.

The existing pipeline measures sounding pitches, trims leading silence, centers pitch and aligns the stronger sustain take's clock using a reliable shared harmonic. It preserves the initial attack before the alignment reference and smoothly reaches full alignment at 800 ms. Common loops, fixed root/layer gains and time-varying energy/covariance curves are prepared offline. Loop refinements must improve seams without worsening stationarity or dynamics energy beyond the documented qualification tolerances. All eight prior banks keep their assets, sample maps, defaults and playback arithmetic.

With NumPy available, run `python tools/prepare_bassoon_trombone_samples.py assets/instruments`. Explicit bank selection processes only this pair; older tool defaults stay unchanged. Run `python tools/verify_bassoon_trombone_assets.py assets/instruments` to validate pinned originals, derived audio, complete layer pairs, ranges, loop bounds and generated energy metadata.

## Verification and demos

`make test-sampled-bassoon-trombone test-instrument-controls test-sound-inspection test-instrument-lifecycle test-session test-mcp-sampled-bassoon-trombone` covers all pitches at 44.1/48/96 kHz, long holds, .95–1.10 sustained blend bounds, note-off/retrigger/release behavior, overlapping notes and exact callback partitions. MCP checks discovery, supported automation, guarded apply/replay, preset saving, preview WAVs and project recall. Native rendering qualifies the instrument panel and supported controls. Package and installed-runtime checks remain separate from human listening acceptance.

Editable 27-second audition projects and unnormalized production WAVs are retained under `daw/artifacts/sampled-bassoon-trombone/20261012/`. Each demonstrates three velocities, ten seconds of held-note Dynamics expression and traversal across the usable range. The demos do not alter the user's project.
