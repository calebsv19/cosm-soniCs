# S4.1 — Runtime workload audit and baseline

Historical S4.1 baseline. Subsequent authorization and implemented S4.2/S4.3/S4.4a progress are tracked in [the implementation ledger](S4-IMPLEMENTATION.md); the original measurements and source-bound receipts below remain unchanged.

Status: measurement/audit complete, 2026-09-21. No production engine behavior or default build policy changed. S4 implementation slices below are proposals, not completed optimizations. S1's editing-integrity and physical-acceptance deferrals remain open.

## 1. Scope and method

1. Inspected source traversal, instrument note loops, revision capture/adoption, analyzer transforms, output queue/deadline diagnostics, recording journals, import/conversion, and export allocation/publication.
2. Added an opt-in build and workload harness using the existing production APIs and prepared live mixer. Isolated `-O2 -g` and `-O0 -g` object roots retain assertions and avoid fast-math. Builds and workload processes ran sequentially.
3. Collected three processes per canonical workload: **30 optimized workload configurations / 90 selected successful samples**, plus **five matched unoptimized configurations / 15 samples**. Raw primary/followup/comparison results are retained. The primary 87-sample run had three invalid sparse-MIDI fixtures: future notes exceeded their region. Corrected fixtures and matched one/128-note controls replaced all nine primary sparse-MIDI samples; the followup also added same-track overlap. No engine defect is inferred from those fixture rejections.
4. Ran optimized export, instrument lifecycle, and transport correctness checks, plus the final default application build. Existing software contracts remain the gate for later optimization.

Host: Apple M2, 8 logical CPUs, 16 GiB RAM, macOS 15.7.4, Apple clang 17.0.0. No exclusive machine access, CPU affinity, thermal/power-mode control, or physical audio interface measurement. This is a short local baseline, not maximum capacity or long-session certification.

The [harness guide](../../tests/performance/README.md) specifies fixture content, warmups, timing boundaries, and reproduction commands. [Aggregated metrics](evidence/s4-runtime/summary.json) report medians and ranges of per-process statistics; p99 values below are **medians of three within-process p99s**, not pooled percentiles. [The receipt](evidence/s4-runtime-audit.json) binds artifacts and source to commands and limits.

## 2. Measured render costs

Unless noted: stereo, 48 kHz, 128-frame blocks, optimized profile. Each block has a nominal **2.667 ms** processing budget. Direct mixer timing excludes setup, validation, and device scheduling. There are 64 warmup blocks and 1,200 measured blocks per process.

| Workload | Median block ms | p99 block ms | p99 / nominal budget |
| --- | ---: | ---: | ---: |
| 8 audio tracks, one clip each | 0.014 | 0.015 | 0.6% |
| 32 audio tracks, one clip each | 0.056 | 0.075 | 2.8% |
| 64 audio tracks, one clip each | 0.114 | 0.158 | 5.9% |
| 32 tracks, Gain + Compressor + Delay + Limiter on each | 0.172 | 0.242 | 9.1% |
| 64 tracks, same four-effect chain | 0.354 | 0.426 | 16.0% |
| 32 tracks × 32 sequential clips, only first clip active | 0.588 | 0.711 | 26.7% |
| 8 tracks × 8 overlapping clips, all active | 0.075 | 0.158 | 5.9% |
| 8 MIDI tracks × 8 held notes | 0.184 | 0.230 | 8.6% |
| 8 MIDI tracks × 32 held notes | 0.735 | 0.865 | 32.4% |
| One MIDI track, one active of 1,024 stored notes | 0.053 | 0.102 | 3.8% |

The sparse 32-track arrangement costs **10.5×** the median of the matched one-clip arrangement even though the same source material is active. One active MIDI note among 1,024 stored notes costs **7.6×** the median of a single stored note, although its absolute cost is still small. These matched comparisons justify scheduling improvements more strongly than simply choosing the largest absolute fixture.

The 32-track effects case also measured p99 0.118 ms at 64 frames (1.333 ms budget), 0.882 ms at 512 frames (10.667 ms budget), and 0.220 ms at 96 kHz/128 frames (1.333 ms budget). These are particular workloads, not guarantees for every sample rate, effect combination, or machine.

### Build profile is a material part of the result

The default `make/flags.mk` has no optimization flag. The matched unoptimized-to-optimized median cost ratios were:

| Case | Unoptimized / optimized |
| --- | ---: |
| 32 audio tracks | 3.97× |
| 32 tracks with four effects each | 3.85× |
| Sparse 32 × 32 arrangement | 3.08× |
| 8 MIDI tracks × 32 notes | 2.55× |
| Analyzer kernel | 1.87× |

The performance wrapper is opt-in. Do not present its results as the current default app's speed. Establish explicit debug/optimized application profiles and qualify the selected app build before user-facing performance acceptance. The optimized profile passed selected existing correctness checks; it has not received full optimized GUI/device acceptance.

## 3. Live scheduling, analysis, and edit observations

