# S4 runtime workload measurements

This opt-in harness measures existing production paths. It does not change engine behavior, enforce host-specific speed thresholds, or join the stable correctness lane.

## 1. Build and run

From the repository root:

```sh
make -f make/performance.mk performance-build
python3 tests/performance/run_workloads.py \
  --binary build/performance/O2/targets/macOS-arm64/tests/runtime_workload_bench \
  --output /tmp/daw-performance-new-run --repeats 3
```

Use a new output directory. The runner refuses to overwrite samples, runs fresh processes sequentially in reproducibly shuffled order, records every failure, and cleans only the unique temporary directories reported by its own workload processes. Each workload has a 120-second timeout. `--group smoke`, `comparison`, `followup`, or `scheduling` selects smaller sets. The followup group repeats valid sparse MIDI fixtures and same-track overlap. The scheduling group includes matched audio/MIDI/overlap/control workloads and live MIDI/edited-arrangement cases for S4.2–S4.3.

`make -f make/performance.mk PERF_OPT_LEVEL=0 performance-build` builds a separate unoptimized comparison executable under `build/performance/O0/`. The default application C flags currently have no optimization flag; this opt-in build does not change that policy. Assertions remain enabled; neither profile uses fast-math. Build normal, optimized, and sanitizer profiles sequentially: vendored shared-library build directories are reused. Do not compile or run another benchmark concurrently with measurements.

## 2. Workloads and interpretation

1. Audio rendering: 8/32/64 tracks; sparse arrangements with 32 clips per track; eight overlapping clips per track; 64/128/512-frame blocks and a 96 kHz effects case. All audio clips reuse one immutable eight-second stereo WAV. Sparse clips begin 16 seconds apart; only the first per track is active in the measured range. This isolates scheduling, not distinct-asset streaming.
2. Effects: four production effects per track (Gain, Compressor, Delay, Limiter), enabled with default parameters. No master chain is added. MIDI: Pure Sine with 8/32 simultaneous notes per track, or one active note plus 127/1023 future notes. Gain is scaled by track count. Instrument signal checks reject silent/nonfinite fixtures.
3. Direct live mixer: 64 untimed warmup blocks, then 1,200 timed blocks, visiting frame positions modulo four seconds without transport resets. This isolates steady mixer cost and is not a seek/loop correctness test. Timing excludes fixture construction and output validation. Empirical nearest-rank percentiles are within a process; report medians/ranges across three processes, not pooled p99 or universal worst cases. Twenty public gain edits are timed separately while stopped.
4. Live cases: actual engine worker plus SDL dummy output callback, 600 ms startup exclusion, then at least four seconds of observation. Compare effects alone, analyzers enabled, polyphonic MIDI, concurrent synchronous export, and scalar edits approximately every 20 ms. Counter deltas exclude startup; maxima are explicitly lifetime values. The default queue can hide stalls. SDL dummy pacing is not calibrated DAC timing and may consume faster than the requested rate.
5. Recording: production queue, drain, journal append, and sync with synthetic stereo input. Drain 100 ms of source frames at a time, accelerated for 10/60 seconds of content; the live recording case waits 100 ms between drains for 10 seconds of content. This is not hardware input, a real capture callback, a full recording-finish/import/UI run, or exact real-time capture pacing.
6. Export: 10/120-second buffers from 32 audio tracks, plus concurrent 30-second export with 32 effect chains. Only the first eight seconds contain authored source input; effect decay and subsequent silence still traverse the common renderer. Output allocation remains duration-sized. PCM16 write timing includes the existing durable publication path. This is not a fully occupied two-minute song.
7. Import: generated 30-second float WAV, native 48 kHz or 44.1-to-48 kHz conversion. Fixture creation precedes timing and warms the file cache. No cold-disk, MP3, distinct-media cache eviction, or background decode claim.
8. Analyzer kernel: the real Hann-windowed logarithmic tone analysis helper over 2,048 samples and 256 bins; two warmup windows and 20 timed windows. Live cases separately measure both analyzer workers and publication/drop counters.

macOS RSS is process resident memory, not live allocation size. Peak RSS includes fixture construction and allocator retention. Exact output/take buffer byte counts are recorded separately; a large RSS sample alone is not a leak. These short local workloads are a baseline for choosing work, not sustained performance, physical-device, GUI responsiveness, or all-effects certification.

The initial audit and proposed implementation sequence are in [S4 Runtime Audit](../../docs/improvement/S4-RUNTIME-AUDIT.md).

