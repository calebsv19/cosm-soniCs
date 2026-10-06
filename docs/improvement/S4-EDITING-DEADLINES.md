# S4.4b and S4.5 — Edit preparation, worker deadlines, and buffering

## 1. Scope and ownership

This slice completes the remaining S4.4 mixer-edit preparation and source-history lookup work, then establishes complete busy-worker timing and explicit output queue policy. S4.4a's compact published meters remain in place. It does not introduce new instruments, effects, parallel rendering, or streaming storage.

Reuse review: existing `core_time` supplies monotonic timestamps and duration arithmetic (`reuse-adopted`). Queue/scheduler/jobs/workers/wake/kernel, data/pack/memory, scene/space/trace/math, and UI libraries were checked; accepted mixer snapshots, audio callback headroom, DSP histories, and project queue policy remain DAW-owned (`reuse-deferred`). Existing atomics and prepared ownership are reused. No shared module, version minimum, or adoption change.

## 2. S4.4b implementation contract

1. Gain, pan, mute, and solo capture all current scalar mixer targets into a compact payload. A payload owns no source graph, media pins, instrument, effects, EQ, or meter histories. On this arm64 build its storage is 56 bytes plus 32 bytes per track (120 bytes for two tracks), in two control-side allocations.
2. Scalar and authored structural revisions use the same pending atomic slot. Control removes a pending revision before amending it. If it is structural, the scalar targets are applied to that exclusively owned preparation and the complete structural revision is republished. If it is scalar, the newer complete scalar snapshot replaces it. A worker that already took the older revision completes that adoption before taking the newer update.
3. Allocation failure occurs before taking the pending slot. The setter rolls back its editable scalar; the previous accepted publication remains intact. Before applying any scalar values, the complete track count and stable identity map are checked. Worker adoption retargets the existing source and mixer ramps and retires the payload for control-side destruction.
4. Source graph entries retain authored clip gain separately from effective track gain, allowing recovery from zero/mute without division. Existing source and stopped-audition solo policies are captured separately. Live updates preserve the existing five-millisecond sample ramps; stopped updates snap. Instrument/combined track settings and structural edits continue to use full preparation. Borrowed external graph requests disable the lightweight path until an authored full preparation reestablishes its source metadata.
5. Full graph preparation builds a sorted `(track, source identity, insertion index)` lookup. Worker gain-history adoption uses binary search and preserves first-compatible duplicate ordering. Unknown/unprepared graphs retain a track-local fallback. Render summation order does not change. Structural track identity matching and effect-history transfer retain their existing costs; the new lookup specifically removes the global source-by-source nested scan.

Exact comparison against complete revisions covers coalesced mixer changes, structural edits before and after scalar changes, gain/pan/mute/solo transitions, EQ history continuity, preparation failure, and deferred retirement. Existing concurrency tests exercise actual worker/control overlap. Export failure injection now covers 56 preparation boundaries.

Matched optimized scheduling measurements passed all 33 samples. For sparse 32×32 live editing, the median of three per-process **maximum** edit times changed from 4.913917 ms in S4.3 to 0.056416 ms here. This is not a p50 of individual edit operations. Render medians remained comparable: sparse audio 0.058583 ms; MIDI 8×32 0.480207 ms. All short live sample intervals reported zero missing frames. See [S4.4b evidence](evidence/s4-mixer-publication.json).

## 3. S4.5 timing contract

The worker measures each busy iteration from before command processing/source adoption through audition retirement, rendering, analysis handoff, notification, sanitization, and queue submission. Intentional idle/full-queue sleeps are excluded. Service-only iterations are counted too, so expensive adoption while the queue is already full remains visible.

`EngineDiagnostics` now exposes `worker_cycles`, `worker_render_cycles`, `worker_over_budget`, `worker_max_ns`, and `service_max_ns`. Every busy iteration is compared against one nominal DSP block duration. The existing `render_*` counters retain their narrower scope. These overlapping counters must not be added together. `service_max_ns` covers commands/adoption and audition retirement before rendering. Counters are independent lifetime atomics, not a globally coherent snapshot. They measure software execution/wall delay within the iteration, not wake lateness, physical presentation latency, or an operating-system deadline guarantee.

