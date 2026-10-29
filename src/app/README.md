# Directory: src/app

Purpose: Application bootstrap and SDL event loop integration.

## Files
- `app_actions.c`: Validates generation-bound instrument targets, copies stored/effective settings, and applies batched track/region patches through reserved scalar undo history; continuous edits retain target and reservation ownership. See [application control](../../docs/application_control.md).
- `bounce_region.c`
  - Allocates bounce filenames under the active library root and inserts successful bounces as normal audio clips on a newly appended `Bounce` track when track creation succeeds.
- `main_bounce.c`
  - Orchestrates offline bounce range selection, progress reporting, WAV/pack writes, library refresh, and bounce-region insertion. Uses the streamed engine export path for a single deterministic PCM16 file; no implicit float sidecar. Existing UI uses cold exact-range export with normalize-if-clipping, two-pass progress, and Escape cancellation; bounded optional pack/insertion outcomes remain separately reported. See [S3.7](../../docs/improvement/S3-EXPORT.md).
- `main.c`
  - `handle_input`: Feeds the current SDL event into the input manager when available.
  - `handle_update`: Ensures layout sizing is up to date and advances input state when work is due.
  - `handle_render`: Clears the renderer, draws panes/controls/overlays, and presents the frame.
  - Wake-loop callbacks: Defines urgent-work checks, render-cadence timeout selection, wake-event filtering, background tick, render-gate, and diagnostics hooks consumed by `SDLApp` wake-blocked loop execution.
  - Input invalidation routing: Pointer events now invalidate targeted panes via pane hit-testing; global/layout events still invalidate all panes for correctness.
  - Async producer bridge: Worker-thread producers post typed main-thread messages (`daw_mainthread_message_post`) that coalesce wake signaling and drive targeted UI invalidation in the background tick.
  - Gate diagnostics mode: `DAW_LOOP_GATE_EVAL=1` with `DAW_SCENARIO=idle|playback|interaction` emits pass/fail threshold checks from loop diagnostics windows.
  - Loop diagnostics JSON mode: `DAW_LOOP_DIAG_FORMAT=json` (or `DAW_LOOP_DIAG_JSON=1`) emits schema-1 `LoopDiag` lines for cross-program sleep/wake calibration parity.
  - Gate harness: run `daw/tools/run_loop_gates.sh` to execute all gate scenarios and emit a summarized `pass/fail/inconclusive` report with scenario logs.
    - headless validation: set `HEADLESS=1` for no-display/no-swapchain loop gate checks.
  - Visual artifact proof: `DAW_VISUAL_ARTIFACT_ONCE=1` requests a first-frame
    capture and exits after the artifact is written or fails.
  - Quick commands:
    - `make -C daw loop-gates` (default profile, inconclusive => exit 2)
    - `make -C daw loop-gates-strict` (strict profile, inconclusive => failure exit)
  - `main`: Loads config, restores the last session from `config/last_session.json` (or seeds defaults), initialises UI subsystems, configures wake-loop policy, runs the SDL framework loop, and auto-saves the session on shutdown.
- `visual_artifact_proof.c`: Env-gated one-shot first-frame capture used by the
  R6 `visual-artifact` target.
- Shutdown stops engine producers before destroying UI wake/message/job infrastructure.
- Recording failures retain captured audio for Finish retry or explicit cancellation/recovery. Cancel quiesces the capture callback before releasing its queue; failed capture startup clears armed state while preserving its error. SDL dummy tests cover both raw and clock-aligned concurrent capture.
- `audio_recording.c`: DAW-local audio recording coordinator. It owns the capture queue, worker-owned timeline journal drain, bounded recent preview, streamed WAV finalization path, media registry registration, and normal audio-clip insertion while leaving shortcut/UI policy to input modules.

Recording setup checks engine arming acceptance before advertising an active take. Rejection releases the prepared queue and reports a retryable error; device-backed setup also closes its newly opened endpoint. The recording test injects arming rejection and proves cleanup/retry.

Audio takes retain a stable target-track identity. Finalization resolves its current index, or reports a removed target while retaining the captured take without insertion. Reusing the former array index does not redirect an existing take.

Space now pauses/resumes at the callback-delivered timeline cursor; the Stop button returns to frame zero. Timeline and transport presentation use the bounded callback-based clock estimate rather than the worker's render-ahead cursor. Physical output latency and capture alignment are not inferred from that estimate.

`audio_recording.c` now transfers timestamped capture packets, preserves overflow intervals as silence, checkpoints a recovery journal on drain, and retains failed takes. Timeline takes stop accepting audio across transport discontinuities and require a non-looping project-rate capture endpoint. `main.c --recover-take JOURNAL OUTPUT.wav` recovers a verified prefix without opening the desktop. See [S2.5](../../docs/improvement/S2-RECORDING-DURABILITY.md).

S4.6 moves actual timeline checkpoint I/O off the UI loop, joins the storage worker before finalization/cancel, and publishes fixed preview snapshots. Raw/synthetic callers retain explicit synchronous draining. The recent waveform represents only the newest 65,536 frames while clip extent follows the complete take. File bounce uses an anonymous spool and bounded pack overview; completed-WAV insertion still uses the full-file media cache. Separate queue/clock observations now include `clock_continuity_frames`: an anchored take retains samples through busy diagnostic reads only when its independently published epoch is still current. Transport changes halt the take. See [the clock correction](../../docs/improvement/S4-CAPTURE-CLOCK.md). See [S4.6](../../docs/improvement/S4-STREAMING.md).


