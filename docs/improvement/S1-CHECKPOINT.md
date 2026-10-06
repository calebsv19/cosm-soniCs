# S1 implementation checkpoint — 2026-09-19

This is an incomplete S1 checkpoint, not release approval. All changes remain in the working tree, with no commit or deployment.

## 1. Implemented and exercised

1. Exact command packets, producer identity checking, FIFO overflow rejection, safety reserve/fallback, counters, bounded draining, and owned graph payload cancellation.
2. Source revisions with independently owned sampler/MIDI data and retained immutable media, adoption at block boundaries, and control-thread retirement. Rebuild allocation failures retain the prior revision.
3. Float/rate/channel endpoint contract, block-size negotiation, endpoint rejection cleanup, and deterministic callback sizing.
4. Explicit zero gain preserved across identified fallbacks; missing serialized gains default to unity at parse time.
5. Consumer-owned output flush boundaries and frame-aligned queue operations.
6. S1.3 preparation bridge: tracks have process-local runtime identities independent of their array positions. `fxm_clone_for_render` prepares independently owned effect handles while retaining instance IDs and parameter targets. `fxm_transfer_render_state` exchanges compatible handles at an exclusive worker boundary so histories survive structural revision adoption; replaced handles remain owned by the retiring revision. Preparation failures destroy partial revisions. The mixer now consumes these prepared revisions, including independently owned EQ histories and meter accumulation.

## 2. Validation

- `make test-engine-command-delivery`: passes exact packet boundary/wrap tests, overload safety/recovery, wrong-producer rejection, graph swap/cancel behavior, 100,000 concurrent FIFO packets, and 100,000 concurrent three-channel frames with repeated flushes.
- `make test-engine-source-lifetime`: passes deterministic render-after-delete/media retirement and 2,000 concurrent source add/fade/trim/delete cycles. It intentionally exercises source ownership without the unsafe full track/FX mixer.
- `make test-audio-output-device`: passes fake-backend format/rate/channel rejection, callback interpretation, and ten open/close cycles. No physical audio backend is used.
- `make test-midi-instrument-render test-session`: passes the added silent audition/bounce and zero-versus-missing gain round-trip checks.
- `make test-stable test-legacy`: passes all current stable targets and all five legacy targets.
- `make`: succeeds.
- `git diff --check`: passes.
- AddressSanitizer: both command-delivery and source-lifetime tests pass with application/support objects compiled using `-fsanitize=address -fno-omit-frame-pointer -g` in `/tmp/daw-s1-asan-isolated`. This validates the exercised ownership paths; it is not a ThreadSanitizer result or a full live-mixer race proof. Log: `/tmp/daw-s1-asan.log`.

Full-suite log: `/tmp/daw-s1-checkpoint-tests.log`; build log: `/tmp/daw-s1-build.log`; final queue stress log: `/tmp/daw-s1-flush-test.log`. These temporary logs supplement the reproducible test targets and may not survive host cleanup.

## 3. Next required implementation

1. S1.3 final acceptance: analyzer/output integration now passes the dummy-device lifecycle test and sanitizers described below. Complete the remaining contract audit together with S1.4; physical-device behavior remains unverified.
2. S1.4: finish status/tempo/transport publication, parameter target validation and rejection/rollback semantics. Meter publication is coherent and nonblocking, and queued MIDI targets use track identity. Current counters remain independently atomic, not a transactional snapshot.
3. S1.7 final acceptance: analyzer ownership, output startup rollback/restart, capture retry, and producer-before-infrastructure shutdown are now implemented and exercised as described below. Review residual failure paths before closing the ledger.
4. Zero-gain undo/clipboard integration now passes (section 9). Finish composite mutation failure handling, final live-mixer/lifecycle acceptance, and distinguish software proof from physical-device acceptance before declaring S1 complete.

Keep S2 atomic saves and recovery next in the improvement chain. Do not begin S2 merely because these isolated S1 tests pass.

## 4. S1.3 bridge integration contract

`make test-effects-revision test-track-role test-midi-instrument-render` passes after the bridge changes. The effect fixture checks independent handles, state retention through insertion/removal mappings, invalid mapping rejection before mutation, partial preparation failure cleanup, parameter-target adoption without history reset, and balanced creation/destruction counts. Track tests verify identity survival across array movement and fresh identity for replacements. Log: `/tmp/daw-s1-structure-foundation.log`.

The effects revision test also passes under AddressSanitizer using the isolated application/support build described above; log: `/tmp/daw-s1-effects-asan.log`. This is ownership-test evidence, not proof that the unconverted live mixer is race-free.

