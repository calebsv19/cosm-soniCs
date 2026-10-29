# Percussion and 808 instruments

## 1. Sound design and controls

Seven appended factory voices provide dedicated percussion rather than short gates on melodic oscillators. Existing preset IDs and the rendering of earlier voices remain unchanged.

| Preset | Role | Useful starting notes |
| --- | --- | --- |
| Kick Drum | Sine body, downward pitch sweep, short click, drive | C2; short trigger |
| Snare Drum | Resonant body plus independent colored noise | D3; short trigger |
| Closed Hat | Bright colored noise with short decay | Any pitch; short trigger |
| Open Hat | Longer independent noise decay | Any pitch; short trigger |
| Hat Kit | GM closed/open hats in one choke group | 42/44 closed; 46 open |
| Sub 808 | Long pitched sine body with restrained saturation | C2/A1; held bass notes |
| Driven 808 | Same pitched body with stronger saturation | C2/A1; held bass notes |

Kick, snare and hat are one-shot voices: the hit completes after a short MIDI trigger. Their tails remain bounded by the MIDI region, so leave room at its end. Both 808 voices follow MIDI pitch and note duration, with a separate note-off release. Mono defaults off for the existing 808 voices, preserving earlier sounds. Enable Mono for one last-onset-wins voice; overlapping notes glide while separated notes retrigger. Individual hat voices ignore MIDI pitch, while Hat Kit uses its note map.

The percussion physical parameters are stored in the complete sound snapshot:

| Parameter | Range | Effect |
| --- | --- | --- |
| Body Decay | 5–4000 ms | Time for the pitched body to reach -60 dB |
| Pitch Drop | 0–48 semitones | Initial pitch offset above the final MIDI pitch |
| Pitch Decay | 1–500 ms | Exponential pitch-sweep time constant |
| Noise Mix | 0–1 | Snare body/noise balance |
| Noise Decay | 5–1500 ms | Independent snare/hat noise tail |
| Click | 0–1 | Short onset transient on kick, snare and 808 |
| Mono | 0/1 | One last-onset-wins 808 voice instead of layered notes |
| Glide | 0–1000 ms | Logarithmic pitch glide on overlapping mono 808 notes |
| Hat Choke | 0/1 | Cut earlier hat tails when another hit starts in this region |

Level, Tone and Attack also work on these voices; kick/snare/808 expose Drive and 808 exposes Release. The browser and MCP advertise only controls that affect the selected voice. Tone changes body harmonics and noise brightness. The instrument pane displays an onset waveform and the dominant body/noise envelope through the production sampler. Drums have their own browser category; 808 voices appear in Bass.

## 2. Rendering and ownership

`instrument_percussion.c` implements the app-local DSP using an analytically integrated frequency sweep and deterministic frame-indexed noise. Rendering requires no heap allocation or mutable noise history and reproduces the same result across seeks and arbitrary render blocks. Body/noise envelopes taper to exact silence. The 808 note-off endpoint is sample-counted and exactly silent. Saturation is not oversampled and is not claimed to be alias-free.

Existing parameter metadata, automation, application actions, gestures, history, project persistence, factory browsing and user-library controls carry the new voices. No shared library or public tool name was added. The control surface remains 45 tools; capabilities now describe 25 factory voices and 27 parameters. Read capabilities and current target state before a guarded write.

User library files now write format 4 with all 27 parameters. Readers retain format 1 (14 parameters), format 2 (18 parameters) and format 3 (24 parameters), initializing absent fields from the voice defaults. Older formats cannot claim the new voices. Projects embed complete settings; modifying a library preset cannot change an existing project. Numeric preset, parameter and automation identities are appended rather than reordered.

## 3. Qualification

1. `make test-percussion-render test-instrument-articulation`: production/diagnostic parity at 44.1, 48 and 96 kHz; short gates, seek/block consistency, exact tails, independently integrated pitch and audible control applicability.
2. `make test-instrument-controls test-session test-preset-library test-midi-scheduling`: previous voice parity, all-parameter persistence, older library formats and scheduling.
3. `make test-mcp-control test-mcp-presets test-mcp-percussion test-mcp-articulation`: real SDK discovery and guarded actions, exact undo/redo audio, factory recall, user preset save/read and exact project-reopen audio.
4. `make run-headless-smoke`: broader non-interactive regression lane. Percussion rendering is included in `test-stable`.
5. `make package-desktop-main-edit-self-test`: separate development-package identity and launcher verification; never implies installed-app refresh or physical-device listening acceptance.

