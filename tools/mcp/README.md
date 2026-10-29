# soniCs local MCP

Current Main Edit surface: 54 tools, 39 factory voices and 32 physical instrument parameters. Instruments/presets, MIDI creation/notes/expression/arrangement, serial effects/chain libraries, nested buses, guarded child/group names and base mixer controls, post-fader sends/returns, isolated analysis and production composition captures are implemented. Read [current capabilities](../../docs/agent_music_capabilities.md) for limits and gaps; historical slice reports are not the current tool inventory.

## Setup and exact-state operation

1. Install the isolated SDK environment from the repository root (Python 3.11+):

   ```sh
   python3 -m venv build/mcp-venv
   build/mcp-venv/bin/python -m pip install -r tools/mcp/requirements.lock
   ```

   The lock records the complete tested Python 3.14/macOS dependency set, including
   official `mcp==1.30.0`. Other platforms/Python versions require their own qualification.

2. Build the DAW, then launch the resulting binary with an explicit private endpoint.
   The example uses the verified task build; the default `make` binary lives under
   `build/targets/macOS-arm64/toolchains/clang/bin/daw_app` on this host.

   ```sh
   control_dir="/tmp/sonics-control-$UID"
   mkdir -p -m 700 "$control_dir"
   export DAW_CONTROL_SOCKET="$control_dir/control.sock"
   DAW_CONTROL_ALLOW_WRITE=1 build/mcp-midi-20261009/targets/macOS-arm64/toolchains/clang/bin/daw_app
   ```

   Omit `DAW_CONTROL_ALLOW_WRITE=1` for read-only access. Without
   `DAW_CONTROL_SOCKET` the app creates no endpoint. Startup refuses an existing
   socket/path, an unsafe parent directory, or a socket path of 104 bytes or more.
   Clean shutdown removes only the socket inode created by that app instance.
   After a crash, establish that the old app is stopped before removing its stale socket.
   The endpoint grants same-user clients access to the enabled tools; it is not a
   per-client approval or isolation boundary. No TCP listener is created.

3. Generate the client configuration with the **same** endpoint environment:

   ```sh
   python3 tools/mcp/client_config.py
   ```

   This prints a common `mcpServers` JSON example with absolute paths. Adapt it to
   your client's stdio MCP configuration; the generator does not edit global
   client settings. Configure/restart the client to discover tools. Merely building
   this repository does not add new callable tools to an already-running chat.

4. Use the tools in this order:

   - `capabilities`: get endpoint instance, presets/defaults, parameter ranges and
     which controls are visible for each preset. Visibility is UI metadata, not a
     promise that every DSP parameter has equal audible influence in every preset.
   - `inspect_project`: obtain track and MIDI-region targets. Page with `next_offset`
     until it is `-1`; repeat discovery if the user changes arrangement structure.
   - `get_instrument`: read stored and effective settings and `expected_state`.
   - `set_instrument`: supply capability `instance`, a fresh UUID `request_id`, the
     copied target, `expected_state`, optional preset ID, and parameter dictionary.
     One accepted batch produces one undo entry; an exact no-op produces none.
   - `undo_instrument` / `redo_instrument`: supply a fresh request ID and current
     expected state. Only a matching instrument command at the history cursor is
     eligible; these cannot undo unrelated edits or skip history commands.
   - `request_status`: recover the terminal response using the original instance
     and request ID after a timeout or disconnect. Identical set/history calls
     replay their original result; changed payloads with the same ID reject.

   Every domain response has `ok` and `code`; check both. Protocol/connection errors
   use MCP errors. `expected_state` is an opaque canonical copy of the addressed
   instrument state, including endpoint identity and effective inheritance, **not** a whole-project
   revision or an edit counter. A change away and back to identical state matches.
   Region movement/removal and project replacement invalidate old targets.

   Receipts are in memory, limited to 128 admitted mutation attempts (including
   rejected attempts). They never evict earlier outcomes. A full ledger refuses
   new IDs until a new endpoint lifetime; already-retained IDs remain queryable.
   Restart changes `instance` and loses receipts. A stale instance cannot prove
   whether a previous edit was saved; reconcile from the current project instead
   of automatically retrying. Replay returns historical accepted state, so use a
   fresh `get_instrument` to inspect current state after later changes.

5. Run the integration test after SDK setup:

   ```sh
   make BUILD_DIR=build/agent-actions-20261008 test-mcp-control
   ```

   This negotiates MCP with an official SDK client against the production bridge
   and engine in an isolated C fixture. It needs permission to bind local Unix
   sockets. It does not open user projects, audio devices, or an installed app.
   The ordinary headless smoke suite remains independent of Python dependencies
   and local socket permission. See [application control](../../docs/application_control.md).

## Creative workflow references

- [Current capability map](../../docs/agent_music_capabilities.md).
- [MIDI notes/patterns](../../docs/midi_mcp.md), [region creation/extension](../../docs/midi_regions.md), [expression](../../docs/midi_expression.md), [arrangement](../../docs/arrangement.md).
- [Instrument controls](../../docs/instrument_controls.md), [filtered voices](../../docs/filtered_synth.md), [percussion/808/glide/hat choke](../../docs/percussion_synth.md).
- [User presets](../../docs/user_presets.md), [effects and captured audio](../../docs/effect_chains.md), [routing](../../docs/routing.md), [isolated sound analysis](../../docs/sound_inspection.md).