Actual worker plus SDL dummy callback ran after 600 ms warmup, with at least four seconds measured per process. All **18 live sample intervals** (six configurations × three runs) reported zero missing output frames. Across configurations, individual samples recorded **0–5 over-budget worker blocks**. A processing overrun and a delivered-output gap are different events.

The output queue reached **4,096 frames**, or **85.3 ms at 48 kHz**, before device/processing delay. The worker fills the available 32-block queue. This cushions stalls but is a significant software-latency policy for interactive sound work. Dummy callbacks advanced faster than a calibrated 48 kHz device (roughly 52–53 kframes/s in these runs), so this cannot certify physical low-latency operation.

Median aggregate process CPU time over approximately four seconds:

| Live case | CPU seconds | Approximate equivalent busy fraction of one core |
| --- | ---: | ---: |
| 32 tracks with four effects each | 0.933 | 23% |
| Same plus spectrum and spectrogram | 1.707 | 43% |
| 8 MIDI tracks × 32 notes | 2.032 | 51% |
| Effects plus concurrent 30-second export | 2.396 | 60% |
| Sparse 32 × 32 arrangement with repeated gain edits | 1.974 | 49% |

This is total process CPU across threads, not whole-machine CPU utilization. Both analyzers kept publishing with zero dropped analysis windows in their enabled cases. The spectrum kernel alone took a median **5.04 ms** per 2,048-sample/256-bin window, about 12% of one core at the requested 48 kHz window cadence. Analyzer optimization has a measured benefit, but no analyzer throughput failure appeared here.

Twenty stopped public gain edits per direct-render fixture separately measured complete preparation/publication. Median edit cost was **1.69 ms** with 32 effects tracks, **4.22 ms** with 64, and **3.87 ms** with 32 at 96 kHz. These are control-thread costs, not render-block durations. Repeated live sparse-arrangement edits had a median per-run maximum of **3.22 ms**. Current render timing excludes command processing and revision adoption; this is a known observability gap, not evidence that the complete worker iteration always met its budget.

## 4. Storage and memory observations

| Path | Measured result | Consequence |
| --- | --- | --- |
| 30-second native 48 kHz float WAV import | 73 ms median | Synchronous control work is noticeable even with a warm file cache |
| Same duration, 44.1 → 48 kHz | 294 ms median | Background decode/conversion has a concrete responsiveness benefit |
| 60 seconds of synthetic stereo recording | 23,040,000 used bytes; 33,554,432 allocated bytes | Take storage grows with duration despite durable journal checkpoints |
| 60-second accelerated recording drains | 0.170 ms median; 0.469 ms p99 | Local disk kept up in this fixture; long-session memory and main-thread I/O ownership remain the concern |
| Paced synthetic recording during playback | 1.459 ms p99 drain; zero missing output frames | Useful software evidence, not input-device or recording-finish qualification |
| 120-second export | 46,080,000 output bytes; 1.19 s render; 39 ms PCM16 write | Full output remains resident until publication; content is active for first eight seconds only |
| Concurrent 30-second effects export | 1.74 s render median | Playback continued, but the synchronous caller remains occupied for that operation |

At 48 kHz stereo float32, one hour of sample data is **1.29 GiB**, before capacity rounding, source media, DSP, and temporary state. This is arithmetic extrapolation from storage format, not a one-hour measurement. Recording's doubling policy can reserve 2 GiB for that take. Streaming should preserve recovery and normalization contracts rather than merely move the same unbounded allocation to another thread.

Memory deserves a focused audit as part of edit work. The 64-track effects fixture had a median RSS after public-API construction of approximately **872 MiB**. RSS includes allocator retention/churn and DSP buffers; this is not a measured leak or an exact live-heap total. A compiled layout probe shows `EngineFxMeterBank` is **226,832 bytes**. Full LUFS history is embedded in all 16 tap slots, and even the two UI snapshot banks use that large type while publication copies only IDs and compact snapshots. Four track-bank sets alone describe about **55 MiB for 64 tracks** before spare capacity or transient revisions. Compact snapshot types and allocating only necessary history are source-supported candidates; savings and semantics still need an implementation audit.

## 5. Source findings and constraints on fixes