S4.7 `media_import.c` owns application-lifetime requests across project candidates. Library drops, interactive recording finish and bounce insertion publish asynchronously; saved WAVs survive failed/canceled insertion. Ctrl/Cmd+Escape cancels pending requests. Raw/headless recording without this owner and explicit offline APIs retain synchronous behavior. See [S4.7](../../docs/improvement/S4.7-MEDIA.md).


`control_bridge.c` owns the opt-in same-user Unix endpoint and request mailbox;
its transport thread never reads the engine. `control_dispatch.c` validates
strict instrument requests, exact-state preconditions and retained request IDs
on the application thread. `main.c` polls the bridge in the normal background
slice and joins transport before teardown. The endpoint pointer survives project
candidate replacement while targets remain engine-generation bound. See
[the control contract](../../docs/application_control.md) and
[MCP setup](../../tools/mcp/README.md).

Custom preset storage is owned by `preset_library.c`; `preset_actions.c` reuses instrument target/state/history contracts, `preset_preview.c` renders bounded private PCM examples, and `control_presets.c` adapts those owners to the existing receipt dispatcher. No shared-library implementation or dependency minimum changed. See [user presets](../../docs/user_presets.md).

`sound_render.c` shares isolated production rendering between WAV and inspection; `sound_analysis.c` owns calibrated fixed-window measurements/common actions; `control_analysis.c` adopts them through two read-only MCP tools. See [sound inspection](../../docs/sound_inspection.md).

`midi_actions.c` owns copied MIDI/tempo snapshots, exact-state append/replace transactions and guarded history replay; `midi_patterns.c` generates bounded four-beat major-key phrases through the captured tempo map. `app_actions.c` provides their common identity resolver. See [transactional MIDI](../../docs/midi_authoring.md).


`control_midi.c` adopts common MIDI snapshots, note/pattern actions and guarded history through exact retained-state tokens and bounded pages. The existing dispatcher reserves immutable receipts before MIDI writes. See [MIDI MCP](../../docs/midi_mcp.md).

### Guarded MIDI region authoring

Common MIDI layout/create/extend actions and structural control routing live in `midi_regions.c` and `control_regions.c`. They reserve history before one publication and reuse the existing mailbox and receipt ledger.

### Effects authoring development

`fx_actions.c` owns guarded complete-chain actions/history; `control_fx.c` adopts them into MCP. `composition.c` captures production range/track/region previews and calibrated analysis without modifying authored state.

`control_composition.c` owns a four-render/64 MiB immutable audio cache and adapts production composition analysis and receipt-backed captured PCM publication.

`composition_preview.c` publishes exact retained production audio for MCP and native inspection. `fx_library.c` stores strict versioned portable effect chains with revision-bound atomic updates; its MCP/editor adoption is implemented.

`fx_preset_actions.c` owns guarded library save/recall and `control_fx.c` adopts list/get/save/apply. `midi_expression.c` owns tempo-aware quantize/swing, velocity scaling and seeded bounded variation through common MIDI history; `control_midi.c` exposes `transform_midi`.

Project float serialization now preserves complete authored float values through decimal JSON; expression/library native restart qualification covers exact notes and processed WAV bytes.

Guarded arrangement is documented in [arrangement](../../docs/arrangement.md). The common action, MCP adapter and native MIDI duplicate/delete share exact content guards and one-step batch history; native Arrange fields and actual-window acceptance now pass; advanced routing remains pending.

`routing_actions.c` owns common exact-state routing edits and scoped history. It validates the complete routing graph, refuses busy/foreign/stale requests, reserves independent before/after metadata and publishes once. Routing restoration preserves unrelated mixer, regions and processors. Native routing controls and `control_routing.c` adopt the common guarded action/history path.

Common routing now includes one-publication return creation from an empty project with stable-identity redo and created-lane later-edit guards. `control_routing.c` adopts reads, creation, atomic role/send batches and scoped history behind four copied tokens and the existing immutable receipt/write admission contract. Native routing controls and nested group management are implemented; see `docs/routing.md` and `docs/track_groups.md`.

`automation_actions.c` prepares complete automation snapshots before engine publication and shares region point commands with graphical input and MCP adapters.

Sampled Oboe and French Horn extend the existing acoustic sampler with paired recorded dynamics and held-note expression. Complete audition chords follow each bank's playable range. See `docs/sampled_oboe_horn.md` for ranges, provenance, preparation and verification.

[Header mixer and lane density](../../docs/header_mixer.md) adds guarded gain/pan scalar history, standard relative gestures, exact-value inspector synchronization, smaller gutters and subtle persistent lane tints. The application mixer/history boundary and input gesture owner are separate small modules.

`track_controls.c` owns copied header state, field-scoped guarded writes and stable-label/scalar history; `control_track_controls.c` adapts the existing endpoint tokens/receipt admission. Native rename and MCP label edits share reservation before publication.

`eq_actions.c` owns copied dedicated curves, exact lifetime/value publication guards and native/MCP shared history; `control_eq.c` adopts bounded tokens and existing receipt admission. `daw_composition_measure` adds bounded independent channel calibration without changing source audio.

`mix_capture.c` owns exact common project guards and bounded immutable per-channel graph measurements; `control_mix_capture.c` provides read-only guard/capture/page tools. See [mix diagnostics](../../docs/mix_diagnostics.md) for stages, units, context and capability limits.

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

Asynchronous whole-song WAV export now uses a captured production graph and shared worker, with native/MCP format/tail choices, progress, cancellation and verified no-overwrite output. See [export contracts](../../docs/song_export.md).

Qualified live 400 ms per-channel/stereo/mono compatibility windows now cover child/group/return/master output, with native scope controls and guarded output-only stereo/mono monitoring. The approximate live LUFS view is hidden behind an explicit qualification notice; offline validated LUFS remains available. See [live metering contracts](../../docs/live_stereo_metering.md).
