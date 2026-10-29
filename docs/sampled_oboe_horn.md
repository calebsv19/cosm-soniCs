# Sampled Oboe and French Horn

Main Edit adds `sampled_oboe` (ID 31, Bb3–F6 / MIDI 58–89, nine recorded roots) and `sampled_french_horn` (ID 32, C2–C4 / MIDI 36–60, eight roots). Both are sustained acoustic recordings with two genuine soft/strong layers, not synthesizer imitations. Notes outside the advertised range are silent. Horn recordings outside the paired-layer range are intentionally excluded.

## Playback and expression

Both voices reuse the resident shared sampler. MIDI duration controls sustain and release; velocity changes recorded timbre and loudness. Level, Attack, Release, Dynamics and Velocity Timbre are the five supported controls. Existing track/clip gain and pan remain available; Dynamics and Velocity Timbre can be automated during held notes. Unsupported synthesis controls stay disabled with a reason. User presets retain the existing v5, 29-parameter schema.

Original stereo 16-bit/44.1 kHz WAVs are pinned to VSCO 2 Community Edition commit `440300901dfe9275fd84e0b7763af1f8443ae62e` under CC0. Each bank retains LICENSE and source SHA/URL provenance. Prepared mono 24-bit files center pitch and align the stronger take's sustain clock with the soft take, guided by a reliable shared harmonic. This accommodates the horn's weak fundamental. The original attack is preserved before the 300 ms alignment reference; alignment smoothly reaches full strength at 800 ms. Original source files remain untouched. Derived hashes and source links are recorded in `prepared/provenance.json`.

Common loops use 120 ms overlap for oboe and 240 ms for horn, qualified seam correlation, per-root bounded energy/covariance curves, and the existing continuous dynamics gain correction. No additional DSP arithmetic is introduced for the six established voices. Attack defaults are 8/12 ms and release defaults 160/220 ms for oboe/horn respectively. Sustains are the initial articulation; staccato, legato transition recordings and mutes are future sample material.

## Preparation and verification

With NumPy available, run `python tools/prepare_oboe_horn_samples.py assets/instruments`. It processes only the two new banks. `python tools/verify_oboe_horn_assets.py assets/instruments` validates originals, derived files, complete layer pairs, range, loops, generated maps and energy descriptors. Existing flute/cello tools keep their original default bank selection.

Run `make test-sampled-oboe-horn test-instrument-controls test-sound-inspection test-session test-mcp-sampled-oboe-horn`. Sampler tests cover every supported pitch at 44.1, 48 and 96 kHz, long holds, blend energy, finite overlap/retrigger, release silence and callback partition parity. MCP covers discovery, guarded apply/replay, supported automation, saved presets, WAV preview and project recall. The audition chord now transposes its whole C-major voicing by octaves into each bank's range; live preview, WAV export and analysis note metadata share the same selection. Existing project renders retain their old note placement.

Editable audition projects and production WAVs are in `daw/artifacts/sampled-oboe-horn/20261012/`. Each demo covers velocity layers, held-note Dynamics expression and a range traversal without normalization. These are listening deliverables, not edits to the user's song.

## Oboe/horn stabilization

Two oboe roots (F4/MIDI 65 and Bb4/70) and three French horn roots (Eb2/39, G2/43 and A3/57) use refined common sustain loop boundaries. Overlap lengths remain 120 ms for oboe and 240 ms for horn. Replacement loops must improve the weaker seam by at least .03 correlation, stay within the existing sustain-stationarity tolerance, and pass before/after blend-energy qualification. Other loops are retained. Per-root energy curves are regenerated for the exact new playback geometry; the existing .75–1.35 correction bounds, recorded attacks, source pitch correction and attack/release defaults remain unchanged.

The production sampler now has tighter oboe/horn blend tests: every supported pitch must stay within .95–1.10 of expected RMS energy in the checked sustain windows. Per-pitch tests also verify note-off has no instantaneous level discontinuity, zero-level retrigger onset and exact silence after a short note's release at low, medium and high velocities. Existing overlapping-retrigger, callback-partition and multi-rate tests remain active. This pass does not add sampled legato articulations or alter the other six instruments.

`prepare_oboe_horn_samples.py` includes qualified loop refinement. To reproduce only that stage from the stabilized inputs, run `python tools/refine_flute_cello_samples.py assets/instruments --banks vsco2_oboe_v1 vsco2_french_horn_v1 --refine-new-winds`. The original default flute/cello selection and cello search profile are unchanged.

Before/after 27-second WAVs, identical editable comparison projects and per-root measurements are retained in `daw/artifacts/oboe-horn-stabilization/20261012/`. Comparisons use the same MIDI, velocity, Dynamics lane and level without normalization, including long holds on changed roots. Original and derived recorded WAV files retain their previous hashes.
