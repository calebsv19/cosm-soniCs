# S4 implementation ledger

Authorization: proceed through the proposed slices, validating each boundary before continuing. The [S4.1 audit](S4-RUNTIME-AUDIT.md) remains the historical baseline.

## 1. Execution sequence

1. S4.2 audio regions: prepared track-local lookup and whole-silent-block rejection; preserve overlap order, gain evolution, source lifetime, exports, and effect tails.
2. S4.3 MIDI: bound work to candidate sounding/releasing notes while retaining automated gate/release and exact accumulation order.
3. S4.4 edit/meter storage: separate published observations from large DSP histories, then evaluate remaining revision costs.
4. S4.5 queue/deadlines: complete-cycle observations and explicit queue policy before latency claims.
5. S4.6 recording/export: independently closed streaming sub-slices preserving journal/publication/normalization semantics.
6. S4.7 media/cache, S4.8 analysis compute, S4.9 sustained integrated acceptance.

## 2. S4.2 contract

Complete at the software boundary. [Receipt and matched measurements](evidence/s4-region-scheduling.json). Each prepared graph has a sorted track lookup and an insertion-ordered source chain per track. Only audio samplers declare immutable bounds; MIDI/audition/external callbacks remain unbounded. Entire nonintersecting blocks skip sampler callbacks and scratch clearing/accumulation. Boundary blocks still invoke the original sampler. Skipped gain ramps retain the exact repeated-add arithmetic of the original render loop. Unknown callbacks retain their behavior, and uint64-wrapping blocks use the original callbacks.

This is a lightweight track-local scan with interval rejection, not a general interval tree. Metadata checks remain proportional to source count within a track, but inactive sources do not incur sample-sized work. Effects/EQ/pan/latency compensation and track summation still run, preserving tails after source inactivity. Prepared revision mutation, rollback, media pins, and live history transfer are unchanged.

Reuse: `reuse-deferred` for a shared region scheduler. The existing DAW graph/source-plan and sample-ramp machinery own the necessary semantic boundaries; shared time/queue/scheduler/job/worker and data/math modules do not define audio clip spans, gain evolution, or effect-tail policy. No shared library, minimum version, or adoption change.

Acceptance requires exact bounded/reference graph parity for interleaved tracks, overlap summation, endpoints, seeks, resets, gain ramps, uint64 wrap, allocation rollback, and zero graph heap calls during configured-size rendering; existing fade/control/export/live-lifetime regression; selected sanitizer checks; matched optimized measurements; and final `make`.


S4.2 acceptance passed: focused exact graph parity, existing export/fade/control checks, stable and legacy lanes, ASan graph/export/control, TSan graph plus 2,000 live source-edit cycles and 300 live mixer-edit cycles, and `make`. Export preparation failure coverage increased to 54 injected boundaries. The matched optimized sparse 32×32 median fell from 0.588416 ms to 0.060000 ms (9.81×); the active-only 32-track case in the same new run was 0.059000 ms. Other matched control workloads changed by approximately 3–5%, consistent with the nonexclusive host boundary; no universal speed claim is made.

## 3. S4.3 contract

Implemented: select potential sounding/releasing notes once per block, retaining original note accumulation order and original per-sample automated envelope checks. Use the maximum supported release bound for candidate selection so a later release-automation increase cannot incorrectly retire a note. Cache only invariant base pitch; vibrato and oscillator/envelope math remain unchanged. Reserve candidate workspace with note descriptors before live updates, and use the prior unscheduled render loop as an exact-output test reference across presets, channels/rates, irregular blocks, seeks, export boundaries, and unsigned timeline wrap.


