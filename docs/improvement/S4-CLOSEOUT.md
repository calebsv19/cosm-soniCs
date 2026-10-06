# S4 remaining runtime acceptance work

Status: bounded S4 software closeout complete; strict stress timing and physical listening/capture acceptance remain qualified below. Prior failures are retained.

## 1. Numbered implementation and acceptance plan

1. Attribute heavy-workload timing between DSP work, control servicing, and host scheduling; test a bounded correction only where current evidence supports it. Preserve queue policy and audio/DSP semantics.
2. Measure repeated mixed workflows and project/engine replacement in one process, including live malloc and RSS after retirement. Resolve ownership defects if found; document realistic resource limits without using forced allocator purges as proof.
3. Re-run integrated continuity, focused correctness, sanitizers and the default build; identify any physical-device acceptance that remains unavailable.
4. Reconcile S4 disposition and define S5 in functional slices with explicit success/failure behaviors. Do not start a broad UI redesign or MCP implementation in this closeout.

## 2. Shared reuse boundary

Reuse existing core_time observations and existing DAW queue, media-job and prepared-revision ownership. core_sched manages deadlines, core_workers manages pools, and core_wake provides generic orchestration; none substitutes for the existing dedicated SDL audio-render producer. Platform worker scheduling policy and DAW workload acceptance remain app-specific (`reuse-deferred` for a new shared abstraction). No shared library/version/adoption change is planned.

## 3. Timing correction and attributed limits

The dedicated producer now requests `SDL_THREAD_PRIORITY_HIGH` once from its own worker entry, before rendering. This is best-effort ordinary priority, not privileged real-time scheduling. Startup continues if refused. `EngineDiagnostics.worker_priority_status` reports 0 before a live request/after stop, 1 when accepted and -1 on refusal; restarts make a fresh request. The lifecycle test injects refusal on alternate restarts and verifies continued rendering and analyzer publication plus status reset. DSP math, buffer policy and signal history are unchanged.

A serial before/after/after/before 30-second 32-track 96 kHz comparison retained all results. Before maximum busy-worker observations were 15.831 and 9.247 ms; after were 4.844 and 5.919 ms. All four had zero output gaps, and budget counts were 26/11/12/10 in that order. This supports a scheduling-policy improvement but does not isolate the cause of the earlier 98 ms stall or prove a universal speed/deadline guarantee. The profiling run identified ordinary DSP and deliberate queue-full waits; it is not a timing sample. Final profiles exercise the actual main-thread notification queue as well as the audio workers.

The original zero-budget-exceedance gate remains visible and unchanged in runner exit status. Callback continuity and processing-budget telemetry answer different questions: a queued producer can exceed one block's duration while meeting output delivery. Do not relabel retained strict failures as passes.

## 4. Repeated ownership and memory acceptance

Twenty ten-second mixed lifetimes ran serially in one process, each constructing a project, playing audio/MIDI/FX with analyzers, recording, editing, importing/removing converted media, exporting, canceling an export, restoring the saved project and destroying the engine. This also initializes/drains/shuts down the real UI notification queue. The run completed 796 scalar edit pairs, 40 imports, 20 exports and 20 structural pairs; all captures finalized exactly and all owned-media retirement checks passed. There were zero input/output/analyzer losses and one worker-budget exceedance across the instrumented run.

The envelope was declared before execution: after ten warmup lifetimes, additional cleanup live malloc must remain within 8 MiB and RSS within 64 MiB of lifetime ten. It passed. Cleanup RSS over lifetimes 11–20 ranged approximately 185.6–221.0 MiB, versus 232.5 MiB at lifetime ten. Reported live malloc fluctuated approximately 4.7–9.9 MiB over those later cleanups; it did not grow monotonically. No allocator purge or resource-limit increase was applied.

A live `vmmap` attribution snapshot reported 34.3 MiB physical footprint (56.3 MiB peak), 145.9 MiB allocator residency, 32.5 MiB dirty allocator pages and 41.8 MiB allocated bytes. This establishes why raw RSS alone overstated live owned memory in this workload. It does not account for every possible project, certify zero leaks, or guarantee overnight sessions. The external snapshot is recorded as instrumentation in this memory lane, not a pristine timing sample. Full-file decoded active media and the existing decode-admission ceiling remain deliberate resource limits. The 64 MiB per source/converted buffer fits about 175 seconds of stereo 48 kHz float audio; the closeout does not quietly raise it or claim disk-streamed source playback.

## 5. Physical backend boundary

A ten-second silent project opened the current default CoreAudio output at requested 48 kHz / 512 DSP frames with a 2,048-frame target. It serviced 938 callbacks with zero missing output and accepted the worker priority request. The output inventory contained AirPods and built-in speakers; the probe did not establish which named default endpoint was selected. No capture endpoint was opened and no authored sound was played. This is backend-open/callback delivery evidence only; device-specific listening, microphone alignment and recovery remain operator acceptance.