The optional `sonics-music-authoring` skill connects these operations with composition, percussion, effects and listening guidance. Complete project sound settings remain embedded independently of library files. Its recipes do not create new tools or grant permission to replace projects or change client settings.

## Qualification

Use focused tests for the changed domain, such as `test-mcp-midi`, `test-mcp-percussion`, `test-mcp-articulation`, `test-mcp-track-hat-choke`, `test-mcp-fx`, `test-mcp-composition`, `test-mcp-fx-library` and `test-mcp-routing`. Use fresh task-owned build/shared inputs: ordinary shared-library rules recursively clean their selected shared source build directories. Do not run broad cleanup over retained evidence. `run-headless-smoke` is broader non-interactive proof; native save/reopen, installed discovery and physical listening remain separate.

Analysis is read-only; WAV publication and library saves require write admission. Project open/save/transport, general audio import, arbitrary automation are not tools on this surface. No automatic app/client restart or global configuration update is part of this guide.

## Track and group controls

Use `get_track_controls` with a stable track target to read its name, base gain/pan, mute/solo and bus roles. `set_track_control` changes one `name`, `gain` or `pan` field using the copied `expected_state`, endpoint instance and a new request UUID. Gain uses linear amplitude 0–10 (1 unity); pan uses stereo balance -1–1. Names accept 1–127 UTF-8 bytes without control characters. Matching `undo_track_control` and `redo_track_control` cannot skip unrelated history. These tools apply to source children, groups and returns; unrelated fields remain unchanged. Refresh an expired state token before a deliberate new operation. See [header controls](../../docs/header_mixer.md).

A fresh official SDK session discovers all 54 tools. Existing chat catalogs can remain cached; reconnect the MCP client to rediscover additive tools. Client discovery is independent from installed endpoint capability, and setup does not automatically restart a user editor or change global settings.

## Automation control

The Main Edit source adds guarded region automation inspection, replacement, undo and redo. See [automation units, boundaries and commands](../../docs/automation.md). Reads and writes use region-local sample frames, complete arrangement guards, stable IDs, and the shared graphical command layer. `make test-mcp-automation` exercises the official SDK with isolated fixture endpoints.

Sampled violin capabilities advertise MIDI 55–96, a fixed CC0 bank and two recorded dynamics. Automation targets 26/27 are Dynamics/Velocity Timbre; preset schema v6 stores 32 parameters while v1–v5 remain readable. See `docs/sampled_violin.md`.

Sampled Trumpet and Clarinet use existing guarded sound/preset/automation tools. Capabilities expose the actual sounding range and two/three layers; no new parameter schema or automation IDs. See docs/sampled_winds.md.


Sampled Tenor Saxophone (`sampled_saxophone`) uses the same guarded tools and 32-parameter v6 schema (v5 remains readable). Capabilities advertise MIDI 44–88, 23 recorded roots and two dynamics layers. See `docs/sampled_saxophone.md`.

Sampled Flute (`sampled_flute`) and Sampled Cello Section (`sampled_cello_section`) use the existing guarded preset tools and 32-parameter v6 schema (v5 remains readable). Capabilities report their actual ranges, recorded roots, two layers and five supported instrument controls. See `docs/sampled_flute_cello.md`.

Sampled Oboe and French Horn extend the existing acoustic sampler with paired recorded dynamics and held-note expression. Complete audition chords follow each bank's playable range. See `docs/sampled_oboe_horn.md` for ranges, provenance, preparation and verification.

Sampled Bassoon and Tenor Trombone add two paired-layer recorded sustain banks through the existing sampler, supported controls and guarded preset/automation tools. See `docs/sampled_bassoon_trombone.md` for ranges, source provenance and verification.

Sampled Acoustic Piano, Viola Section, Double Bass and Violin Section extend the acoustic palette. Piano has finite velocity-selected strikes and voice-specific automation; bowed strings reuse sustain/dynamics playback. Audition notes follow each recorded range. See `docs/sampled_piano_strings.md`.

Piano expression extension: [piano expression](../../docs/piano_expression.md) describes the three piano-only controls, binary region/live sustain, 88 string modes, recorded key/pedal mechanics, 32-field v6 presets and session v25. Older preset versions 1–5 and session v24 remain readable; existing non-piano voices keep their behavior.

## Nested groups

[Track groups](../../docs/track_groups.md) extend the initial return graph with exclusive parent routing, nested bus effects, visible hierarchy, format-26 persistence and guarded group history. MCP adds `create_group` plus group/parent/fold/order `edit_routing` operations; reacquire runtime targets after reopening. Earlier return-only restrictions describe that initial slice; returns still cannot feed groups or returns.

### Track/child/group header controls

