# S4.6 — Recording and export disk pipelines

Current media-loading follow-up: [S4.7a–d](S4.7-MEDIA.md) now implement background preparation and owned insertion. The earlier import limitations below describe this report’s original boundary.

Current follow-up: the historical capture-clock finding below is corrected by [the bounded epoch continuity change](S4-CAPTURE-CLOCK.md). Its new strict zero-gap matrix is separate from the original S4.6 receipts.

## 1. Scope and acceptance boundary

S4.6a moves production timeline recording drain/checkpoint I/O onto a storage worker and bounds its live preview. S4.6b streams file exports through an anonymous spool and a durable WAV candidate, preserving the captured render, normalization, and deterministic encoding. The explicit in-memory bounce API remains available to callers requesting an owned sample buffer.

This bounds pipeline memory with respect to **output duration**, for a fixed project, format, and block configuration. Prepared project/DSP state, pinned source media, project metadata, and import of the completed WAV are separate. Finished-asset decode/cache remains synchronous and full-file until S4.7. Finish/cancel can wait for storage-worker retirement; file export remains a synchronous control-thread operation with progress and cancellation. No physical-device, power-cut, listening, or sustained maximum-load qualification is implied.

## 2. S4.6a recording contract

1. Capture callbacks retain the S2 sample-clock packets, complete-packet SPSC publication, dropped-frame accounting, and transport-epoch rules. Separate `queue_dropped_frames` and `clock_missing_frames` counters attribute the unchanged total-loss policy. They perform no disk I/O, allocation, logging, or mutex acquisition. Queue storage remains fixed at setup, sized from a nominal ten seconds of 256-frame packets; shorter callback packets reduce the number of source frames that fit.
2. Actual timeline capture starts one storage worker before starting input. That worker is the sole packet consumer and journal owner. It drains/checkpoints at approximately ten-millisecond polling intervals, subject to storage/scheduler delay. It never holds the snapshot mutex during writes or synchronization. The UI tries to copy a completed snapshot and skips a busy publication instead of waiting on I/O. Raw/synthetic APIs retain their explicit synchronous drain and transport-gating contract.
3. Each preview stores only the newest **65,536 frames**, in chronological order. Total take/checkpoint counts remain independent of preview length. The worker, published snapshot, and UI each own one fixed preview buffer: 768 KiB total for mono, 1.5 MiB for stereo, excluding the fixed packet queue, stack/stdio buffers, and thread state. Manual drains need one preview buffer. Timeline clip extent covers the whole take; the waveform occupies only the retained recent interval. It is not stretched over older audio.
4. Queue gaps still append silence at the source-clock position, including a dropped suffix at Finish. Checkpoint failure halts the producer, publishes an error, and retains the journal. Stopping first quiesces input, then joins the sole consumer before queue or journal ownership changes. Production cancellation drains accepted queued packets unless storage has failed, then closes while retaining the journal.
5. Finish streams checked journal records to a PCM16 candidate with the existing `0xA23` dither seed. It requires the exact expected take length and no torn/corrupt suffix before publishing. A record-boundary truncation therefore cannot silently become a shortened completed take. Strict finalization, stable target identity, synced-media-before-insertion, undo, and transactional clip insertion remain enforced.
6. Explicit recovery streams the valid ordered/checksummed prefix into float32 WAV using fixed buffers. It still reports incomplete tails, preserves the journal, and refuses source/destination aliases. Recovery intentionally permits a prefix; Finish requires the complete expected take. RIFF limits and nonfinite samples remain rejected.

## 3. S4.6b export contract

1. `engine_bounce_range_to_wav` captures one independent prepared plan and holds its media pins. The existing range, preroll, tail, DSP-latency trimming, authored control, and live-state isolation policies are unchanged. The app bounce action and legacy `engine_bounce_range` file API use this path.
2. Pass one renders configured-size blocks into an anonymous temporary float spool while measuring the selected output peak. Pass two reads bounded chunks, divides samples by the same peak only when requested and above unity, and writes the requested PCM16/float encoding. DSP renders only once; there is no second project capture or replay with potentially different live state. Disk space grows with duration; render buffers do not.
3. The shared DAW `WavStreamWriter` retains deterministic dither state across arbitrary append boundaries, checks finite samples/RIFF limits, and patches the actual header in its private candidate before flush, file sync, close, rename, and directory sync. Every failure before rename preserves the previous destination. A failed directory sync remains `DAW_SAVE_PUBLISHED`, distinct from `DAW_SAVE_SYNCED`; the app reports this uncertainty and does not insert the WAV.
4. Progress covers render work (including discarded preroll/latency) plus output-writing frames. A false callback cancels before publication, including at the final progress boundary. The app pumps events for held **Escape** or Quit without dispatching project edits during export. The timeline cursor maps the two-pass fraction onto the selected range. Spool/candidate resources are retired on cancellation and I/O/allocation failure. Progress and sample observers must keep the engine alive; normalized sample observations are provisional until publication succeeds.
5. Optional waveform packs consume normalized chunks instead of a complete bounce buffer. Up to 65,536 min/max points occupy at most 512 KiB. The existing 256-frame resolution is preserved for shorter exports; longer overviews declare a coarser `samples_per_pixel` in the existing header. Tests preserve exact existing short-pack bytes. Optional pack writing retains its existing `core_pack` publication contract and is reported separately from durable WAV success; WAV, pack, and insertion are not a single transaction.