1. [graph.c](../../src/engine/graph.c) scans all graph sources for each track. Matching sources clear and accumulate a complete scratch block even when the sampler later finds the clip out of range. Any active-region index must preserve overlap summation, clip gains/ramps, selection boundaries, seeks/loops, and source lifetime. Do not skip an entire track just because it currently has no source: its delay/reverb/release may still be audible.
2. [instrument_osc.c](../../src/engine/instrument_osc.c) scans every stored note per sample and recomputes pitch/envelope/oscillator work for sounding notes. Active-note scheduling and precomputed invariant values can help; preserve automated parameters, gate/release behavior, note ordering, exports, and alias-reduction contracts.
3. [engine_tracks.c](../../src/engine/engine_tracks.c) rebuilds prepared sources for gain/pan updates. [engine_source_plan.c](../../src/engine/engine_source_plan.c) clones effects and sources; [graph.c](../../src/engine/graph.c) transfers gains by nested identity scans at live adoption. Reuse the established ownership/rollback boundaries. Do not reintroduce editable-state reads on the render worker to save preparation time.
4. [engine_meter.c](../../src/engine/engine_meter.c) already copies compact fields, but [engine_internal.h](../../include/engine/engine_internal.h) allocates full history-bearing banks for snapshots. Separate published observations from DSP history without weakening metering identity or nonblocking publication.
5. [engine_core.c](../../src/engine/engine_core.c) processes commands before the timed render section and fills the output queue until fewer than one block is free. Measure complete worker-cycle/adoption cost and queue dwell before changing queue targets. A shorter queue trades responsiveness against scheduling margin.
6. [audio_recording.c](../../src/app/audio_recording.c) drains and synchronizes the recovery journal from the app loop while maintaining a growing in-memory take. [engine_io.c](../../src/engine/engine_io.c) renders to a full-size buffer. [media_clip.c](../../src/audio/media_clip.c) and [resample.c](../../src/audio/resample.c) decode/convert full files synchronously. Their durability, pinning, gap accounting, finite-output, and failure semantics are prerequisites for streaming/background work.
7. [analysis_math.h](../../include/engine/analysis_math.h) recalculates Hann weights and trigonometry per bin/sample. Precomputation or a carefully qualified transform can reduce worker CPU. Preserve the calibrated logarithmic tonal-amplitude meaning; an arbitrary FFT display is not a behavior-preserving substitute.

## 6. Proposed focused slices

The initial audit is **S4.1**. Each proposed slice still begins with a focused contract audit, then implementation and evidence; these are not authorization to execute the whole list now.

| Slice | Proposed scope and useful subsets | Acceptance that matters |
| --- | --- | --- |
| **S4.2 — Active audio-region scheduling** | Track-local source lookup; block interval activation; boundary/seek/loop updates; preserve effects tails | Same rendered samples/overlap/fades/export semantics; substantially reduce sparse-vs-active-only overhead; no render heap growth |
| **S4.3 — Active MIDI work** | Note-on/off/release scheduling; cached pitch constants; automation-aware parameter work | S3.6 lifecycle and sound contracts; sparse-note cost closer to active-note count; real polyphony comparison |
| **S4.4 — Bounded edit preparation and meter memory** | Compact meter snapshots; separate history storage; selective scalar updates; bounded identity/adoption work | Rollback, source pinning, history/ramp continuity, and nonblocking meter delivery; lower edit latency/RSS with measured live allocations |
| **S4.5 — Queue and complete deadline policy** | Whole worker-cycle/adoption timing; queue dwell/low-water observations; explicit playback/audition target occupancy | Reduced software queue latency with measured margin; underrun recovery; physical-device tests before a low-latency claim |
| **S4.6 — Duration-independent disk pipelines** | **6a recording:** journal/drain worker and bounded preview; **6b export:** chunked durable writer, bounded render buffers, cancellation/progress; independent sub-slice closeouts | Torn-write recovery, gap placement, transactional insertion, export snapshot isolation; explicit two-pass normalization strategy; memory bounded independently of duration |
| **S4.7 — Background media preparation/cache** | Decode/resample jobs; cancellation/stale-result rejection; pin/eviction policy; bounded handoff to project edits | UI responsiveness without partial publication or invalid media lifetimes; warm/cold distinct-asset workloads |
| **S4.8 — Analyzer compute efficiency** | Reuse window/kernel work; qualified transform alternative if warranted; consumer backlog reporting | Existing tone/dB/log-frequency/epoch/gap tests; lower measured CPU; no render-thread transform work |
| **S4.9 — Integrated sustained acceptance** | Long playback/recording, repeated edits/export/import, memory plateau, recovery/storage pressure, physical device and GUI frame budgets | Source-bound optimized app build, stable long-session metrics, recoverability, listening/device acceptance separately recorded |

**Recommended next implementation: S4.2.** It targets a large measured cost caused by inactive content, with a clear behavior-preserving contract. Keep the optimized measurement profile explicit; qualify an optimized application profile before advertising app-level gains. S4.3 and S4.4 follow naturally. If interactive latency is the immediate product priority, S4.5 can move earlier after its queue-policy audit. Streaming remains important for long work but was not a short-fixture throughput failure. Avoid a broad parallel-render rewrite before these lower-complexity costs are addressed.

## 7. What this audit does not establish

No full session load/save performance, unique-asset cache pressure, cold storage, MP3 decoding, GUI frame timing, arbitrary FX chains, convolution/reverb extremes, overnight leaks, real capture callback stress, power-cut recovery, or physical ADC/DAC timing was measured. Playback/live samples are short and queue-buffered. No new sanitizer suite or full stable/legacy rerun was required for this measurement-only change; selected existing optimized correctness checks and final default `make` passed. Historical S3 receipts remain their own snapshots.

Stop here for review of the proposed slices. No commit, release, default-profile change, or S4 engine optimization was performed.