1. Clone only control-owned FX state; never clone a concurrently rendering manager. Install the new revision's meter/scope callbacks explicitly so callback userdata belongs to its render state.
2. Build the old-track mapping from runtime identities, not array positions. A new or replacement track maps to -1. Mappings must be one-to-one; invalid mappings are rejected before any state exchange.
3. Transfer handles only at a block boundary when both revisions are exclusively worker-owned. The operation allocates and destroys nothing. It preserves compatible handles and smoothing current values, including across parameter and bypass changes; the prepared revision retains its new targets for the existing smoothing path. Effect setter allocation and DSP discontinuity behavior remain in S3/S4.
4. Keep old render metadata, handles, meter banks, and callback userdata alive until retirement. The mixer now uses prepared track/FX arrays and owned meter accumulators. Analyzer transport and scope semantics still require their lifecycle audit.
5. Runtime track identities are not serialized project IDs. They protect one process's object lifetimes and must not be used as durable asset identity.

## 5. Mixer cutover evidence

1. `make test-stable test-legacy` passes after the mixer cutover; log `/tmp/daw-s1-mixer-regression.log`.
2. `engine_mix_ownership_test` runs 300 concurrent track insertion/removal, clip, FX, EQ, pan, and meter cycles through `engine_mix_tracks`. A deterministic reader renders the old revision after its editable track and effects are deleted. Holding the UI meter mutex does not stall the reader. Effect chains retain their IDs when tracks shift.
3. A numerical comparison checks that inserting an empty track between two render blocks leaves the surviving track's EQ output equal to uninterrupted rendering within 1e-7.
4. Command tests verify that queued MIDI note-on follows a surviving track through insertion, cannot target a deleted track's replacement, and active audition stops when its track is removed.
5. AddressSanitizer passes the mixer and command tests with application/support objects instrumented; log `/tmp/daw-s1-mix-asan.log`.
6. This test does not start analyzer threads or a physical audio device. It does not establish complete S1 race freedom. Parameter target adoption retains the existing compatible handle and smoothing current values. Detailed effect setter allocation and DSP discontinuity behavior still require S3/S4 work.
7. ThreadSanitizer passes the concurrent mixer ownership test with application/support objects instrumented in `/tmp/daw-s1-tsan-isolated`; log `/tmp/daw-s1-mix-tsan.log`. This strengthens the tested mixer-path ownership evidence, with the same analyzer/device scope limits.
8. The render transport frame is now atomic for UI reads; this remains the render clock, not an audible presentation clock. Tempo defaults are initialized at engine creation instead of only in an output-start failure branch.
9. After the final parameter-history adoption adjustment, `make test-effects-revision test-engine-mix-ownership test-engine-command-delivery` and `make` pass; logs `/tmp/daw-s1-mixer-final-checks.log` and `/tmp/daw-s1-mixer-build.log`. The earlier sanitizer runs preceded that small adjustment; the final targeted tests explicitly cover its history-preservation behavior.

## 6. Analyzer, output lifecycle, and capture recovery evidence

1. `engine_analysis_queue.c` publishes complete contiguous windows in exact packets. Whole-window overflow drops leave the consumer index untouched. Stable track/effect identities and target generations reject obsolete windows. Analyzer consumers exclusively own their averaging state; history publication rechecks the generation under its result mutex. Live target changes and clears do not reset queue indices.
2. `engine_start` allocates worker scratch and initializes the output queue before creating threads. Repeated start is idempotent. A single failure path stops/joins all created threads, releases scratch, and closes the failed endpoint. `engine_stop` pauses output and joins worker/analyzer threads before resetting quiescent queues. Device restarts preserve project configuration, tempo, and FX identity.
3. `make test-engine-analysis-lifecycle` passes complete-window alignment/overflow/generation checks, five injected startup failures (open, each of three thread creations, device start), eight successful restarts, repeated start/stop, and live track insertion/removal while both analyzers publish. A first run caught an omitted output-queue initialization in the refactor; the corrected implementation passes. Log: `/tmp/daw-s1-analysis-lifecycle.log`.
4. ASan and TSan pass that full threaded fixture with application/support objects instrumented. Logs: `/tmp/daw-s1-analysis-asan.log` and `/tmp/daw-s1-analysis-tsan.log`. SDL and vendored libraries are not wholly instrumented. Dummy output exercises software lifecycle, not physical audio acceptance.
5. Application shutdown now stops engine producers before tearing down UI wake/message/job infrastructure. Recording retries stop/close a failed take's active callback before replacing its queue. Capture-start failure releases armed/allocated take state and preserves the error message. `test-audio-recording` adds repeated dummy-capture error/retry followed by a synthetic-take retry; the initial fixture passes TSan (`/tmp/daw-s1-capture-tsan.log`). The final fixture uses bounded polling instead of a fixed startup delay.
6. `make test-stable test-legacy` passes after these changes; log `/tmp/daw-s1-lifecycle-regression.log`. This includes the final bounded-poll capture fixture. S1.4 rejection/publication semantics and zero-gain undo/clipboard integration proof remain open; do not begin S2 or mark all of S1 complete from these results.
7. Source inspection confirms scope reads, resets, resizing, and worker writes are serialized by the meter mutex; worker delivery only tries the lock and drops a sample when busy. This is a source review plus indirect mixer exercise, not a dedicated scope failure-injection test. Spectral calibration/transform efficiency remain S3/S4 work.