The S4.3 focused reference comparisons, stable/legacy regression, ASan, and TSan checks passed. The optimized scheduling group passed all 33 samples across 11 configurations. Compared with the S4.1 baseline, MIDI 8×32 median block time changed from 0.735250 to 0.487000 ms (1.51×); one active note among 1,024 stored notes changed from 0.053250 to 0.006000 ms (8.88×). The one-note control was 0.005542 ms. Dense MIDI 8×8 changed from 0.183583 to 0.129251 ms. Audio/FX/overlap controls remained close to baseline. Short dummy-device MIDI and structural-edit workloads reported zero missing frames and zero render-only over-budget blocks in all three repeats. These are optimized local workload results, not physical latency or sustained device qualification. See [the S4.3 receipt](evidence/s4-midi-scheduling.json).

Selection still scans stored notes once per block; it is not a fully event-driven scheduler. Prepared descriptors are larger and cache base pitch during preparation. Conservative candidates retain maximum supported release even when the current release is shorter. Instrument samples, automated envelopes, note ordering, and existing oscillator behavior retain exact reference parity in the test matrix.

## 4. S4.4a compact published meter storage

Published banks now contain only effect IDs, public snapshots, counts, and track identity. Mutable LUFS histories remain in the worker-owned DSP banks. The public API, locking, opportunistic publication, identity rejection, and two-buffer layout are unchanged. All allocations, growth copies, and clear operations use the compact type.

On this arm64 build, each published bank changes from 226,832 bytes to 9,104 bytes, a 96.0% reduction. Two published banks save 435,456 bytes per allocated track capacity slot, plus the master pair. This is an exact storage-layout saving, not a process-RSS claim. Full control/render history banks, prepared effects, scope buffers, and media remain separately allocated.

Stable/legacy, focused AddressSanitizer and ThreadSanitizer, and final `make` passed. Acceptance covers all effect slots and alternating published frames, exact public payloads, stale track identity, removed effects, clear behavior, growth row strides and payloads, allocation-failure rollback/retry, and concurrent live editing. See [the S4.4a receipt](evidence/s4-meter-snapshots.json).

## 5. S4.4b and S4.5 completion

[The edit/deadline report](S4-EDITING-DEADLINES.md) records compact ordered scalar publication, prepared source identity lookup, complete busy-worker telemetry, and explicit callback-aware queue targets. [S4.4b evidence](evidence/s4-mixer-publication.json) includes exact full-revision parity, rollback/retirement, concurrency, and matched edit measurements. [S4.5 evidence](evidence/s4-worker-queue.json) records configuration/project compatibility, transport boundaries, and the optimized 4/8/32-block workload matrix. Both slices are complete at the documented software boundary; physical latency and sustained device qualification are not claimed.

## 6. S4.6 recording/export completion

[S4.6 contracts and measurements](S4-STREAMING.md) cover the two independently verified disk pipelines. Recording now uses a journal worker for actual timeline capture, bounded recent preview snapshots, exact-length streamed finalization, and bounded recovery. Export now uses a single captured render and anonymous two-pass normalization spool, chunked durable WAV publication, cancellation/progress, and bounded waveform packs. The explicit buffer bounce API remains available.

The software memory/I/O boundaries are complete. Recording preview allocation stayed at 512 KiB for accelerated stereo 10/60-second takes; streamed export used 2 KiB of explicit block buffers for 10/120-second ranges. The long buffered export added 46,104,576 bytes of RSS; the streamed runs showed no additional sampled RSS for this fixed project. Spooling increased elapsed time and does not remove source/DSP snapshot memory or finished-media import cost. See [the receipt](evidence/s4-streaming.json) and [both workload runs](evidence/s4-streaming-workloads.json).

**Historical capture finding:** all three initial five-second capture probes failed the zero-total-input-gap assertion. Separate counters then attributed 1,280–1,408 input frames per repeat to the existing bounded clock-snapshot rejection path, with zero storage queue losses. Finalized takes preserved every source-clock interval through explicit silence. This is a successful streaming/preservation gate with an unresolved clock-read quality finding, not proof of gap-free recording.

