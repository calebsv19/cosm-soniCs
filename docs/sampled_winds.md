# Sampled trumpet and clarinet

Choose **Brass > Sampled Trumpet** or **Woodwinds > Sampled Clarinet** in the
instrument browser. Both use original CC0 VSCO 2 Community Edition recordings,
nearby-root pitch playback, long crossfaded sustain, protected S-curve note gates,
and continuous recorded dynamics. MIDI pitches are sounding pitches (C4 = MIDI 60).

| Voice | Sounding MIDI range | Recorded roots | Dynamics layers | Source WAVs |
| --- | --- | --- | --- | --- |
| Trumpet | 53–84 (F3–C6) | 10 | 2 | 20 |
| Clarinet | 50–89 (D3–F6) | 11 | 3 | 33 |

Notes outside each supported range are silent. Nearest roots transpose by at most
two semitones within the ranges. These source producers label filenames one octave
below soniCs's convention; the actual fundamental and harmonic families were checked.
The clarinet files labeled F#5 sound at F6 and are mapped to MIDI 89. Original names
and bytes remain unchanged; preparation.json records the octave and semitone corrections.
Natural tuning deviations are retained, not digitally retuned.

## Playing and expression

Both voices expose Level, Attack, Release, Dynamics and Velocity Timbre. Velocity
changes recorded tone and amplitude. Dynamics blends layers during held notes;
Velocity Timbre controls the velocity contribution to that blend. Instrument
Dynamics/Velocity Timbre use existing automation target IDs 26/27. The full menu
keeps unavailable oscillator/filter/percussion controls visible and disabled.

Trumpet blends its two recorded layers; clarinet crossfades only between adjacent
soft/medium/strong recordings, with a continuous transition through the middle layer.
Prepared sustain levels are matched per pitch with a transient headroom limit.
Positive boundary correlation and level agreement guide long-loop selection;
correlation-aware overlap energy reduces average seam dips. Gates share violin's
3 ms onset and 4 ms release protection, including zero-time UI settings. Trumpet
has a 5 ms attack/140 ms release default; clarinet 10/180 ms. Recorded tonguing,
breath texture and timbre remain. There is no true recorded legato, mute selector,
staccato articulation switch, random round-robin or breath-controller mapping yet.

## Shared sampler and violin preservation

The app-owned instrument_samples module uses fixed bank descriptors with preset ID,
range, roots, two or three layers, articulation and precomputed per-note lookup.
All banks load before sources are published; rendering does no I/O, allocation or
locking. Trumpet and clarinet are pre-downmixed to centered mono; violin retains
its original stereo decode and averaging arithmetic. The violin's assets, mapping,
defaults and DSP operation order are preserved. Rendering its original stabilized
demo produces a byte-identical WAV in the shared-bank implementation.

This is a fixed acoustic-bank foundation, not a generic SFZ/user-sample importer.
No shared/core or shared/kit library is changed. Existing WAV decoding and engine
source/automation/history contracts are reused. Banks remain immutable and resident
for process lifetime. DAW_INSTRUMENT_BANKS_ROOT is a development override; the packaged
launcher selects its own bundled bank root. The original violin-specific override
remains supported.

## Persistence, provenance and verification

Stable voice IDs are sampled_trumpet and sampled_clarinet, appended without changing
older IDs. Presets still use format v5 and the same 29 physical parameters. Projects
embed the complete voice settings and lanes independently of the personal preset
library, and refer to the fixed bundled bank through the stable voice ID. MCP
capabilities advertise each actual pitch range, root/layer count and articulation.

Source: https://versilian-studios.com/vsco-community/
Pinned upstream: sgossner/VSCO-2-CE revision 440300901dfe9275fd84e0b7763af1f8443ae62e.
Both banks retain LICENSE.txt, provenance.json with hashes, original WAVs and
preparation.json. Twenty trumpet WAVs total 40,543,376 bytes; 33 clarinet WAVs total
59,347,828 bytes. Package assembly independently checks all hashes and mappings.

Run test-sampled-winds, test-sampled-violin, test-instrument-controls, test-session,
test-mcp-sampled-winds and test-mcp-sampled-violin, followed by headless smoke,
native control capture and Main Edit package/runtime checks. The paired demos
exercise three velocities, a ten-second swell, and the supported pitch range;
a separate duet checks coexistence. Numerical/native checks do not establish
subjective acoustic preference. See artifacts/sampled-winds/20261011 acceptance
receipts and WAVs in the canonical evidence checkout.


### Wind stabilization

Wind playback uses `sample_map_stabilized.inc`, prepared with
`tools/prepare_wind_samples.py assets/instruments --stabilize`. Loop selection
measures 100 ms sustain-energy variation alongside seam correlation and boundary
level matching. Original recordings and original preparation remain intact.
Wind dynamics use a quintic easing curve within each adjacent-layer blend, giving
the clarinet middle layer a smooth timbre slope. Existing protected attack and
release gates remain active. Violin mapping, blend arithmetic and defaults are
unchanged. Matching before/after WAVs and installed checks are retained in the
canonical `artifacts/wind-stabilization/20261011/` evidence lane.