8. Final capture fixture passes AddressSanitizer (`/tmp/daw-s1-capture-asan.log`). `make` succeeds with no warnings in this invocation (`/tmp/daw-s1-lifecycle-final-build.log`), and `git diff --check` passes. All changes remain uncommitted.

## 7. Parameter and FX transaction checkpoint

1. Gain/pan/mute/solo and EQ setters now restore prior control values when publication preparation fails. They enforce the creating-thread mutation contract. Numeric validation rejects non-finite gain/pan/active EQ inputs, negative gain, and out-of-range pan.
2. FX parameter changes snapshot previous values/modes and hold the control mutex through preparation or rollback. Structural FX operations prepare a candidate manager, keeping the original manager on failure. Render-state adoption still preserves compatible DSP history; cloning does not copy concurrently running DSP state.
3. `test-engine-parameter-transaction` forces preparation rejection and verifies editable and active state, FX add/remove/reorder/bypass rollback, exact IDs/order, successful retry, invalid values/indices, explicit zero gain, and preservation of a prior accepted pending revision. This fixture injects rejection at the publication boundary; partial clone allocation cleanup has separate coverage in `test-effects-revision`.
4. `make test-engine-parameter-transaction test-stable test-legacy` passes (`/tmp/daw-s1-parameter-regression.log`). The final pending-revision assertion was added afterward and passes under ASan together with `test-engine-mix-ownership` (`/tmp/daw-s1-parameter-asan.log`).
5. Gain/pan UI snapshots only advance after accepted edits. Broader EQ UI, session/undo failure propagation, coherent tempo/status, clip/track structural transaction handling, and zero-gain undo/clipboard integration remain open. S1 is not complete.

6. ThreadSanitizer passes the parameter transaction and concurrent mixer fixtures (`/tmp/daw-s1-parameter-tsan.log`). The final normal parameter test also passes (`/tmp/daw-s1-parameter-final-test.log`). Gain/pan UI changes are source/build validated; no manual interaction acceptance is claimed.

7. Final `make` succeeds (`/tmp/daw-s1-parameter-final-build.log`); `git diff --check` passes. No commit or deployment was performed.

## 8. Tempo and recording isolation ownership

1. `engine_set_record_armed_track` now prepares the matching source revision before accepting the edit; failure restores the previous armed track. The parameter transaction fixture verifies rejection and rollback.
2. Tempo mutation is control-thread-only, rejects non-finite inputs, normalizes sample rate to the project rate, and transfers the complete validated tempo through the existing command FIFO. Offline seek and loop mutation now enforce the same producer identity as their queued paths.
3. Command tests verify offline tempo, project-rate normalization, no control-side mutation of queued worker tempo, worker adoption, overflow rejection, and wrong-thread tempo/seek/loop/record-arm rejection. `make test-engine-command-delivery test-engine-parameter-transaction` passes (`/tmp/daw-s1-tempo-tests.log`). Recording regression also passes (`/tmp/daw-s1-transport-controls.log`).
4. `make` succeeds (`/tmp/daw-s1-tempo-build.log`) and `git diff --check` passes. This is targeted functional/build evidence; no new sanitizer or physical-device claim is made for this increment.
5. Remaining acceptance work is unchanged: callers must propagate recording-arm rejection; transport requested versus applied state needs a clear snapshot contract; clip/track structural rollback, EQ/undo/session rejection propagation, and zero-gain undo/clipboard integration are still open. The goal remains active.

## 9. Zero-gain and history/capture caller integration

1. `test-timeline-midi-region` exercises real clipboard copy/paste and undo/redo for both audio and MIDI. Each source must render a nonzero baseline. Zero-gain paste and redo, plus track-gain undo/redo, are checked in stored state, offline bounce, and the active playback mixer. This closes the previously pending S1.6 integration proof.
2. Recording setup honors record-arm rejection before declaring a take active, frees its unused queue, and exposes a retryable status. Capture setup closes the newly opened endpoint when arming is rejected. `test-audio-recording` injects rejection for synthetic and device-backed setup, checks cleanup, then retries successfully.
3. Undo/redo no longer consumes or transfers a failed command. Destination capacity is reserved before applying the edit; successful commands alone move stacks. Tests exercise rejected FX parameter edits and corrected retries in both directions. Composite-command partial mutation remains separate unfinished work.
4. Targeted tests pass in `/tmp/daw-s1-undo-history-tests.log`. The initial zero-gain/capture tests also passed ASan before the history-stack change (`/tmp/daw-s1-zero-recording-asan.log`). The full stable/legacy suite and build passed at that earlier point (`/tmp/daw-s1-zero-recording-regression.log`, `/tmp/daw-s1-zero-recording-build.log`); final history-change verification is recorded below when complete.

