# DAW functionality and next steps after the S4 acceptance work

This is the development worktree's engineering status, not a release certification. The S1–S6 labels refer to the runtime improvement plan, not older scaffold milestone names. Use the [S4 closeout](S4-CLOSEOUT.md) for the latest acceptance disposition; the [initial S4.9 report](S4.9-ACCEPTANCE.md) preserves the earlier baseline and failures.

## 1. What the program already does

soniCs is a custom C DAW with audio and MIDI regions, timeline editing, built-in instruments/effects, track and master processing, analyzers, recording, saved projects, and offline WAV exports. The runtime work has strengthened the behavior behind these surfaces. It has not established that every interaction is easy to discover, visually coherent, or dependable on a particular physical audio setup.

| Area | Current implemented behavior | What the evidence does not imply |
| --- | --- | --- |
| Engine ownership and edits | Control-owned editing state publishes prepared render revisions; source/media lifetime, individual edit rollback, ordered commands, meter snapshots and startup/shutdown have explicit contracts | Every compound UI action or history variant is transactional |
| Projects and transport | Atomic save/recovery, coherent project capture/restore, separate render/consumed/presentation clocks, and defined seek/pause/stop/loop behavior | Physical speaker/microphone latency is calibrated |
| Audio and MIDI | Audio regions with fades/automation, instruments with note gates/releases and audition retirement, track/master gain/pan/EQ and built-in effects | Full commercial-DAW routing/plugin/MIDI-hardware parity or every preset's listening acceptance |
| Processing and analysis | Corrected dynamics/limiter delay, sample-counted transitions, anti-aliased import conversion, calibrated spectrum/spectrogram and consistent capture identity | Every meter represents loudness/true peak, or all possible FX chains are independently certified |
| Recording and export | Worker-owned recoverable recording journals and bounded previews; independent export snapshots, explicit range/preroll/tails/normalization and streamed durable WAV output | Unlimited media duration/residency, unattended overnight robustness, or physical recording alignment |
| Media work | Bounded background decode/resampling, cancellation, source/project-generation checks, owned insertion and warm cache reuse | Disk-streamed playback: active media still pins full decoded files; decode buffers have a documented admission ceiling |
| Performance | Inactive audio/MIDI work reduced, compact meter storage, lighter mixer edits, queue/worker diagnostics and substantially cheaper analysis | Universal track-count limits, zero-latency playback, or a broadly qualified optimized release |

Spectrum/spectrogram are specifically Hann-windowed **tonal peak-amplitude dBFS** on the existing logarithmic grid. Stereo analysis currently uses Mid, `(L+R)/2`. It is not selectable L/R/Side, integrated band energy, or loudness. That distinction matters for using the DAW as a sound-learning tool.

## 2. Where the improvement chain stands

1. **S1 — Runtime foundation:** individual engine operations and ownership are substantially hardened. Three editing-integration debts remain: all-or-nothing compound actions, stable identity across the remaining history variants, and consistent visible rejection feedback. Physical/interactive acceptance was always separate.
2. **S2 — Projects and timing:** the six software slices are implemented, including atomic project restore. Device alignment and recovery presentation still need real workflow acceptance.
3. **S3 — DSP and analysis:** the named processing, calibration, conversion, instrument and export slices are implemented and tested within their documented contracts. Listening and arbitrary-chain qualification remain distinct.
4. **S4 — Runtime efficiency and integrated acceptance:** S4.2–S4.8 implemented the measured improvements. S4.9 exercises the combined system, records memory/continuity/scheduling evidence and keeps remaining acceptance failures explicit. The initial eight-case run retained its failures. Subsequent bounded closeout passed continuity in all four profiles, strict timing in two, and the declared memory envelope across twenty repeated lifetimes. Bounded software closeout is complete; strict stress timing and physical listening/capture acceptance remain open.
5. **S5 — Cohesive UI:** partially implemented product-facing phase; see the [implementation ledger](S5-IMPLEMENTATION.md). Begin with an actual user journey and observable status, then refine transport, arrangement, mixer, inspector, FX/analyzers, recording and MIDI surfaces in focused slices.
6. **S6 — Creative expansion:** later scope: richer routing/buses/sends, sidechains, general automation, external MIDI and deeper modulation/experimentation. Existing lower-level correctness does not automatically supply those workflows.

