# soniCs improvement chain

Current authorization: work through the proposed S4 slices with correlated correctness and performance checks at each boundary. S4.1 audit is complete; S4.2 audio-region scheduling, S4.3 MIDI candidates, and S4.4a compact meter publication are implemented. S4.4b selective edit preparation and S4.5 worker timing/queue policy are now software-complete; S4.6 recording/export disk pipelines are now implemented at the documented memory/I/O boundary. The [capture-clock follow-up](S4-CAPTURE-CLOCK.md) now preserves anchored samples through diagnostic contention, with strict zero-gap software measurements. [S4.7a–d media preparation/cache](S4.7-MEDIA.md) now implement metadata/content probing, bounded background preparation, generation-safe publication and workflow integration. [S4.8 analyzer compute](S4.8-ANALYSIS.md) is implemented and calibrated, with measured CPU reductions and consumer diagnostics. [S4.9 integrated sustained assessment](S4.9-ACCEPTANCE.md) is now complete: eight workflow cases passed, seven retained continuity, and none passed the strict timing gate. Full S4 acceptance remains open for timing/device, long-session memory and interactive GUI qualification. Track current completion and evidence in [the S4 implementation ledger](S4-IMPLEMENTATION.md). Earlier S1 deferrals and physical-device acceptance remain separate. No commits without explicit permission.

## 1. Iteration contract

For every focused slice: (1) audit current implementation and dependencies, (2) enumerate normal/transition/concurrent/boundary/failure/persistence/user-feedback behaviors, (3) record the functional contract and meaningful decisions, (4) implement it, (5) validate with evidence appropriate to the behavior. Routine engineering choices are covered by the authorization to implement S1. Introduce a new user decision only when product semantics cannot reasonably be resolved within that scope.

Source, offline rendering, threaded stress, real devices, visual interaction, and publication are distinct proof layers. Preserve working project/media bytes. No commits without explicit permission.

## 2. Full chain

1. **S1 — Dependable engine execution:** S1.1 command delivery/overflow; S1.2 clip/source lifetime; S1.3 track/FX structural edits; S1.4 parameter updates and coherent snapshots; S1.5 output device format/channel handling; S1.6 zero-gain semantics; S1.7 startup/shutdown/recovery.
2. **S2 — Durable projects and accurate timing:** S2.1 atomic save/recovery; S2.2 coherent project capture/restore; S2.3 render/consumed/presentation clocks; S2.4 seek/stop/pause/loop semantics; S2.5 timestamped capture/alignment; S2.6 deadline/dropout/queue diagnostics.
3. **S3 — Truthful processing and analysis:** shared fade math; limiter delay/dynamics; smoothing/discontinuities; contiguous analyzer windows; calibration/tap identity; supported media/resampling; instrument release/aliasing; deterministic exports/reset/tails.
4. **S4 — Sustained performance:** optimized benchmarks; active-region scheduling; bounded rendering/allocation; streamed recoverable recording; streamed export; background decode/cache policy; long-session stress.
5. **S5 — Cohesive UI:** shared interaction rules; transport/status; track/mixer; arrangement; inspector; effects; synchronized analysis; recording/MIDI. Make physical relationships and measurements visible.
6. **S6 — Creative expansion:** buses/sends; routed sidechains/latency compensation; general FX automation; external MIDI; richer instruments/modulation; reusable experiment presets.

All six S2 software slices have bounded closeouts: [saving](S2-SAVING.md), [capture/restore](S2-CAPTURE-RESTORE.md), [clocks/transport](S2-CLOCKS-TRANSPORT.md), [recording durability](S2-RECORDING-DURABILITY.md), and [diagnostics](S2-DIAGNOSTICS.md). Physical timing/listening acceptance, recovery-browser UI, and long-take streaming remain separate. Analyzer correctness is backend work even though users encounter it through a visualization; it is not deferred to visual polish.

## 3. S1 completion ledger

