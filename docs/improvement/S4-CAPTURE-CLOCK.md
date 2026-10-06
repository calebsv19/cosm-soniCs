# S4 capture-clock continuity correction

Current media-loading follow-up: [S4.7a–d](S4.7-MEDIA.md) now implement background preparation and owned insertion. The earlier import limitations below describe this report’s original boundary.

## 1. Problem and execution sequence

S4.6's live capture qualification found 1,280–1,408 missing input frames per five-second dummy-device interval. The queue did not overflow: `engine_get_clock_snapshot` exhausted its bounded reads while output-clock producers/consumers were publishing. Recording advanced its source count and inserted silence instead of retaining those input samples.

1. Separate anchored capture continuity from full diagnostic clock observations.
2. Verify exact samples during diagnostic contention and fail closed across actual transport discontinuities.
3. Repeat short and longer live capture with strict zero-input-gap checks, then sanitizer/regression/build checks.
4. Reconcile the S4.6 finding and produce the [S4.7 media audit and implementation sequence](S4.7-MEDIA-AUDIT.md).

## 2. Implemented contract

`Engine.clock_capture_epoch` is one atomic publication representing a completed, playing, non-looping transport epoch. It is zero before any discontinuity changes clock state, and only publishes the completed epoch after all reset fields and diagnostic sequences have been updated. Ordinary output queue writes and callback observations do not alter it. Stopped/paused/looping transport leaves it unavailable.

An initial recording anchor still requires the existing full clock snapshot. Once anchored, the capture callback normally uses the detailed snapshot for its observed placement/alignment diagnostics. If that bounded read is busy, `engine_capture_epoch_is_current` checks the anchored epoch with one atomic read. A match allows the packet to retain its source-clock sample position without pretending a new presentation observation was available. The packet is not marked as a fresh timing observation. The next valid detailed snapshot resumes alignment diagnostics.

A zero or different epoch halts the take; it never substitutes a stale epoch or stitches different transport segments. The atomic epoch read is the continuity decision boundary: a subsequent transport change can affect the next callback, just as a change after a full snapshot could before this correction. Callback allocation, disk I/O, mutex acquisition, sample ordering, gap placement for genuine queue overflow, and journal durability remain unchanged.

`clock_continuity_frames` counts successfully retained frames through this path. Total/queue loss counters remain visible. The prior `clock_missing_frames` observation stays zero for diagnostic contention because those samples are now retained. Already halted callbacks return before reading clocks or extending the source count. This change does not recalibrate ADC/DAC timing or alter initial-anchor policy.

## 3. Software evidence

The [receipt](evidence/s4-capture-clock.json) binds source hashes, commands, logs and the live matrix.

1. Force the detailed clock read to reject a middle input block; all 384 expected samples remain intact in the finalized WAV, with 128 frames attributed to the continuity path and zero artificial silence.
2. Invalidate the capture epoch while the detailed clock remains unavailable; capture halts and the source count does not grow.
3. Hold both ordinary diagnostic sequences busy; full reads fail while the capture epoch remains readable. Pause, stop, seek, and loop changes each reject the prior epoch. New context agrees with the completed playing/loop state.
4. Preserve existing gap/journal/finalization tests and 300 live transport sequences across 4/8/32-block queue targets.
5. Run nine optimized live captures: three five-second intervals at each of the 4- and 32-block profiles, then three 30-second intervals at 32 blocks. Each uses eight FX tracks, actual SDL dummy input/output, and one-second UI polling gaps. All nine reported **zero total, queue, or clock missing input frames**, **zero missing output frames**, and exact captured/finalized frame counts. The short takes retained 768–1,792 frames via epoch continuity; the longer takes retained 6,400–8,064 frames. Thus the contention path was exercised, not merely absent from the new sample windows.

Final verification passed: targeted recording/transport tests in normal, optimized, AddressSanitizer, and ThreadSanitizer builds; `make test-stable test-legacy`; and final `make`. The receipt records commands and log hashes.

These are nonexclusive-host software observations. Dummy capture pacing is not calibrated physical sample time, and wall-clock test durations do not imply exact nominal ADC counts. Physical timing/listening and sustained maximum-load acceptance remain separate.

## 4. Reuse and next boundary

The existing DAW clock/discontinuity owner already serializes transport changes. This is a DAW-specific capture contract (`reuse-deferred` for a new shared clock abstraction), reusing existing atomics, prepared runtime ownership, and `core_time` observations. Shared libraries, versions, adoption state, and queue defaults are unchanged.

The S4.6 capture-clock rejection finding is corrected at the tested software boundary. S4.7 remains an audited implementation plan: separate metadata/probing and prepared-cache adoption first, then own bounded background requests, project-generation rejection, and user-facing insertion workflows. No S4.7 loader/cache implementation is included in this correction.


## 5. Reproduction

Build profiles sequentially; do not compile during measurements:

```sh
make -f make/performance.mk performance-build test-audio-recording test-engine-transport-clock
python3 tests/performance/run_capture_clock.py \
  --binary build/performance/O2/targets/macOS-arm64/tests/runtime_workload_bench \
  --output /tmp/daw-capture-clock-new-run
```

Choose a new output directory. The runner retains each sample and its executable hash, then removes only successful workload-owned temporary directories identified by the benchmark. The nine cases enforce zero missing input/output and exact finalized duration. Failure artifacts remain available for diagnosis.