## 3. Work that matters before a broad UI redesign

The S1 debts are still concrete in current code. Compound transform history now publishes atomically. Slide drops retain affected neighbor content for undo/redo, including topology restoration for drops that create tracks. Timeline compound duplicate/delete now uses one retained-content action and history entry. Clipboard paste now uses one complete insertion transaction, including required tracks and guarded history. Other compound trim/split entry points remain follow-ups. The gesture audit found no reachable ripple-trim mode; existing audio left trim and mixed-media ripple movement are now hardened. Automation history still refers to track/clip indices; rename history resolves a sampler identity. Some composed caller operations do not propagate every rejected setter. Session replacement has since gained its own transaction in S2; do not mislabel that completed work as still pending.

Start with a representative editing sequence: import → trim/split/move → duplicate/multiselect → undo/redo → add/reorder/remove effects → record → export → save/reopen. Test both success and a rejected operation, and verify the visible result matches the actual engine. Fix the failure feedback and history boundaries encountered in that sequence before multiplying editing entry points.

Physical acceptance should then cover audible playback and tails, selected-track microphone capture, recorded placement against a known reference, device restart/recovery and a comfortable buffer setting. Empty and populated real-app frames now render with fonts at 2560×1440. Inspection shows crowded/overlapping compressor and limiter controls; this is a concrete S5 layout finding. GUI acceptance still needs actual frame/input latency and sustained rendering with meters/analyzers open. A renderer self-test or captured first frame proves only its stated visual boundary.

## 4. MCP readiness

A bounded source/tooling inventory found no DAW MCP server or JSON-RPC control surface. The current C APIs and headless C harnesses are useful building blocks; they are not an external automation interface. The existing main-thread message system handles wakeups/notifications and is not a validated general-purpose remote command dispatcher.

An MCP layer is best treated as a new adapter over explicit application operations. It should not invoke mutable engine APIs directly from arbitrary transport threads, bypass undo/project ownership, or expose the internal structs as an external contract.

A focused implementation sequence would be:

1. **Read-only project inspection:** project identity/revision, stable track/clip/FX IDs, transport state, runtime diagnostics and bounded meter/analyzer snapshots. Each snapshot should expose age/epoch and distinguish unavailable data from silence.
2. **Controlled transport and file operations:** play/pause/stop/seek; save/open; asynchronous import/export with operation IDs and observable pending/applied/failed/cancelled states. Dispatch to the owning control thread and preserve project-generation checks.
3. **Editing commands:** route through common application actions with validated parameters, stable target IDs, undo behavior and explicit rejection. Define transaction scope before adding multi-object tools.
4. **Agent acceptance scenario:** create an isolated test project, import known sound, apply a supported edit/effect, inspect measurements, export and reopen. Verify the resulting audio/project, cancellation and stale-target behavior, rather than accepting successful protocol responses alone.

That adapter can share domain actions with the UI; neither surface should become a second implementation of audio processing. Creating an MCP connection will improve testability and agent access, but it will not establish visual usability or physical audio quality.

## 5. Practical order from here

1. Preserve the completed S4 software closeout and its explicit stress/device limits; keep the default and optimized builds clearly identified.
2. Run one visual/audio product-acceptance session on the actual app with the workflow above. Turn concrete failures into bounded S5 slices, starting with editing integrity and trustworthy state/feedback.
3. Build the read-only MCP/application-action boundary, then add a small verified write workflow. Expand only after the operation lifetime, undo and project replacement rules hold.
4. Continue the coherent S5 surface improvements, then choose S6 creative capabilities based on real use.

The subsequent [S4 closeout](S4-CLOSEOUT.md) adds best-effort render-worker priority, repeated-lifetime memory qualification and a silent CoreAudio callback check. The concrete proposed [S5 behavior slices](S5-FUNCTIONAL-SLICES.md) begin with editing integrity and common actions.

The immediate transition is from backend implementation to proving and improving the user and automation workflows built on it. No new MCP server, broad UI rewrite, commit, package installation or release is implied by this status report.