This earlier ledger is retained as the original requirement baseline. The reconciled dispositions and evidence are in [S1-CLOSEOUT.md, section 3](S1-CLOSEOUT.md#3-reconciled-original-s1-checklist); subsequent checkpoint sections below are chronological, not an instruction to continue working.

| Slice | Required behavior / proof | Status |
| --- | --- | --- |
| S1.1 | Whole-message FIFO, bounded consumption, observable rejection, reliable overload stop/note-off, rebuild recovery, payload lifecycle; saturation/wrap/concurrency tests | Implemented and software-verified at the engine boundary; remaining caller feedback is deferred (D3) |
| S1.2 | Source/media objects stay alive for every reader; live add/move/trim/delete and MIDI replacement; deterministic lifetime and threaded stress tests | Independent source/media ownership and stable registry metadata implemented; render-after-delete, 2,000-cycle source stress, full mixer, and analyzer/lifecycle integration exercised |
| S1.3 | Track/FX add/remove/reorder cannot race rendering or target replacement objects; state survives structural edits | Mixer uses revision-owned track/EQ/FX state; concurrent structural test and EQ history comparison pass; analyzer/output integration passes dummy-device restart, fault-injection, ASan, and TSan checks |
| S1.4 | Scalar changes and snapshots have explicit owners/publication; transport/meter reads coherent; rejected changes cannot silently pretend to apply | Meter ownership/publication isolated; individual scalar, FX, track, clip, MIDI, automation, creation, overlap, cross-track move, complete track-setting and clip-transform transactions implemented and exercised; playback intent/application snapshots implemented; partial against the original full requirement because D1–D3 remain |
| S1.5 | Float output contract, supported channel/rate/block negotiation, failure cleanup; fake-backend tests | Adapter implemented; fake-backend tests pass; integrated startup rollback/restart tests pass with SDL dummy output; physical endpoint acceptance unverified |
| S1.6 | Explicit zero gain means silence in playback/audition/bounce/session round-trip; missing fields retain defaults | Implemented and exercised: explicit zero survives playback mixer, audition, bounce, session parsing/round-trip, audio/MIDI clipboard paste, and clip/track undo/redo; integration uses audible baseline fixtures |
| S1.7 | Repeatable start/stop/failure recovery; pending owned payloads reclaimed; producers stop before consumers are destroyed; safe queue flush | Output/analyzer lifecycle and recording retry repaired; injected failures, repeated restarts, and capture retry tests pass; software acceptance reconciled in the closeout; physical/interactive acceptance remains D4 |

S1 exits only after each row has current evidence, targeted tests and existing stable/legacy gates pass, and `make` succeeds. Passing offline tests alone does not establish physical low latency or race freedom.

## 4. Shared boundaries

Retain vendored `third_party/codework_shared` as the dependency source. Reuse existing time, UI wake/kernel, theme/font/pane, and visualization adapters. Current `core_queue` is an unsynchronized pointer ring or mutex queue, not a drop-in concurrent audio packet transport. Keep the audio-specific SPSC contract local for this slice; no shared version/adoption change is implied. Keep rendering ownership in the DAW, and reserve general worker pools for noncritical work.

## 5. Current implementation checkpoint

1. Commands use all-or-nothing packets, a creating-thread producer contract, reserved safety capacity, a bounded FIFO batch, and atomic overload stop/all-notes-off requests. Counters expose acceptance, rejection, application, and safety fallback. Graph payloads are prepared off-thread, adopted at a block boundary, retired to the control thread, and canceled on shutdown. Source rebuilds use a latest-complete revision mailbox.
2. Source revisions own independent sampler/instrument snapshots and cache pins. Deleting or replacing editable clips cannot free the active source or its media. The source-only concurrent test covers deterministic deletion/adoption/reclamation and 2,000 add/fade/trim/delete cycles. This does not prove the full mixer's track/FX state safe.
3. Output callbacks require float samples and the requested rate/channel layout. SDL may negotiate block size and perform internal hardware conversion. Unsupported returned formats are closed. The fake backend verifies rejection, frame sizing, remainder clearing, and ten reopen cycles; physical-device behavior is unverified.
4. Explicit zero gain survives the identified playback, audition, session, undo, drag, clipboard, and overlap-split paths. Missing serialized gain defaults to unity during parsing. Tests cover silent audition/bounce and omitted-versus-zero serialized round trips.
5. Output queue flushing publishes a boundary and lets its sole consumer advance its own tail. Frame writes/reads never consume fractional interleaved frames, including three-channel queues whose rounded byte capacity is not frame-aligned. Analyzers now use generation-tagged whole windows without stealing consumer indices. Capture retry quiesces the old callback before replacing queue storage. Scope storage access is serialized by the meter mutex, with nonblocking worker delivery.

Historical next-step list superseded by the bounded closeout: individual mutations and playback status have since been implemented; remaining compound actions, identity/feedback integration, and physical acceptance are explicitly deferred. Do not continue implementation without a new instruction. The mixer now uses prepared track/EQ/FX revisions and preserves unchanged DSP histories across unrelated source edits. UI meter publication uses a nonblocking lock attempt; rendering does not wait for UI access.

## 6. Analyzer and lifecycle checkpoint

1. Whole-window packet tests verify contiguous samples, overflow drops, and invalidation without live queue reset. Analyzer consumers own their averaging/history and reject stale target generations.
2. `test-engine-analysis-lifecycle` verifies five injected output startup failures, eight restart cycles, idempotent start/stop, preserved FX identity, and live structural edits while both analyzers publish. ASan and TSan pass this path.
3. `test-audio-recording` now exercises callback-active error/retry with dummy capture and a subsequent synthetic-take retry. TSan passes the capture path.
4. These checks use software dummy devices; they do not establish physical latency, hardware recovery, analyzer calibration, or user-visible acceptance. See [S1 checkpoint](S1-CHECKPOINT.md) for logs and remaining work.

## 7. Transactional control edits

1. Gain, pan, mute, solo, and EQ reject failed revision preparation and restore their prior editable values. Gain/pan and FX parameters reject non-finite input; gain cannot be negative and pan stays within [-1, 1].
2. FX parameter changes hold the control-manager mutex through preparation and rollback. FX add/remove/reorder/bypass prepare a candidate control manager and commit it only with a successfully prepared render revision. The active render manager retains compatible histories through normal adoption.
3. `test-engine-parameter-transaction` injects publication rejection and checks scalar/EQ/FX state, chain IDs/order/bypass, successful retry, zero gain, invalid values, and preservation of an earlier accepted pending revision.
4. This does not close all S1.4 work: clip and track structural mutation failures, tempo/status coherence, EQ UI/session/undo error propagation, and user-facing rejection feedback still need integration. Structural FX transactions add control-side preparation cost; sustained performance is still S4 work.

Tempo/record-arm follow-up: control-thread admission and finite tempo validation are implemented; queued tempo uses the project rate. Recording isolation rolls back on preparation failure. Targeted command/parameter and recording tests pass. Caller-side recording rejection handling is now covered by the integration checkpoint below; requested/applied transport snapshots remain in S1.4.

## 8. Zero-gain integration and rejected history operations

1. `test-timeline-midi-region` now checks actual nonzero audio/MIDI baselines, zero-gain copy/paste, paste undo/redo, and track-gain undo/redo. Silence is verified in both the active playback mixer and offline bounce, alongside stored gains. This closes S1.6's pending undo/clipboard proof.
2. Recording setup now checks arming acceptance before advertising an active take. Rejection releases the unused queue, reports a retryable error, and causes capture startup to close its newly opened endpoint. The recording test injects rejection and verifies successful retry.
3. Undo/redo reserves destination stack space before applying an edit, and retains the command on its original stack if application fails. The integration test checks rejection/retry in both directions through real FX parameter validation.
4. Individual multi-operation undo/session/clip/track edits still need atomic failure contracts; preserving the history entry does not itself undo a partially applied composite command. S1.4 remains open.

## 9. Track structure and clip deletion transactions

1. Track append/insertion and removal prepare a candidate effect manager and complete replacement render revision before relinquishing original ownership. Preparation failure restores track count, order, runtime identities, effect chains, and recording isolation. Track insertion/removal remaps the engine's recording target with the surviving identity, or disarms a removed target.
2. Clip deletion retains the detached clip until publication succeeds. Rejection restores ordering, active state, sampler/MIDI ownership, and notes; only accepted deletion reclaims editable source data.
3. The parameter transaction fixture now includes audio/MIDI deletion rejection, last-clip activity, track add/insert/remove rejection and retry, FX IDs, and recording-index shifts. ASan and TSan evidence is recorded in the checkpoint.
4. Remaining structural work includes clip creation and overlap compound edits, implicit track growth, app-level recording identity when tracks move. These are not covered by successful insert/delete tests alone.

S1.2 registry follow-up: the new source-growth fixture reproduced relocation of metadata borrowed by existing clips. Registry entries now have stable heap addresses behind a growable pointer table; clear detaches editable clip references before freeing metadata. The source-lifetime fixture grows the table by 128 entries, verifies identity/path, clears it with a live clip, and continues rendering/deletion stress.

## 10. Transactional capacity growth

1. Track and meter arrays are prepared separately, and all pointers/capacities commit only after scope storage also succeeds. Failed allocation preserves the original arrays and owned track resources.
2. Snapshot buffers are copied row-by-row using the new capacity stride. New scope banks are prepared independently; partial construction frees only their new rings and leaves the old host unchanged. Existing scope data survives successful growth.
3. The parameter transaction fixture rejects each of seven array allocations and each new scope ring, verifies unchanged storage on failure, retries successfully, and checks both snapshot rows and a retained scope sample. This closes the identified capacity-expansion failure path; it does not close composite edit or implicit track-growth semantics.

## 11. Transactional scalar clip edits

1. Timeline moves, region trims, gain, and fade lengths now prepare a replacement source revision before accepting the edit. Rejection restores clip metadata, sampler timing, and sorted clip order; move output indices are written only on success.
2. These setters require the control thread and an existing clip. Scalar timing refresh avoids reallocating unchanged automation. Gain rejects non-finite values; fade bounds avoid integer overflow.
3. Failure tests compare audible sampler output before and after rejected edits, along with source identities and metadata. Clip creation, MIDI/automation mutation, cross-track/overlap composites, caller rejection propagation, and coherent transport status remain S1 work. Fade-curve DSP correctness remains S3.

## 12. Clip automation ownership and rejection

1. Lane creation, point insertion/update/removal, lane-point replacement, and whole-lane replacement prepare independent automation and sampler/instrument sources before publishing. Rejection frees the candidate and retains original pointers, points, sources, and audible behavior; output indices are exposed only on success. Invalid updates do not implicitly create lanes or tracks.
2. Lane-copy, lane-replacement, clip-copy, and snapshot helpers now honor point-allocation failures and preserve old ownership. Sampler automation replacement returns failure and commits only a complete lane set. Self-copy and aliased input are supported by copying before release.
3. Tests inject publication rejection and candidate lane/source/point allocation failures on audio and MIDI clips. They verify metadata/pointer preservation, exact rejected-edit samples, accepted audio-volume/MIDI-level changes, and concurrent automation replacement in 2,000 source-edit cycles.
4. MIDI note/parameter and track-instrument automation transactions are implemented in the next checkpoint; remaining S1 work includes clip creation/composites, caller integration, recording target identity, and transport snapshots. Generic volume/pan automation is currently skipped by the MIDI instrument renderer; correcting that audible behavior is explicitly recorded for S3, separate from this ownership work.

## 13. MIDI and track-instrument transactions

1. MIDI note insertion/update/removal/replacement prepares independent note storage and exposes output indices only after publication. Rejection retains original notes and the active render revision; non-finite velocities are rejected.
2. Track instrument enabled/preset/parameter settings and clip preset/parameter/inheritance settings restore prior metadata on preparation failure. These setters require the control thread and existing tracks/clips; instrument parameter inputs must be finite.
3. Whole track-instrument automation and individual lane-point edits copy before release and publish once, retaining original ownership on allocation or publication failure. Aliased input is supported.
4. Tests cover publication and allocation rejection, note/source-state preservation, unchanged rendered output, successful clip and inherited track level changes, and note removal. Concurrent source stress now includes MIDI note insert/update/remove in each of 2,000 audio/automation/MIDI cycles.
5. S1 remains open for clip creation/composite edits, caller rejection propagation, app recording target identity, coherent transport snapshots, and final acceptance audit. These changes do not implement S2 saving/recovery or S3 instrument processing corrections.

## 14. Clip addition candidates

1. Audio/MIDI creation, audio duplication, and segment creation stage independent descriptor arrays while borrowing existing clip sources. Existing descriptors are not moved during preparation, so duplicate/segment source references remain valid across capacity growth.
2. Additions publish once; failure releases only newly owned clip resources and leaves original clip arrays, ordering, activity, instrument settings, and output indices unchanged. Implicit new tracks and their effect-manager changes are committed with the addition or rolled back together. Prepared spare capacity and source-registry metadata may remain cached after rejection.
3. Default-lane and sampler automation setup failures propagate and release partial sources. Duplicate start overflow is rejected; segment media bounds include the source offset. Tests cover creation/publication/allocation failure, new-track rollback, successful duplication/segment ownership, and implicit-track retry.
4. Remaining S1 edit work is cross-track/overlap and multi-operation caller transactions, plus recording identity, transport snapshots, and final acceptance. Atomic project save/recovery remains S2.

## 15. Atomic overlap resolution

1. Overlap removal/trim/shift/split now prepares one candidate descriptor array and independent sources for changed regions. Unchanged clips and the anchor keep their sources; newer-clip priority is preserved. Exactly one render revision is attempted after all regions are ready.
2. Any allocation or publication failure frees candidate-owned regions and retains original descriptors, source ownership, clip ranges, active revision, and output index. Successful publication releases replaced control sources; worker revisions retain their own copies/media pins.
3. A mixed-operation test covers split, trim, shift, removal, and a newer survivor together, with injected descriptor/source/automation allocation failures and publication rejection. Source stress includes overlap splitting in each of 2,000 audio/automation/overlap/MIDI cycles.
4. This closes the individual overlap resolver transaction. Cross-track moves and multi-operation UI/session/undo actions still require caller-level atomicity; recording target identity, transport snapshots, and final S1 acceptance remain open.

## 16. Atomic cross-track movement

1. `engine_move_clip_to_track` transfers complete clip ownership between prepared source/destination descriptor arrays and publishes once. Clip creation identity, audio sources, notes, automation, and clip settings survive the transfer. The destination track supplies inherited MIDI instrument settings.
2. Rejection restores both arrays/activity/settings and audio sampler timing, retaining caller output indices. New destination tracks and effect-manager growth roll back together. Same-track movement uses the existing transactional timeline setter without duplicating audio.
3. Timeline drag now delegates to this engine operation. Undo's cross-track caller returns failure when the move is rejected instead of applying subsequent fields to the original track. The broader multi-operation undo transaction remains unfinished.
4. Tests cover audio/MIDI publication rejection, descriptor-allocation failure, implicit-track rejection/retry, one publication, stable source/note identity, and same-track no-duplication behavior. Each live stress cycle now includes audio and MIDI transfers out and back.
5. Remaining S1 work includes compound UI/session/undo atomicity and rejection feedback, recording-target identity, coherent transport snapshots, and final acceptance.

## 17. Recording target identity

1. Takes retain the target track's runtime identity when arming succeeds. Finalization resolves its current index before writing the recording file or inserting a clip.
2. Track insertion/removal before the target no longer redirects the take. If the target itself was removed, finalization reports an error and retains captured samples without inserting them into a replacement track.
3. Recording tests cover insertion before an active take and deletion followed by a replacement at the same index. Remaining S1 work includes compound caller transactions, transport snapshots, rejection feedback, and final acceptance.

## 18. Transport acceptance versus application

1. Live play/stop no longer write the worker playback flag from the control thread after enqueueing. `engine_transport_is_playing` reports applied playback state; successful submission alone is not execution.
2. Offline play/stop synchronously consume their command batch, preserving offline test/recording behavior and applying stop flush/reset in order.
3. Command tests hold a simulated live consumer, verify accepted play/stop leave applied state unchanged, consume the queue, and verify the state transition. Requested-state snapshots and responsive pending-action UI remain part of the unfinished coherent transport work.

## 19. Playback intent/application snapshots

1. Accepted play/stop requests carry monotonically increasing serial tokens. The worker publishes applied playback tokens; a snapshot reports accepted intent, applied state, serials, and whether commands are pending. It also handles application occurring before submission returns.
2. Overflow safety stops carry the latest accepted stop token and acknowledge it when applied. Rejected requests do not change intent. Shutdown cancels pending intent and resets playback snapshot state.
3. Keyboard toggles and recording play toggles use accepted intent, while playback consumers continue to use applied state. Transport buttons show a pending ellipsis while their active styling reflects applied playback. This snapshot covers play/stop, not S2 render/consumed/presentation clocks.
4. Tests cover pending play/stop, rapid toggles, rejected play during safety-stop recovery, safety acknowledgement, and 10,000 concurrent playback requests with monotonic snapshot checks. Compound caller transactions, rejection feedback integration, and final S1 acceptance remain.

## 20. Transform-history targeting audit

1. Clip-transform undo now prefers stable creation identity for audio as well as MIDI, retaining sampler lookup only for older states without an identity. This survives accepted edits that replace the control sampler.
2. Timeline position changes return the clip's new sorted index, which subsequent transform fields now use. Rejected timeline movement returns failure rather than continuing on the stale index.
3. A regression replaces an audio sampler through automation, then undoes/redoes a transform across a neighbor. It verifies identity, placement, gains, unchanged neighbor state, and rendered silence/restoration.
4. Remaining compound work: apply the complete clip state with one commit; provide outer atomicity for multi-clip/track/session operations; migrate remaining history variants that still identify clips by sampler/index; integrate rejection feedback. These targeting fixes do not make the existing multi-field operation atomic.

## 21. Complete track-settings restores

1. Added a settings transaction covering track gain, pan, mute, solo, and MIDI instrument enabled/preset/parameters. It validates all inputs, prepares one render revision, and restores every field on rejection.
2. Track-setting undo/redo now uses that transaction and updates panel values only after acceptance. Disabled-instrument history retains the previously dormant instrument configuration, matching the existing history semantics.
3. Tests inject publication rejection and assert one attempt with all settings/revision preserved, then successful retry. Undo integration checks invalid snapshots preserve engine/UI/history and corrected undo/redo restores every requested field.
4. Remaining S1 compound work includes complete clip transforms, multi-clip/track/session operations, remaining history/caller identity migrations, rejection feedback, and final acceptance.

S2.5/S2.6 software slices are implemented with bounded proof in [recording and durability](S2-RECORDING-DURABILITY.md) and [runtime diagnostics](S2-DIAGNOSTICS.md). Preserve the listed physical calibration, activation/feedback, streaming, and manual UI acceptance boundaries; S3 has not started.

S3.1 existing audio fade parity is implemented at the boundary in [S3-FADE-PARITY.md](S3-FADE-PARITY.md). The next proposed slice is S3.2 analysis calibration; do not start it as part of S3.1 closeout.

## S4 closeout and S5 handoff

See [bounded S4 closeout](S4-CLOSEOUT.md) for measured disposition and retained strict/physical limits. The next proposed sequence is [S5 functional slices](S5-FUNCTIONAL-SLICES.md); acceptance-discovered input fixes do not constitute a broad S5 redesign.