5. ASan passes the final history-stack, zero-gain, and recording-caller fixtures (`/tmp/daw-s1-undo-history-asan.log`). This checks the exercised ownership paths, not physical audio or all composite undo atomicity.

6. Final `make test-stable test-legacy` and `make` succeed (`/tmp/daw-s1-undo-final-regression.log`, `/tmp/daw-s1-undo-final-build.log`). `git diff --check` passes. S1.6 integration is complete; S1 as a whole remains in progress. No commit or deployment was performed.

## 10. Destructive and track-structure transactions

1. Clip deletion detaches without destroying sources, prepares the replacement, then either restores the original clip/order/activity on failure or reclaims the detached resources on success. Tests check both audio media/samplers and MIDI instruments/notes, including last-clip rejection.
2. Track append/insertion/removal now prepare effect-manager candidates and retain track ownership until publication succeeds. Failure restores track order/count/runtime IDs, effects, and recording isolation. Successful insert/remove remaps the engine recording target; deleting that target disarms it. Render histories still follow stable IDs; stale UI caches are cleared after commit.
3. `test-engine-parameter-transaction` verifies rejection and retry for these operations, original source pointers and notes, unchanged active revision, surviving FX IDs, and recording-index movement. Source/mixer tests also pass after the changes.
4. ASan passes parameter transactions, concurrent mixer edits, and source-lifetime stress (`/tmp/daw-s1-structure-transactions-asan.log`). TSan passes parameter transactions, concurrent mixer edits, and analyzer/restart integration (`/tmp/daw-s1-structure-transactions-tsan.log`). These are instrumented application/support paths with the existing SDL/vendored instrumentation limits.
5. Remaining S1.4 work includes clip creation/move/trim/overlap and composite edit failure semantics, implicit track growth/capacity-allocation failure handling, app recording target identity, EQ/session/undo rejection propagation, and coherent requested/applied transport status. Passing structural deletion tests does not close those paths. S1 remains active.

## 11. Stable audio-source registry ownership

1. A new registry-growth test reproduced a stale-pointer bug: metadata was stored in a reallocating array while clips retained pointers to its entries. The pre-fix test failed with `registry growth relocated a borrowed source` (`/tmp/daw-s1-registry-before.log`).
2. Metadata objects are now separately allocated behind a growable pointer table. Registry lookups/creation are control-thread-only. Clear detaches editable clip metadata references before freeing entries; engine destruction frees all remaining entries. Render media ownership remains independent.
3. The source-lifetime fixture creates 128 distinct metadata entries, verifies the original address and path, clears the registry while a clip remains, then runs the existing render-after-delete and 2,000-cycle source stress. Post-fix source and parameter/structure tests pass (`/tmp/daw-s1-registry-after.log`).
4. Stable/legacy and app build passed after the track transactions but before this registry follow-up (`/tmp/daw-s1-track-transaction-regression.log`, `/tmp/daw-s1-track-transaction-build.log`). Final combined checks are recorded below when complete.

5. Combined ASan passes source registry/lifetime, parameter/structure rollback, and concurrent mixer tests (`/tmp/daw-s1-registry-structure-asan.log`). Combined TSan passes source lifetime, concurrent mixer, and analyzer/lifecycle tests (`/tmp/daw-s1-registry-structure-tsan.log`). These checks follow the final metadata ownership changes.

6. Final `make test-stable test-legacy` and `make` succeed after the registry/track/clip ownership changes (`/tmp/daw-s1-registry-final-regression.log`, `/tmp/daw-s1-registry-final-build.log`). `git diff --check` passes. All work remains uncommitted; no installation or physical-device acceptance is claimed.

## 12. Capacity allocation and snapshot layout

1. Track/meter capacity growth previously committed arrays incrementally and changed snapshot row strides without relocating the second row. It now prepares all six track/meter arrays, then transactional scope storage, before committing any parent pointer/capacity.
2. Scope growth initializes separate new banks and frees all partially constructed rings on failure. Existing banks transfer their queue ownership only after successful preparation. Empty newly reserved track slots do not allocate EQ buffers; activating an old spare slot releases its prior unused EQ buffers before initialization.
3. The expanded transaction fixture injects failure at each of seven array allocations and every new scope ring, checks original array pointers/capacities after every rejection, then verifies track identity, both snapshot rows, and an existing scope sample after retry. Normal targeted tests pass (`/tmp/daw-s1-capacity-tests.log`).
4. ASan passes the expanded transaction and concurrent mixer tests (`/tmp/daw-s1-capacity-asan.log`). Final TSan and regression/build outcomes follow below. Broader compound edits and implicit track growth remain open; this does not complete S1.