S4.2/S4.3 results and their limits are tracked in [the implementation ledger](../../docs/improvement/S4-IMPLEMENTATION.md). The retained MIDI scheduling run predates S4.4a compact meter storage. Use a fresh output directory and rebuild the optimized binary when comparing later source snapshots.


## S4.4b/S4.5 reproduction

`--group queue --queue-blocks 32` selects six live cases: MIDI, FX plus analysis, playback plus export, repeated scalar edits, mixed scalar/structural edits, and recording. Repeat sequentially with `--queue-blocks 8` and `--queue-blocks 4`, each in a fresh output directory. Use three repeats per profile. The runner records the queue request in metadata and passes it through `DAW_BENCH_QUEUE_BLOCKS`; this environment variable belongs only to the benchmark fixture. Configure the application through `output_queue_blocks` in its config/project instead.

The mixed-edit case alternates gain/pan and adds/removes an empty track every ten edits. The original scalar-edit case remains unchanged for comparison against earlier receipts. Worker-cycle counters include service-only iterations and all busy work; render-only counters retain their narrower historical scope. Intentional sleeps and physical wake/presentation deadlines are outside these measurements. Queue-profile runs are sequential, randomized within each profile, without exclusive host access.

The additional mixed-edit workload increased the pre-S4.6 full group to 31 configurations. The historical S4.1 audit remains its recorded 30-configuration baseline.


## S4.6 reproduction

Use `--group streaming --queue-blocks 32 --repeats 3` with a fresh output directory after rebuilding. Eight configurations cover accelerated recording at 10/60 seconds, retained buffered export and streamed export at 10/120 seconds, live FX playback plus streamed export, and actual SDL dummy timeline capture while UI polling pauses for one second. The full group now contains 35 configurations.

The buffered export API intentionally still returns an owned full-size buffer; it is the matched memory reference. Streamed export measures both passes together, samples process residency every 48,000 work frames, and reports the two explicit block-buffer bytes independently. Recording `take_used_bytes` now describes retained preview samples, while `recorded_frames` describes the complete journal. A fixed eight-second source fixture is reused across export durations; longer ranges include silence/tails beyond authored source ends. This isolates output-duration memory, not continuously active long-form content or distinct-asset cache pressure. Capture uses software dummy devices; no physical timing claim follows.

See [S4.6 contracts and evidence](../../docs/improvement/S4-STREAMING.md). Finished-media insertion/decode is outside these pipeline memory measurements.

The S4.6 live capture probe separately records total missing input, queue losses, and clock-read rejection losses. Acceptance requires no storage queue loss, exact finalized/captured source-clock length, and zero missing output frames in the retained runs. Nonzero clock-read losses remain a reported capture-quality failure; they are not hidden by the pipeline gate. The initial zero-total-gap assertion failures are retained beside the attributed rerun in the S4.6 evidence bundle.


## Capture-clock follow-up

`python3 tests/performance/run_capture_clock.py --binary build/performance/O2/targets/macOS-arm64/tests/runtime_workload_bench --output /tmp/daw-capture-clock-new-run` runs nine strict capture cases: three 5-second intervals at each of 4/32 queue blocks, then three 30-second intervals at 32 blocks. Current capture qualification once again requires zero total/queue/clock input losses, alongside exact final duration and zero missing output. `capture_clock_continuity_frames` reports accepted frames preserved by the current-epoch check while detailed observations were busy. The earlier S4.6 loss-attribution runs remain historical evidence; the follow-up [closes that software finding](../../docs/improvement/S4-CAPTURE-CLOCK.md).


S4.7 adds `make -f make/performance.mk performance-media-import` and `media_import_bench 48000` / `media_import_bench 44100`. Each process uses four distinct 30-second assets, cold/warm scans and imports, three removal/reload cycles, and eight FX tracks with real dummy capture/output. Results separate admission/poll time, ready/reserved/pinned bytes, RSS and missing input/output frames. A 200 ms consumption pause is included in batch time. Run profiles sequentially; see [S4.7 evidence](../../docs/improvement/S4.7-MEDIA.md).

For retained six-process runs, use `python3 tests/performance/run_media_import.py --binary build/performance/O2/targets/macOS-arm64/tests/media_import_bench --output /tmp/daw-media-new-run`. The output directory must be new; every case's text and JSON receipt remain available on failure. The implementation receipt records the exact temporary runner used for the accepted measurements.


## S4.8 reproduction