The subsequent [capture-clock correction](S4-CAPTURE-CLOCK.md) retains anchored samples through ordinary diagnostic contention and invalidates capture across actual transport discontinuities. All nine new live captures reported zero missing input/output frames, including three 30-second intervals. This closes the recorded rejection finding at the software boundary; physical timing/listening and sustained maximum-load qualification remain separate.

## 7. Original S4.9 boundaries (before closeout)

These results describe the original assessment. Section 10 and [S4 closeout](S4-CLOSEOUT.md) govern the current bounded software disposition; they retain, rather than erase, these earlier failures.

1. **S4.7a–d media/cache:** [implementation and qualification](S4.7-MEDIA.md) now cover background metadata/content probing and decoding, prepared cache adoption, cancellation, generation/track-safe publication, library/recording/bounce integration, warm reuse and retired-pin collection. Active sources remain full-file; explicit offline/restore APIs remain synchronous.
2. **S4.9 sustained integrated acceptance:** [assessment completed](S4.9-ACCEPTANCE.md); full qualification remains open after 8/8 workflow, 7/8 continuity and 0/8 strict timing passes. Default optimization policy, physical listening/device acceptance, and GUI performance remain unqualified.

S4.7a–d and [S4.8 analyzer compute](S4.8-ANALYSIS.md) are implemented. S4.9 assessment is complete; its timing/memory/interactive acceptance findings, physical acceptance, release and commit remain separate.


## 8. S4.8 analyzer compute

Worker-owned Hann weights and oscillator coefficients replace repeated per-bin/sample trigonometry while preserving the existing arbitrary logarithmic tonal grid. The 13,824-bin reference comparison had maximum error 0.0000153 dB. Matched optimized CPU improved 4.03–4.36× by configuration median; default-profile kernel comparisons improved about 2.94–2.98×. New consumer diagnostics expose pending/high-water, stale, compute and publication observations without render-thread transforms.

All enabled live cases retained analyzer throughput with zero dropped windows. Strict whole-workload acceptance was not universally achieved: two 32-track runs had output gaps, disabled-analyzer controls also had occasional deadline misses, and one eight-track run had one miss without an output gap. These observations remain an explicit S4.9 investigation, not a hidden success. See [S4.8 contracts, proof and failures](S4.8-ANALYSIS.md).


## 9. S4.9 integrated assessment

The [sustained report](S4.9-ACCEPTANCE.md) retains the complete eight-case 660-second matrix, a separate 180-second heap probe, short leak/sanitizer checks and actual empty/populated GUI frames. All workflow assertions passed; a heavy 96 kHz dummy run lost 8,320 output frames and all eight retained strict timing findings. Enabled analyzers dropped no windows. Repeated imported-media retirement returned to baseline, while elevated process RSS leaves long-session memory qualification open. GUI startup/rendering passed; interactive editing, physical audio and effects-layout refinement remain separate. No production engine changes were introduced in this acceptance slice. See [DAW functionality and subsequent work](DAW-STATUS-AND-NEXT-STEPS.md) for S1 debts, S5 and MCP prerequisites.


## 10. S4 follow-up disposition

[The S4 closeout](S4-CLOSEOUT.md) adds a best-effort high-priority render-producer request with observable refusal and restart behavior, matched scheduling observations, twenty repeated mixed project/engine lifetimes and real UI message delivery, plus a silent default CoreAudio endpoint check. Previous strict timing failures remain historical evidence, and broad physical/listening qualification is separate. [S5 functional slices](S5-FUNCTIONAL-SLICES.md) begin with the remaining S1 compound-edit/history/feedback behaviors before cohesive surface refinement.

Bounded S4 software work is complete. The final four integrated profiles all passed workflow and continuity checks; two passed the strict timing gate and two failed it. The twenty-lifetime memory envelope passed. These results permit the functional S5 work to proceed, but do not establish universal deadline compliance, long-file disk streaming, or physical listening/capture acceptance. S5 implementation already present in this worktree is tracked separately in its implementation ledger.