5. TSan passes the expanded capacity transaction, concurrent mixer, and analyzer/lifecycle tests (`/tmp/daw-s1-capacity-tsan.log`). The final normal stable lane passes; final legacy/build outcomes follow below.

6. Final `make test-stable test-legacy` and `make` succeed (`/tmp/daw-s1-capacity-regression.log`, `/tmp/daw-s1-capacity-build.log`); `git diff --check` passes. No commit or deployment was performed. S1 remains active.

## 13. Scalar clip edit rejection and sampler restoration

1. Move, trim, gain, and fade-length setters now report source-publication failure and restore the previous clip descriptor and sampler timing. Moves restore sorted order and preserve the caller's output index on rejection. Setters enforce control-thread access without implicit track creation.
2. Scalar sampler timing updates preserve existing automation storage. Fade bounds use subtraction after clamping, avoiding unsigned addition overflow; non-finite gain is rejected and explicit zero remains valid.
3. The parameter transaction fixture rejects MIDI/audio moves, trims, and gains plus audio fades. It checks original identities, timing, gain, fade metadata, output index, and exact sampler output against an asserted audible baseline. It also checks a successful maximum-integer fade request is bounded to clip duration.
4. ASan passes parameter transactions, source lifetime, and timeline MIDI/clipboard tests (`/tmp/daw-s1-clip-scalar-asan.log`). TSan passes parameter transactions, 2,000 live source-edit cycles, and 300 concurrent mixer-edit cycles (`/tmp/daw-s1-clip-scalar-tsan.log`). These are exercised application/support paths, not complete vendored instrumentation or physical-device acceptance.
5. Clip creation, MIDI/automation and compound edit atomicity, caller rejection propagation, app recording target identity, and coherent transport status remain open. Fade-curve DSP behavior remains S3. S1 is not complete.

6. Final `make test-stable test-legacy` and `make` succeed (`/tmp/daw-s1-clip-scalar-regression.log`, `/tmp/daw-s1-clip-scalar-build.log`); `git diff --check` passes. No commit or deployment was performed.

## 14. Complete clip automation candidates

1. All public clip-automation mutation paths now prepare independent lane storage and replacement sampler/instrument objects before requesting a render revision. Failure discards the candidate; success releases the previous control-owned lanes and sources. Existing worker revisions own independent sources throughout. Invalid point updates no longer create empty lanes, and mutating APIs require an existing clip on the control thread.
2. Lane copy/replacement and clip copy/snapshot helpers propagate allocation failures and copy before releasing old storage, including aliased inputs. Sampler automation replacement now returns a boolean and retains existing lanes on failure. Point outputs are exposed only on successful publication; borrowed lane/source pointers expire after accepted replacement.
3. Parameter tests exercise publication rejection for lane creation, point insertion/update/removal, lane-point replacement, and whole-lane replacement on audio and MIDI clips. They assert original pointers, point data, source objects, active revision, output indices, and exact audible baseline samples survive rejection. Candidate point/lane/sampler allocation failures are injected individually; snapshot failure and self-copy are checked. Successful audio volume and MIDI instrument-level changes reduce the rendered output as expected.
4. The source reader stress now replaces automation during each of 2,000 live add/fade/trim/automation/delete cycles. ASan passes parameter transactions, that stress, and timeline/clipboard integration (`/tmp/daw-s1-automation-asan.log`). TSan passes parameter transactions, source stress, and concurrent mixer edits (`/tmp/daw-s1-automation-tsan.log`). These are exercised application/support paths with existing SDL/vendored instrumentation limits.
5. Processing audit: `instrument_params_at_frame` skips targets that do not map to instrument parameters. Generic MIDI volume/pan lanes therefore do not currently affect this source renderer. This S3 gap is retained explicitly; the S1 accepted-output test uses instrument-level automation for MIDI and volume automation for audio.
6. MIDI note/parameter mutations and track-instrument automation still discard or mutate original data before checking publication. Clip creation/composite edits, caller integration, recording target identity, and transport snapshots also remain. S1 remains active.

7. Final `make test-stable test-legacy`, `make`, and `git diff --check` pass (`/tmp/daw-s1-automation-regression.log`, `/tmp/daw-s1-automation-build.log`). No commit or deployment was performed.

## 15. MIDI notes, instrument settings, and track automation