The current adapter exposes 54 tools. Discover `get_track_controls`, `set_track_control`, `undo_track_control` and `redo_track_control` after refreshing cached SDK discovery. Reads include names, linear gain, stereo balance, mute/solo and bus role; writes own one name/gain/pan field, with the existing endpoint receipt recovery and stable lifetime guards. Use a track target from `get_routing`; names and base mixer values persist in the native project. See [header mixer](../../docs/header_mixer.md) for units, ranges, field-scoped token checks, live-playback admission and history limits. Native collection Load/Save remains the project-file workflow; these are not new/open/save commands.

### Dedicated native EQ and captured channel calibration

The current source adapter exposes 70 tools. `get_eq`, `edit_eq`, `undo_eq`, `redo_eq` adopt the native dedicated EQ separately from insert EQ. `analyze_composition` adds immutable per-channel sample peak/RMS/dBFS and separate full-scale/overload counts. See [physical units, guards and calibration](../../docs/dedicated_eq_mcp.md). Refresh cached tool discovery after the candidate is installed at a saved/closed app boundary; an older running app does not acquire new bridge operations from source edits.

`get_mix_snapshot`, `capture_mix` and `get_mix_capture` add read-only exact guards and immutable in-context production graph diagnostics. These work on read-only endpoints and do not publish PCM or alter the project. See [stages, physical units, context, limits and SDK example](../../docs/mix_diagnostics.md). A prepared candidate must be installed at the existing saved/closed handoff boundary before an older normal app supports the new bridge operations.

Mix diagnostic insert pages now expose per-insert input/output levels and actual
per-channel Compressor/Limiter attenuation with output-latency alignment; see
`docs/mix_diagnostics.md` for paging, calibration and remaining measurement limits.

Immutable mix capture schema v3 adds per-channel spectral windows for processed
child/group/return/master outputs. `get_mix_capture` independently pages
`frequency_window` and `frequency_channel`; see `docs/mix_diagnostics.md` for
calibration, window coverage and remaining loudness/true-peak limits.

Immutable mix captures now include prepared note-ending controls/fences and
threshold-qualified aggregate MIDI source tails. `get_mix_capture` independently
pages eight note rows with `note_offset`; see `docs/mix_diagnostics.md` section 7.

Whole-song analysis uses the existing tool: `capture_mix(expected_mix=guard, whole_song=True, tail_frames="240000")` derives the arrangement end and requests a 5-second tail at 48 kHz; leave start/end at their defaults. Read each node through `get_mix_capture`. Programme LUFS/LRA/true peak supports ten-minute arrangements plus at most thirty seconds explicit tail. Default tail is zero. Read [full contracts](../../docs/mix_diagnostics.md#9-whole-song-integrated-loudness-and-lra); detailed passage history is separate. A cached client catalogue may need rediscovery to expose the new optional arguments. Package installation and private SDK proof do not imply a running live endpoint.

Group-volume and send-level curves use existing guarded `edit_routing` operations `volume_automation` and `send_automation`; the adapter still has 70 tools. Exact decimal frame strings, absolute gain, baseline/hold boundaries and linear/hold/smooth points are read back by `get_routing` and mix provenance. Native editing and MCP share routing undo/redo. See [mixer automation](../../docs/mixer_automation.md).

## Selected effect-parameter automation — 2026-10-13

Main Edit qualifies Gain dB and constant-power Pan position curves on child/group/return/master inserts. Native FX Auto point editing, native project persistence, shared effect-chain history and existing guarded MCP tools use the same complete physical curves. Inserts follow incoming and preceding-processor latency; static Gain ramps remain intact. See [contracts and proof](../../docs/effect_parameter_automation.md).

Gain-matched A/B listening is available through the native effects toolbar and four guarded MCP tools. Captures are ephemeral, preserve authored gains/history and use equal all-channel RMS with attenuation-only sample-peak protection. See [A/B listening](../../docs/ab_listening.md).

Asynchronous whole-song WAV export now uses a captured production graph and shared worker, with native/MCP format/tail choices, progress, cancellation and verified no-overwrite output. See [export contracts](../../docs/song_export.md).

Qualified live 400 ms per-channel/stereo/mono compatibility windows now cover child/group/return/master output, with native scope controls and guarded output-only stereo/mono monitoring. The approximate live LUFS view is hidden behind an explicit qualification notice; offline validated LUFS remains available. See [live metering contracts](../../docs/live_stereo_metering.md).

Compressor/Limiter controls now have qualified non-consuming live gain-reduction feedback in native headers and `get_effect_chain.dynamics_feedback`, separate from authored edit tokens. Compressor detection remains independent by channel; Limiter is sample-peak, not true-peak. See [dynamics controls and feedback](../../docs/dynamics_feedback.md).

Production `capture_ab` and `analyze_composition` requests use a bounded 60-second socket timeout; `capture_mix` retains 180 seconds. `request_status` also allows 60 seconds because receipt recovery can wait behind an owner-thread render. Ordinary controls remain at five seconds. A timeout/closed response is uncertain: recover with the original instance/request ID, or reconcile actual state on `stale_instance`; never automatically replay or invent a new request ID. These budgets are client waits, not render cancellation or guaranteed completion deadlines.
