# Sampled tenor saxophone

Select **Woodwinds > Sampled Tenor Saxophone**. The stable voice ID is
`sampled_saxophone`, appended after all existing voices. The fixed bank
`vcsl_tenor_saxophone_v1` uses 46 original stereo 24-bit, 48 kHz CC0 VCSL
non-vibrato tenor sustain recordings: 23 roots, each with two dynamic layers.
Sounding MIDI range is 44–88 (A-flat2–E6), including the recorded upper register.
Within this range, nearest-root transposition is at most one semitone. Notes
outside it are silent. Source filenames label octaves one below soniCs C4=60;
fundamental/harmonic measurements confirm the +12 mapping. No additional B-flat
transposition is applied. Original bytes are retained with per-file hashes.

Velocity changes recorded tone and volume. Held-note Dynamics blends the two
layers with the existing smoothed wind blending; Velocity Timbre sets velocity's
influence. Level, Attack, Release, Dynamics and Velocity Timbre are supported.
The complete automation menu advertises these controls and keeps unsupported
synthesis controls disabled with their existing reasons. Defaults are level .75,
attack 8 ms and release 180 ms. Presets retain v5 and 29 physical parameters;
projects preserve voice identity, settings and lanes independently of libraries.

Loop preparation scores seam correlation, boundary level agreement and 100 ms
sustain-energy variation. Long loops use 120 ms overlaps with correlation-aware
energy compensation. Protected onset/release gates and 20 ms Dynamics smoothing
reuse the existing sampler. Natural tonguing and breath texture remain; this
first voice has non-vibrato sustain, rather than dedicated recorded legato,
staccato or growl articulations.

The sampler descriptor now carries a source rate: existing banks remain at
44.1 kHz with their original assets, maps, defaults and playback arithmetic;
saxophone loads at native 48 kHz. The existing decoder, engine, UI, MCP and
persistence contracts are reused. No shared-library adoption or version changes
are needed; app-specific bank policy stays in the sampler. No I/O, allocation or
locking is added to rendering.

Source: https://github.com/sgossner/VCSL
Pinned revision: c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e.
Source directory: Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato.
License: CC0-1.0; LICENSE.txt and provenance.json are bundled.

Reprepare with `tools/prepare_saxophone_samples.py assets/instruments` using
NumPy. Verify with `tools/verify_saxophone_assets.py
assets/instruments/vcsl_tenor_saxophone_v1`, `make test-sampled-saxophone`,
`make test-mcp-sampled-saxophone`, existing instrument/session tests, headless
smoke and the Main Edit package/runtime checks. The canonical
`artifacts/sampled-saxophone/20261011/` lane retains the demo and verification
receipts. Listening acceptance remains a separate user judgment.


## Saxophone stabilization

Playback uses separate stabilized metadata; original recordings and the initial
map remain intact. `tools/stabilize_saxophone_samples.py assets/instruments`
prepares shared loop bounds for each dynamics pair and shifts the strong layer
by at most one source cycle (under 10 ms) to align the sustained waveform phase.
An offline dense waveform search checks shared seams, boundary levels and steady
energy. Upper-register loops may be shorter (at least 0.8 s); the irregular F#5
and E6 roots permit a .65 seam-correlation floor, while others require .85. The
120 ms correlation-aware overlap remains active.

Sax-only lower-quartile positive inter-layer correlation conservatively compensates the blend's
shared energy through attenuation, with no cancellation-driven boosts. Existing
Dynamics smoothing and protected attack/release gates remain unchanged. A new
real-sample regression bounds middle-layer energy across the entire sax range
and held-note windows. Other instruments retain their prior maps and DSP.

Measured native-root sustain windows improve the weakest middle-blend RMS ratio
from .226 to 0.863 relative to matched endpoint energy; the original worst pitch
(MIDI 82) improves to 0.927. These measurements and the matched production-engine
WAVs are retained in canonical `artifacts/saxophone-stabilization/20261011/`. The
comparison repeats the original velocity/range tests and holds MIDI 82 during
the ten-second Dynamics sweep to expose the measured issue. Listening preference
remains a separate acceptance step.