## 6. S5 handoff

The proposed next work is in [S5 functional slices](S5-FUNCTIONAL-SLICES.md). Begin with common application actions, atomic compound editing, stable undo identity and truthful failure feedback; then transport/recording, arrangement/inspector, mixer/effects, analysis interpretation and file-operation visibility. The optional MCP adapter comes after common action semantics, beginning read-only. Only acceptance-discovered input defects were corrected here; broad S5 and MCP implementation remain future work.


## 7. Evidence and documentation map

- Scheduling source and refusal/restart test → engine README, this report, current truth and implementation ledger.
- Matched priority samples, sustained profiles and lifecycle receipt → `evidence/s4-closeout*.json` and performance reproduction instructions.
- Physical backend probe → explicitly silent/default-endpoint scope in the receipt; listening/capture alignment stays separate.
- Current undo/UI/media source plus prior real-app capture → S5 functional slices and future intent; proposed behaviors are not implementation claims.
- HEAD~1..HEAD and dirty worktree collection plus bounded eight-item MemDB retrieval → private closeout. Retrieved older release/scaffold notes are context only; current source and new receipts govern this status. No memory mutation.

## 8. Final integrated profiles

The serial optimized profiles ran 120 seconds heavy dummy 96 kHz, 60 seconds paced 96 kHz, 120 seconds mixed 48 kHz / 128 frames, and 60 seconds mixed 48 kHz / 512 frames. All four passed workflow and continuity checks with zero missing output and zero analyzer drops; mixed captures also lost no input. Both mixed profiles passed the strict gate. Heavy dummy reported one busy-worker budget exceedance (1.454375 ms maximum); paced 96 kHz reported 18 late consumer periods despite zero producer budget exceedances. The runner correctly returned 1: two of four strict gates failed. This is bounded profile qualification, not universal hard-real-time completion. UI notification queues drained completely with high-water three; reported maximum latency includes initial 600 ms warmup and synchronous export, and is not GUI frame latency.

## 9. Acceptance-discovered input fixes and GUI readback

Quick Play/Stop clicks and Space presses previously depended on frame-sampled input. They now dispatch from SDL input events, with Space repeat suppression and existing text-focus guards. Save/load modals now consume unhandled events, preventing letter shortcuts such as R from starting recording behind the dialog. The deterministic input-delivery test covers these boundaries.

The isolated native app rehearsal used dummy audio and a synthetic nine-track, 250-clip project. Play and Space pause were observed. An initial save succeeded but reopen failed after the modal shortcut leak had created an empty retained recording; that failed attempt is not counted as restore success. After the fix and a clean restart, typing `S4 Rehearsal Retest`, saving and loading succeeded, with explicit Project saved/Project loaded log readback and no capture opening. Native close exited successfully. The temporary wrapper was not installed or released.

Remaining S5 findings: duplicate project-list entries in the isolated setup, load-dialog dismissal on failure, discoverable cancellation/recovery of a retained failed take, remaining frame-polled gestures/shortcuts, compound-edit atomicity, and crowded compressor/limiter labels. Their complete resolution is outside this bounded acceptance correction.

Timing/lifecycle receipts precede the final input-only fixes; the engine scheduling behavior is unchanged. The final application/source identities and regression outcomes are recorded separately in the combined receipt. Optimized measurements do not change the default build policy.

## 10. Validation boundary

Optimized, AddressSanitizer and ThreadSanitizer builds passed the focused input-delivery and analyzer-lifecycle tests, including five injected startup failures, eight restarts and alternating priority refusal. Both sanitizers completed two integrated lifetimes without reported sanitizer findings. Instrumented runs can lose output deadlines and report allocator values that do not describe ordinary live allocation; they are correctness checks, not continuity or memory-envelope qualification. The uninstrumented receipts above carry those claims.

Final `make -j4 test-stable test-legacy` and required default `make` passed. Python harness compilation and `git diff --check` passed. Documentation synchronization checked 76 documents with zero stale-reference findings. The combined receipt records final binary/source identities and log hashes. No commit or release was made.

## 11. Handoff reconciliation

The subsequent S4 handoff review rechecked this disposition against the implementation ledger and retained closeout receipts. Fresh `make -j4 test-input-delivery test-engine-analysis-lifecycle` and default `make` passed on the current worktree (logs: `/tmp/daw-s4-final-reconcile-tests.log` and `/tmp/daw-s4-final-reconcile-build.log`). This focused rerun is a current regression/build check, not a repeat of the historical timing, memory, native UI or physical-backend measurements. Existing S5 edits remain outside the S4 performance receipt identity. Remaining strict timing and physical acceptance limits are unchanged; S5 priorities remain the functional slices linked above.