## 4. S4.5 queue policy

`output_queue_blocks` accepts integer values 2..32 in `config/engine.cfg` and project engine settings. The default remains **32**. Older project files default to 32; invalid file values are rejected, and zero/unset C configurations normalize to 32. No live resizing API is introduced; policy is installed during engine startup.

The effective target is the greater of the requested DSP-block count and two device callback buffers, rounded up to a complete DSP block. The worker renders only when one more complete block fits under this target. Queue storage retains at least the compatibility capacity; reducing the target reduces render-ahead depth rather than reallocating during playback. `queue_target_frames` reports the effective target; `queue_high_water_frames` remains a lifetime observation.

For 48 kHz, 128-frame DSP/callback blocks, nominal software queue targets are 10.67 ms (4 blocks), 21.33 ms (8 blocks), and 85.33 ms (32 blocks), before DSP and device delay. Larger device callbacks can raise the effective minimum. Four and eight blocks are explicit opt-in profiles; physical-device qualification is required before changing the default policy.

Acceptance covers exact budget thresholds, service-only accounting, queue rounding/defaults, actual bounded queue filling, project/config round trips, and 300 live transport sequences across 4/8/32-block profiles. The workload matrix additionally includes MIDI, analysis, export during playback, scalar editing, mixed scalar/structural editing, and recording during playback. Measurements and terminal checks are recorded in [S4.5 evidence](evidence/s4-worker-queue.json).

## 5. Acceptance results

Both slices are complete at the documented software boundary. Stable and legacy regression, focused normal/AddressSanitizer/ThreadSanitizer checks, optimized correctness checks, and the final default application build passed. Coverage includes 300 transport sequences across queue profiles, 300 concurrent mixer-edit cycles, 2,000 live source-edit cycles, exact scalar/full-revision output parity, and 56 export allocation-failure boundaries.

The optimized queue matrix passed **54 samples**: six workloads × three repeats × three profiles. Every sample enforced its effective queue target and reported zero missing output frames during its measured warm interval.

| Requested blocks | Effective target | Nominal queue duration at 48 kHz | Missing frames across 18 samples | Complete-worker overrun count |
| --- | ---: | ---: | ---: | ---: |
| 32 | 4,096 frames | 85.33 ms | 0 | 9 |
| 8 | 1,024 frames | 21.33 ms | 0 | 7 |
| 4 | 512 frames | 10.67 ms | 0 | 5 |

Render-only overrun totals happened to equal complete-worker totals in this matrix; the independent service/adoption measurements remain necessary because that need not hold in other workloads. The overrun counts do not establish that smaller queues improve execution speed: profiles ran sequentially on a nonexclusive host. All three retained gap-free delivery in these short fixtures. Each ordinary live interval lasts roughly four seconds; recording lasts ten seconds, with 600 ms warmup excluded. Startup gaps, physical devices, calibrated sample pacing, long sessions, and GUI load are not qualified by this matrix. Retain 32 as the compatibility default; 8/4 are explicit lower-buffering profiles.

## 6. Build-system finding

The first combined stable run failed in export fixture creation after the engine/config layout changed. Instrumented test objects emitted dependency files but the make rules did not include them, allowing stale header layouts to be linked against current normal objects. Including those transitive dependencies rebuilt the stale objects; the targeted export test then passed all 56 failure boundaries. The original failed log and the corrected follow-up are retained as separate evidence. No test assertion was weakened.

## 7. Remaining scope

S4.6 recording and export streaming is next, with independent bounded-memory, journal, cancellation, normalization, and publication contracts. S4.7 media/cache, S4.8 analysis compute, and S4.9 sustained integrated and physical-device acceptance remain separate. Default optimization policy and GUI performance are unchanged. No commit or release is included.