Listening examples and the editable five-track fixture project are retained under the canonical checkout's `artifacts/music/percussion-20261010/`. The eight-second 120 BPM groove combines kick, slightly delayed backbeat snare, alternating eighth-note hats, and clean/driven C/A bass. The two 808 tracks are comparison layers, not a claim that every mix should stack them. The mix is unnormalized, with float peak 0.61290139 and RMS 0.0949078597. Subjective sound quality remains a listening decision.

## 4. Next bounded refinements

Percussion-specific independent noise color/filter controls, 808 sustain/glide curves and independently named choke groups would extend this foundation. Multi-sample kits, velocity-layer sampling, oversampled drive and hardware emulation remain future work. Existing sessions and the currently running installed app are not migrated by this source slice.

## 5. Mono glide and hat choke scope

The 808 voice retains oscillator phase, the initial pitch-drop envelope and body decay during a legato chain. Glide is linear in semitones (exponential in Hz), with analytic phase integration. A 5 ms velocity ramp avoids steps during rapid overlapping changes. Zero glide steps pitch while preserving phase; simultaneous notes select the last authored note without a fictitious glide. Earlier held notes do not resume after a newer note ends. A gap retriggers attack and pitch drop, with a short pre-transition fade for an earlier release tail.

Hat Kit maps note 46 to Open Hat, and other notes to Closed Hat; use GM 42/44 for clarity. Noise Decay controls the open tail; closed hits cap it at 80 ms. With Hat Choke enabled, active hat regions on the same track share one group and the next participating hit fades the prior hit over the final 2 ms. Open Hat and Hat Kit tails can continue across a region edge; closed hats opt into this tail behavior when Hat Choke is enabled. Notes outside a region, zero-velocity notes and inactive regions do not trigger choking. Region gain and automation attenuate the sound without removing group participation. Separate tracks have independent groups. Both participating regions must enable Hat Choke. Simultaneous hits select the greatest persistent region creation identity, then the last authored note in that region. The previous Closed Hat preset defaults choke off for compatibility. The new Open Hat and Hat Kit presets default it on.

Mono, Glide and Hat Choke are prepared articulation settings edited through UI/application actions/MCP and fully saved in projects and user presets; this slice does not add automation lanes for these three settings. Existing pitch-drop automation is latched at the start of a mono legato chain; level, drive and body controls retain their frame-based handling. An isolated waveform preview or fixed-note preset audition cannot demonstrate a multi-note glide or choke; use the saved pattern or listening examples.

The articulation examples are under `artifacts/music/articulation-20261010/` in the canonical checkout. The four-track C/A bass groove includes open hats closed by subsequent hits. Separate WAVs compare glide with zero glide and polyphonic playback, and choked with unchoked hats. No installed-app replacement or existing-song edit is part of these source checks.

## 6. Track-wide hat routing

Track choke context is prepared with each immutable source revision and copied into per-note fences. Playback performs no global event search, allocation or mutable cross-region voice sharing. Effective track-inherited and region-local settings use the same existing instrument actions. Moving, retiming, deleting or changing a participating region rebuilds that context; captured renders retain their original sound after later edits.

Region-isolated composition analysis/preview includes the other participating hat onsets on its track, even when those other voices are not rendered. A track stem preserves the same interaction; another track cannot choke it. An export excludes all trigger onsets at or beyond its selected input end, including across regions, so excluded notes cannot silence a release tail. Playback looping uses authored absolute timeline events and does not carry a tail from the previous loop iteration into the next.

Use **Open Hat** in one region and **Closed Hat** in the following region, enable **Hat Choke** on both, then trigger the open note shortly before the first region ends. A closed note after the boundary cuts the open tail. The legacy Closed Hat default remains choke off, and its old region-bounded audio remains unchanged until explicitly enabled. No project schema, preset ID, parameter ID or MCP tool changes are needed.

`make test-track-hat-choke` checks production source-plan routing at 44.1/48/96 kHz, adjacent boundaries, isolated previews, export cutoffs, region edits, captured lifetime, simultaneous ordering and seek/partition/loop parity. `make test-mcp-track-hat-choke` verifies eight adjacent regions on one track through the real SDK/native bridge, exact audio undo/redo and project reopen. Listening examples and an editable four-track project are retained in the canonical checkout at `artifacts/music/track-hat-choke-20261010/`.