1. MIDI add/update/remove/set-notes now prepare independent note lists and commit only after successful revision publication. Rejected edits retain original note allocation and output indices. Non-finite velocity is rejected by the note model.
2. Track enabled/preset/parameters and clip inheritance/preset/parameters restore previous metadata on publication failure. Instrument inputs are checked for finite values; the individual parameter API also validates its parameter identifier. Mutation requires the control thread and an existing track/clip, with no implicit track growth.
3. Track-instrument automation replacement copies complete lanes before releasing old storage. Per-lane edits stage borrowed descriptors and use the same whole-lane transaction. Publication/allocation failure retains original lane ownership; aliased input remains valid during preparation.
4. Parameter tests inject note allocation and publication failure, track automation lane/point allocation failure, and scalar instrument rejection. They verify original notes/settings/lanes, unchanged active revision and audible samples, successful clip-level and inherited track-level changes, aliased automation retry, new-lane retry, and silence after accepted note removal.
5. ASan passes parameter transactions, 2,000 concurrent audio/automation/MIDI edit cycles, MIDI instrument rendering, and timeline/clipboard integration (`/tmp/daw-s1-midi-asan.log`). TSan passes parameter transactions, the expanded source stress, and concurrent mixer edits (`/tmp/daw-s1-midi-tsan.log`). These remain exercised software paths with existing SDL/vendored instrumentation limits, not physical-device acceptance.
6. Remaining S1 work: clip creation/duplication/cross-track/overlap and composite transactions; rejection propagation in UI/session/undo callers; app recording target identity; coherent requested/applied transport snapshots; final requirement-by-requirement acceptance. S1 is not complete.

7. Final `make test-stable test-legacy`, `make`, and `git diff --check` pass (`/tmp/daw-s1-midi-regression.log`, `/tmp/daw-s1-midi-build.log`). No commit or deployment was performed.

## 16. Creation, duplication, and segment ownership

1. Audio/MIDI additions, audio duplication, and segment creation now prepare a separate clip descriptor array. Existing sources remain borrowed until commit. This also removes the duplication/segment hazard of retaining a source pointer across reallocation of its owning clip array.
2. Publication rejection destroys newly prepared resources and preserves the original clip array/count/order/activity, instrument settings, and caller output index. Implicit track growth uses a candidate effects manager and rolls back track count/resources and manager identity with the failed addition. Spare capacity and source-registry metadata can remain cached; these are not new project clips/tracks.
3. Default automation allocation and sampler automation setup failures now reject creation. Partial sampler failure releases the appended slot's automation. Duplication rejects start-frame overflow; segment media bounds account for the source offset.
4. Parameter tests inject rejected audio/MIDI creation, duplication, segments, and implicit-track additions. They check original descriptor/source/FX identity, counts, and untouched outputs. Creation allocations are individually rejected; successful duplicate/segment/new-track retries check independent source ownership and placement.
5. ASan passes parameter transactions, 2,000 concurrent audio/automation/MIDI edit cycles, and timeline/clipboard integration (`/tmp/daw-s1-creation-asan.log`). Full concurrency/regression/build results follow below.
6. Cross-track/overlap and multi-operation caller transactions, recording-target identity, transport snapshots, and final acceptance remain S1 work. This does not complete S1 or begin S2 saving/recovery.

7. TSan passes parameter transactions, expanded source stress, and concurrent mixer edits (`/tmp/daw-s1-creation-tsan.log`). Final `make test-stable test-legacy`, `make`, and `git diff --check` pass (`/tmp/daw-s1-creation-regression.log`, `/tmp/daw-s1-creation-build.log`). No commit or deployment was performed; physical-device acceptance remains unverified.

## 17. Single-publication overlap edits

1. Replaced the overlap resolver's sequence of independently publishing setters with one prepared track candidate. Changed audio regions own cloned samplers, copied automation, and media references; unchanged clips and the anchor retain their existing sources. Split right regions receive new creation IDs, while surviving originals retain theirs and newer clips retain priority.
2. Rejection discards candidate-owned regions and preserves the original descriptor array, clip ranges, source ownership, active revision, and output index. Acceptance publishes once, then releases replaced control resources. No intermediate trim, removal, or partially initialized split is published.
3. Parameter tests combine split/trim/shift/remove/newer-survivor behavior and check exactly one publication attempt. They inject descriptor/source and automation-point allocation failures, verify unchanged audible output on publication rejection, and validate the complete accepted arrangement. Source stress includes overlap splitting during each of 2,000 audio/automation/overlap/MIDI cycles.
4. The initial ASan run passed the new transaction and source stress fixtures, then found a stale borrowed track pointer in the legacy overlap test's final count reporting after track-capacity growth (`/tmp/daw-s1-overlap-asan.log`). The fixture now reacquires that track; behavioral assertions were retained. Final ASan passes parameter transactions, source stress, legacy overlap, and timeline/clipboard integration (`/tmp/daw-s1-overlap-asan-final.log`).
5. Cross-track and multi-operation caller transactions, recording target identity, coherent transport snapshots, and the final S1 acceptance audit remain open. The individual overlap resolver is now transactional; an outer caller sequence is not automatically atomic.