## 4. Reuse and ownership decision

The shared-core governance scan considered `core_time`, `core_queue`, `core_sched`, `core_jobs`, `core_workers`, `core_wake`, `core_kernel`, `core_data`, `core_pack`, and domain/UI modules. Existing time and pack dependencies remain reused. Take placement, silence insertion, checkpoint semantics, preview ownership, range normalization, and WAV publication remain DAW-specific (`reuse-deferred` for a new shared recorder/exporter). Existing SDL thread/mutex lifecycle and the proven DAW SPSC queue avoid introducing a second runtime. `core_pack` accepts contiguous chunk payloads; a bounded overview uses that API without modifying the shared format. No shared-library source, version, minimum, or adoption change.

## 5. Validation and measurements

The [S4.6 receipt](evidence/s4-streaming.json) binds exact commands, logs, source hashes, and optimized samples. Acceptance includes:

1. Uneven multichannel chunk/dither byte parity; five durable-writer failure stages plus postpublication directory uncertainty; invalid/overflowing media rejection; torn journal recovery and strict expected-length finalization.
2. Long-take preview rollover with every finalized sample checked; unchanged gap placement/target/insertion retry tests; actual aligned dummy-device capture; checkpoint progress without UI drains; stalled-sync nonblocking UI polling; worker error publication and join-before-free cancellation.
3. Streamed PCM16/float parity with the buffered renderer, normalization enabled/disabled, preroll/tail/PDC, four cancellation boundaries, six spool I/O failures, allocation failure rollback/media pins, snapshot edits, and continued live callback/worker/clock identity.
4. Existing pack byte parity, bounded twelve-hour overview allocation, incomplete overview rejection, and optional pack-open failure cleanup.
5. Stable/legacy regression, selected AddressSanitizer/ThreadSanitizer and optimized correctness checks, matched optimized workloads, documentation verification, and final `make`.

Optimized measurements completed 24 cases (eight configurations, three repeats); the original run and attributed rerun are both retained in [raw workload evidence](evidence/s4-streaming-workloads.json). For the fixed project and 48 kHz stereo / 128-frame render configuration:

| Measurement | Short duration | Longer duration |
| --- | ---: | ---: |
| Recording retained preview allocation | 512 KiB at 10 s | 512 KiB at 60 s |
| Streamed export explicit render buffers | 2 KiB at 10 s | 2 KiB at 120 s |
| Buffered export observed additional RSS | 0 at 10 s (allocator reuse) | 46,104,576 bytes at 120 s |
| Streamed export sampled additional RSS | 0 at 10 s | 0 at 120 s |
| Buffered render + WAV write, median | 126.34 ms | 379.98 ms |
| Streamed render + spool + WAV write, median | 133.60 ms | 448.00 ms |

Zero additional sampled RSS is an observation, not zero workspace: allocator reuse can hide smaller allocations. The source-defined buffer bounds are the independent memory proof. Disk spooling adds work; the 120-second case used about 18% more elapsed time than buffered render plus write in this run. Playback plus streamed FX export still allocated approximately 33 MB of private fixed-project DSP state. This is expected snapshot cost and does not grow with output duration.

The actual mono capture worker used 768 KiB across its three preview buffers. Its maximum observed UI poll was 0.05875 ms with one-second polling gaps. All six live sample intervals reported zero missing **output** frames; FX export intervals recorded 1–2 worker-budget overruns each. Queue buffering can mask execution overruns.

### Historical finding: capture-clock snapshot rejection

The initial capture workload failed its zero-total-input-gap assertion in all three repeats. The cause was not storage queue overflow. New separate counters attributed all missing input to the existing bounded `engine_get_clock_snapshot` rejection path: **1,280–1,408 frames** per five-second dummy capture interval (nominally 26.7–29.3 ms of input at 48 kHz). Storage queue losses were zero. After stopping capture, every final WAV had exactly the captured source-clock length, including silence for the missing intervals. A deterministic test forces the rejection and verifies the exact silence position/sample sequence.

The attributed rerun therefore passes the **streaming/preservation contract**, not a gap-free capture-quality gate. The counters do not suppress or reinterpret missing input; total loss remains visible. Resolving snapshot contention without accepting samples across transport epochs is a separate bounded correctness follow-up before recording is called production-qualified. Keep this finding ahead of broad workload qualification; do not silently replace the S2 epoch/placement contract with a stale clock fallback.

The workload runner's `streaming` group compares 10/60-second recording and 10/120-second buffered/streamed exports for fixed source fixtures, plus real dummy capture with one-second UI polling gaps and export during live FX playback. These are software measurements on a nonexclusive host; dummy pacing is not physical capture/playback timing.

## 6. Next boundary

The original S4.6 run stopped at its memory/I/O boundary. The subsequent [capture-clock correction](S4-CAPTURE-CLOCK.md) closes the recorded rejection finding at the tested software boundary. S4.7 is background media preparation/cache: decode/resample jobs, cancellation and stale-result rejection, lifetime/pin/eviction policy, and bounded publication into edits. S4.8 analyzer compute and S4.9 sustained integrated acceptance remain later work. Earlier S1 editing-integrity deferrals and physical acceptance remain separate.


Subsequent checkpoint: [S4.8 analyzer compute](S4.8-ANALYSIS.md) is now implemented with calibration/CPU evidence; integrated playback timing findings remain for S4.9. The next-boundary text above records this document’s original closeout.
