# Agent music capabilities

Current Main Edit reference, verified headlessly 2026-10-13. This document supersedes historical tool counts and adoption forecasts in earlier slice reports. It describes implemented application/MCP behavior, not a claim that canonical source, every installed app or every chat exposes the same version. Read live capabilities and tool discovery first.

## 1. Creative surfaces

| Surface | Current behavior | Limits and distinctions |
| --- | --- | --- |
| Instrument design | 39 factory voices, 32 named physical parameters; inherited track sound or region-local override; complete reusable user presets | Visible controls reflect the voice's DSP; dormant values can be stored without audible influence |
| MIDI creation | Empty region on existing track, or atomically one new trailing track and region; guarded right-edge extension | Does not create/open a project; nonoverlapping placements; extension uses new total duration |
| Notes | Explicit append/replace, deterministic major-key melody/chord/arpeggio shortcuts | Integer pitches 0–127; up to 64 supplied events/write, 4096 notes/region; multi-request edits are not atomic |
| Expression | Quantize/swing, velocity scaling, seeded timing/velocity variation | Whole untrimmed region; durations remain fixed frame lengths; boundary/collision refusal; humanize is cumulative |
| Arrangement | Guarded duplicate, move, right-edge trim and delete of MIDI regions | Up to 16 operations/batch; trim refuses discarded notes; destination track owns serial effects/routing |
| Dedicated native EQ | `get_eq`, `edit_eq`, `undo_eq`, `redo_eq` for child/group/return/master; complete curves, native DSP and scoped history | Four peaking bands, fixed 12 dB/oct cuts; Hz/dB/octave width; exact curve/lifetime guards; stopped writes; separate from EQ inserts |
| Effects | Discover 54 registered types; ordered track/master/return insert chains; complete portable chain save/recall | Up to 16 inserts/chain; physical/tempo-synced units; add appends; no region-local chain |
| Track/group mixer and labels | `get_track_controls`, `set_track_control`, `undo_track_control`, `redo_track_control`; names, base gain/pan and truthful mute/solo readback | One field/write; linear gain 0–10 (0 silence, 1 unity), pan -1–1; names 1–127 UTF-8 bytes without control characters; playback allowed, recording/conflicting owners refused |
| Nested group buses | Create, parent, order and fold/expand; child/group inserts and serial routing; combined MIDI/audio source overviews | Acyclic hierarchy; fold keeps audio and source placement unchanged; processed group-waveform cache, group pan and return/master fader automation are outside this surface |
| Sends/returns | Return creation and guarded track-role/post-fader send editing | Four returns, four slots/source, linear gain 0–1; no return feedback, pre-fader sends or external sidechain selection |
| Production mix diagnostics | Exact guarded child/group/return/master captures; channel and insert levels/reduction, frequency windows and source/note-ending diagnostics; programme LUFS/LRA/true peak through `capture_mix(whole_song=True)` | Detailed passages ≤30 s; whole-song arrangement ≤600 s plus explicit tail ≤30 s; mono/stereo 44.1/48/96 kHz loudness; immutable read-only analysis, not a live meter or live export delivery |
| Analysis | Isolated instrument/library sound; captured production project/track/region audio, dry/processed | Composition material ≤10 s and tail ≤5 s; four captures within 64 MiB; per-channel sample peak/RMS/dBFS and separate full-scale/overload counts; immutable capture, not live/LUFS/true-peak; plots do not judge musical preference |
| Audio publication | Captured composition or standard preset WAV preview | Publishes an immutable capture; original level; clipping refuses, no automatic normalization; not a full-song export job |
| Reuse/persistence | Native collection-scoped Load/Save and versions; projects embed names, controls, complete sounds, groups, chains and routing; libraries remain independent | Select a collection before native Load/Save; Save updates the selected project and Save Version creates a checkpoint. No project-file MCP commands; library update/deletion does not alter project sound |

## 2. Musical reach

Explicit note authoring supports major, minor, modal, pentatonic and other twelve-tone pitch collections, chord voicings, riffs and rhythmic patterns. The built-in pattern generator is narrower: major tonic, four quarter-note beats, 85% gates, fixed region bounds, untrimmed source. Four quarter notes only equal one bar in appropriate meters. Timing derives from captured sample rate and tempo map, not a fixed frames-per-bar constant.

Dedicated Kick/Snare/Closed Hat/Open Hat/Hat Kit and Sub/Driven 808 provide one-shot and pitched bass behaviors. Overlapping mono 808 notes glide; gaps retrigger. Hat choke operates across participating regions on the same track; both sounds must opt in. Individual hats ignore pitch, whereas Hat Kit maps 42/44 closed and 46 open. See [percussion behavior](percussion_synth.md).

Pitch conversion currently uses twelve-tone equal temperament with A4 at 440 Hz. Integer note payloads have no per-note tuning/pitch-expression fields. Detuning a second oscillator and gliding a mono 808 do not establish independent microtonal tuning. Saved tuning definitions, scale-degree/frequency mapping, per-note pitch curves, editor inspection and MCP adoption remain future application work.

## 3. Reliable control