6. TSan passes parameter transactions, overlap-enabled source stress, and concurrent mixer edits (`/tmp/daw-s1-overlap-tsan.log`). Final `make test-stable test-legacy`, `make`, and `git diff --check` pass (`/tmp/daw-s1-overlap-regression.log`, `/tmp/daw-s1-overlap-build.log`). These remain software-path checks with existing SDL/vendored instrumentation limits; physical-device acceptance is unverified. No commit or deployment was performed.

## 18. Complete cross-track transfers

1. Added an engine move operation that stages both track descriptor arrays and transfers the clip's existing source/note/automation ownership with one publication. It preserves clip creation identity and all descriptor fields, changes placement, and uses the destination's inherited MIDI instrument settings.
2. Rejected moves restore both tracks, audio sampler timing, implicit destination-track growth, and effects-manager identity. Allocation failure before staging leaves both arrays untouched. Output indices are returned only on success. Same-track movement repositions rather than duplicating audio.
3. Timeline drag delegates to the engine move. The undo cross-track caller now stops on rejection rather than continuing field edits on the original track. This does not make a later multi-field undo application fully atomic.
4. Targeted tests pass (`/tmp/daw-s1-move-final-targeted.log`): audio/MIDI rejection, source/destination allocation failure, new-track rejection/retry, one publication, preserved audio-source/creation/note identity, same-track no duplication, and timeline/clipboard regression. The 2,000-cycle source stress includes audio and MIDI transfers out and back during rendering.
5. Compound UI/session/undo transactions and rejection feedback, recording-target identity, transport snapshots, and final acceptance remain open. S1 remains active; S2 saving/recovery is not implemented by this increment.

6. ASan passes parameter transactions, transfer-enabled source stress, and timeline/clipboard integration (`/tmp/daw-s1-move-asan.log`). TSan passes parameter transactions, source stress, and concurrent mixer edits (`/tmp/daw-s1-move-tsan.log`). These are exercised software paths with the existing SDL/vendored instrumentation limits; physical-device acceptance remains unverified.

7. Final `make test-stable test-legacy`, `make`, and `git diff --check` pass (`/tmp/daw-s1-move-regression.log`, `/tmp/daw-s1-move-build.log`). No commit or deployment was performed.

## 19. Recording target identity

1. Arming captures the track runtime identity; finalization resolves its current index before output-path creation and clip insertion. Cancel/reset clears the identity. A removed target reports an error and retains captured samples without inserting into another track at the old index.
2. The synthetic recording integration test inserts a track before the active target and verifies the recording, selection, and undo integration use its new index. A second test deletes the target, creates a replacement at the same index, and verifies rejection, preserved captured frames, and no inserted clip.
3. `make test-audio-recording`, `make`, and `git diff --check` pass (`/tmp/daw-s1-record-identity-final.log`, `/tmp/daw-s1-record-identity-build.log`). This increment has targeted functional/build evidence; no new sanitizer or physical-device acceptance claim is made.
4. Compound caller transactions, coherent transport snapshots, rejection feedback, and final S1 acceptance remain. No commit or deployment was performed.

## 20. Applied transport-state ownership

1. Live play/stop submission no longer writes the worker's playback flag before FIFO consumption. This removes the control-side state change that could take effect ahead of queued commands. `engine_transport_is_playing` reports applied state; offline play/stop consume the batch synchronously, including stop flush/reset.
2. A command test separates acceptance from application for live play and stop and checks synchronous offline behavior. Command delivery, recording, and timeline integration pass (`/tmp/daw-s1-transport-applied.log`). Analyzer lifecycle passes five injected failure stages, eight restarts, and live edits (`/tmp/daw-s1-transport-applied-lifecycle.log`).
3. `make` and `git diff --check` pass (`/tmp/daw-s1-transport-applied-build.log`). This is targeted functional/build evidence, not new sanitizer or physical-device acceptance. Requested/pending transport snapshots and UI handling remain unfinished alongside compound caller transactions and final S1 acceptance. No commit or deployment was performed.

## 21. Accepted playback intent and pending presentation