`make -f make/performance.mk performance-analysis performance-build` builds the matched direct/prepared kernel benchmark and the production live harness. Run `python3 tests/performance/run_analysis.py --kernel build/performance/O2/targets/macOS-arm64/tests/analysis_compute_bench --live build/performance/O2/targets/macOS-arm64/tests/runtime_workload_bench --output /tmp/daw-analysis-new-run`. The output directory must be new. Eighteen matched kernel processes cover both analyzer shapes at 44.1/48/96 kHz; six live processes enable both analyzers across 32 FX tracks at 48/96 kHz. `--controls-only` disables analysis in the matched live cases; `--live-only --tracks 8` runs the lighter enabled cases.

The runner retains failures and exits nonzero for parity/CPU regression, output gaps, whole-worker deadline misses, missing publications or analyzer drops. This strict performance gate is separate from stable correctness tests. The S4.8 receipt retains enabled and disabled playback failures instead of treating kernel speedup as full integrated qualification. Public analyzer diagnostics expose approximate backlog and lifetime transform elapsed timing; benchmark CPU counters are separate. The existing `analysis` mode now measures the prepared production kernel. For unoptimized matched kernels, `make -f make/performance.mk PERF_OPT_LEVEL=0 performance-analysis` builds only the standalone kernel executable. See [S4.8 evidence and limits](../../docs/improvement/S4.8-ANALYSIS.md).

## S4.9 reproduction

Build `make -f make/performance.mk performance-sustained all`, then run `python3 tests/performance/run_sustained.py --binary build/performance/O2/targets/macOS-arm64/tests/sustained_acceptance_bench --output /tmp/daw-sustained-new-run`. A new output directory is required. The fixed matrix runs sequentially for about eleven minutes plus fixture/finalization time; `--smoke` selects a ten-second mixed harness check. Never compile or launch another workload during measurement.

The harness takes `mode rate block tracks seconds analysis queue_blocks`; mode is `dummy`, `paced`, or `mixed`. The paced mode pauses SDL output and drives the production callback at nominal monotonic intervals, reporting late wakeups without catch-up bursts. It is a software-consumer control, not a hardware backend. Mixed mode uses actual SDL dummy capture/output and public import/edit/record/export/session paths. All temporary projects/media are created beneath a unique `/tmp/daw-s49-*` root printed to stderr and retained for inspection.

Results distinguish workflow assertions, missing-frame/analyzer continuity, and the stricter zero-worker-budget/zero-paced-lateness gate. The runner returns nonzero when any strict gate fails while retaining all measurements. A worker iteration exceeding one nominal block duration is not by itself a physical delivery miss; the queued callback and physical clock are separate boundaries.

For the separate memory-attribution follow-up, run `DAW_BENCH_HEAP=1 build/performance/O2/targets/macOS-arm64/tests/sustained_acceptance_bench mixed 48000 128 8 180 1 32`. This adds macOS malloc-zone observations and must not be compared as an identical timing workload. See [S4.9 acceptance](../../docs/improvement/S4.9-ACCEPTANCE.md) for retained failures, source identities and remaining gates.


## S4 closeout reproduction

1. Build `make -f make/performance.mk performance-sustained performance-lifecycle performance-device all`.
2. Run `python3 tests/performance/run_s4_closeout.py --binary build/performance/O2/targets/macOS-arm64/tests/sustained_acceptance_bench --output /tmp/daw-s4-closeout-new`. This is a six-minute serial matrix with UI notification delivery enabled. It retains workflow, continuity and strict timing outcomes separately and returns nonzero for any strict failure.
3. Run `python3 tests/performance/run_lifecycle.py --binary build/performance/O2/targets/macOS-arm64/tests/lifecycle_acceptance_bench --output /tmp/daw-s4-lifecycle-new --cycles 20`. This separately instrumented lane repeats complete project/engine ownership lifetimes in one process; it includes heap snapshots and real main-thread messages. Its declared warmup/envelope is in the receipt, not a universal memory cap.
4. `device_acceptance_bench` opens the current default physical output for ten seconds with an empty, silent project. It never opens input. Run only when that default output is the intended test endpoint. Enumerated names do not identify the actual selected default. This is callback/backend evidence, not listening or capture alignment.

Never compile or run another workload during timing measurements. `DAW_BENCH_UI=1` in the sustained harness enables actual queue initialization, draining and shutdown; UI-message latency includes the 600 ms startup interval and synchronous control operations. It does not measure a rendered GUI frame. `worker_priority_status` records the best-effort request result, not an OS guarantee. See [S4 closeout](../../docs/improvement/S4-CLOSEOUT.md).