The source server and currently exposed namespace provide 70 tools. This count is descriptive; future clients must discover actual tools rather than enforce it. All writes use current endpoint instance, immutable UUID request identity and the addressed domain's exact token. Header-control tokens check only the addressed name/gain/pan field and target lifetime; unrelated mixer/native edits remain owned by their callers. Sixteen copied track-control states are retained; refresh expired tokens. Tokens cover their domain, not a whole-project revision. Edits share native application actions/history. Check `ok` and `code` and independently read accepted state.

The 128-receipt ledger includes rejected/no-op attempts and does not evict. Recover uncertain writes with `request_status` under the original instance/ID. Never retry an uncertain action under a new ID. Tokens/receipts expire on restart; reconcile project state before deliberate new work. Shared history cannot skip a different domain command at the cursor. Structural/note authoring refuses playback/recording/Test and conflicting gestures. Header controls and fold-only group changes permit playback while retaining recording and conflicting-owner guards.

Note frames and persistent target identities use exact decimal strings. Read all pagination streams using their first token. Larger note replacements require bounded replace/append steps with fresh state and multiple undo commands; they are not one transaction.

## 4. Boundaries

This MCP surface does not expose project new/open/save, transport, general audio import/download, track-wide and arbitrary effect-parameter automation writing, track deletion,  per-note pitch bend or tuning tables. Existing UI/engine functions and private test/export helpers are not automatically remote tools. The setup guide documents endpoint opt-in; do not replace client configuration or restart a user-owned editor solely to satisfy a recipe.

Production composition capture includes source/mixer/automation/inserts/EQ/routing. Dry capture bypasses inserts and dedicated EQ without changing the project and excludes duplicated return processing; it is not a unity-gain source audition. Region-isolated hats retain track choke triggers. Peak/RMS/DC and bounded tonal plots prove signal properties, not LUFS, physical-device acceptance or aesthetic quality.

## 5. Usage references

- [MCP setup](../tools/mcp/README.md), [MIDI control](midi_mcp.md), [region creation](midi_regions.md), [expression](midi_expression.md), [arrangement](arrangement.md).
- [Instrument controls](instrument_controls.md), [filtered voices](filtered_synth.md), [user presets](user_presets.md).
- [Dedicated EQ and channel calibration](dedicated_eq_mcp.md), [Effect chains and captured audio](effect_chains.md), [routing](routing.md), [nested groups](track_groups.md), [header controls](header_mixer.md), [collection Load/Save](project_collections.md).
- The reusable `sonics-music-authoring` skill provides composition, percussion, effects and MCP guidance. Repo-local package: `CodeWork/skills/sonics-music-authoring`; installed copy: `~/.codex/skills/sonics-music-authoring`. Automatic discovery is enabled; explicit invocation is `$sonics-music-authoring`. A current chat may need a later skill discovery refresh to list a newly installed package.

Source qualification, installed endpoint behavior, project persistence and human listening are separate evidence layers. Main Edit source/server and read-only endpoint discovery established this documentation snapshot. Creating the skill changes neither source DSP nor the current musical project.

Region automation is adopted by `get_automation`, `set_automation`, `undo_automation` and `redo_automation`. Volume/pan use physical units; instrument lanes use offsets. Advertised instrument targets follow the effective voice capability metadata. See [automation](automation.md).

Group faders and bound source/group sends have project-frame physical gain curves through `edit_routing`, native Routing → Auto and shared routing history. Limits: 32 points/lane and 256 total; stopped edits; native project persistence and complete immutable capture provenance. See [mixer automation](mixer_automation.md).

Selected insert automation supports Gain dB and Pan position through native FX Auto and `edit_effect_chain(action="automation", parameter=0)`, with project persistence, complete readback and shared FX undo/redo. Stops for authoring, 32 points/insert and 256 effect points/project; `list_effects` reports actual `automation_supported`. Other effect targets remain unsupported. See [qualified effect curves](effect_parameter_automation.md).

Gain-matched A/B listening is available through the native effects toolbar and four guarded MCP tools. Captures are ephemeral, preserve authored gains/history and use equal all-channel RMS with attenuation-only sample-peak protection. See [A/B listening](ab_listening.md).

Whole-song WAV export is now available through `start_song_export`, `get_song_export` and `cancel_song_export`. One hour material, sixty seconds exact tail, PCM16/float32 at captured project rate; one active job and eight ephemeral retained result records. Verified files persist without project insertion. See [export workflow](song_export.md).

Qualified live 400 ms per-channel/stereo/mono compatibility windows now cover child/group/return/master output, with native scope controls and guarded output-only stereo/mono monitoring. The approximate live LUFS view is hidden behind an explicit qualification notice; offline validated LUFS remains available. See [live metering contracts](live_stereo_metering.md).

Compressor/Limiter controls now have qualified non-consuming live gain-reduction feedback in native headers and `get_effect_chain.dynamics_feedback`, separate from authored edit tokens. Compressor detection remains independent by channel; Limiter is sample-peak, not true-peak. See [dynamics controls and feedback](dynamics_feedback.md).