1. Added serial-tagged play/stop commands and a playback snapshot containing requested/applied serials, requested/applied state, and pending status. A snapshot tolerates worker application before submission returns and does not confuse final intent equal to current state with an empty queue. Safety-stop fallback carries and acknowledges its latest accepted stop token. Rejected commands retain previous intent; shutdown cancels/reset pending state.
2. Keyboard/recording toggles and seek-resume decisions use accepted intent. Audio/recording drain and meter consumers retain applied-state behavior. Transport buttons show a pending ellipsis, with active styling based on applied state. This is command-state presentation, not the S2 consumed/presentation audio clock.
3. Command tests cover pending play/stop, rapid play-stop intent, unchanged serial on rejected play, safety-stop acknowledgement, and 10,000 requests against an independent consumer with monotonic/coherence checks. TSan and ASan pass command delivery, analyzer lifecycle, and recording integration (`/tmp/daw-s1-playback-snapshot-tsan.log`, `/tmp/daw-s1-playback-snapshot-asan.log`).
4. Seek-resume callers were subsequently updated to use the same accepted-intent API; final normal regression/build results follow below. Sanitizer evidence covers the exercised backend/support paths with existing SDL/vendored limits. The pending label has source/build coverage, not manual visual acceptance.
5. Compound caller atomicity, user-facing rejection handling, and final S1 acceptance remain. No S2 saving/recovery work is included.

6. Final `make test-stable test-legacy`, `make`, and `git diff --check` pass (`/tmp/daw-s1-playback-snapshot-regression.log`, `/tmp/daw-s1-playback-snapshot-build.log`). No commit or deployment was performed; physical-device and manual visual acceptance remain unverified.

## 22. Transform undo identity and sorted-index correctness

1. The compound-edit audit found audio transform history using ephemeral sampler identity even when a stable creation ID was available. It also found later transform setters using the old index after a timeline move sorted descriptors. Audio/MIDI transforms now resolve creation identity first, and timeline moves return the new index for subsequent fields. Failed movement stops application.
2. A new integration test captures an audio transform, replaces its sampler through automation, then undoes/redoes a move across a neighbor. It checks target identity, position/gain, unchanged neighbor state, silence on undo, and restored audio on redo.
3. `make test-timeline-midi-region` passes normally and under ASan (`/tmp/daw-s1-undo-identity.log`, `/tmp/daw-s1-undo-identity-asan.log`). `make` and `git diff --check` pass (`/tmp/daw-s1-undo-identity-build.log`). This increment has targeted integration/ownership evidence, not a new full regression, TSan, or manual acceptance claim.
4. The multi-field transform is still not atomic. Remaining work includes complete-state transactions, outer multi-operation transactions, identity migration for other history/caller variants, rejection feedback, and final S1 acceptance. No commit or deployment was performed.

## 23. Atomic track-setting history

1. Added `EngineTrackSettings` and one engine commit for mixer/instrument scalar settings. Gain/pan and every instrument value are validated before mutation. A failed revision restores gain, pan, mute, solo, instrument enabled/preset/parameters together.
2. Track-setting undo/redo delegates to this commit and updates the panel only after acceptance. The existing disabled-instrument history behavior retains dormant instrument configuration. This is scalar snapshot restoration, not a whole-track content transaction.
3. Publication-fault tests assert one attempted revision, original settings/revision retained, and complete retry. History integration verifies rejected snapshots preserve engine/UI/history state, then corrected undo/redo restores all requested fields. ASan passes the parameter transaction and timeline/history suites (`/tmp/daw-s1-track-settings-asan.log`).
4. Complete clip transforms, outer multi-operation transactions, remaining identity migration and rejection feedback, and final S1 acceptance remain. No S2 saving/recovery work is included.

5. Final normal parameter/timeline tests, `make`, and `git diff --check` pass (`/tmp/daw-s1-track-settings-final.log`, `/tmp/daw-s1-track-settings-build.log`). No new full regression, TSan, physical-device, or visual acceptance claim is made for this increment. No commit or deployment was performed.


## 24. Complete clip transforms and bounded S1 closeout

1. Complete clip-transform history now calls `engine_transform_clip` once for placement, region, gain/fades, and MIDI contents/settings. Candidate preparation validates notes against target duration; failed allocation/publication retains original ownership, timing, render revision, and output index. Same-track and cross-track/new-track paths share the transaction.
2. Parameter tests reject audio/MIDI publication and note/descriptor allocations, verify unchanged metadata and sampler output, and exercise complete retries including zero-gain audio and audible MIDI. Timeline history tests reject an invalid whole transform before movement and preserve history; stable identity survives sampler replacement and movement across a neighbor.
3. Fresh ASan parameter/source/timeline tests, TSan parameter/source tests, `make test-stable test-legacy`, `make`, and `git diff --check` pass. See [durable evidence](evidence/s1-closeout.json) for commands, excerpts, log hashes, wrappers, and source fingerprints. The 2,000-cycle concurrent source fixture covers the shared transfer path, not a dedicated complete-transform concurrency scenario.
4. The [reconciled S1 ledger](S1-CLOSEOUT.md) supersedes historical pending lists. Original broad S1 remains partial for outer multi-operation transactions, remaining history identity/feedback integration, and physical/manual acceptance. These are explicit deferrals, not completed requirements.
5. This is the user-authorized bounded stopping point. No more implementation, S2 work, commits, or deployment without a new instruction. No additional production implementation was needed during the closeout verification.
