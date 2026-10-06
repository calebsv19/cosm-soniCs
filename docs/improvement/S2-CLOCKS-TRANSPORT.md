# S2.3 / S2.4 — Clocks and transport

## 1. Scope and acceptance boundary

Implement distinguishable render-ahead, callback-delivered, and estimated presentation positions, then define transport discontinuities against those positions. This is a bounded software-clock and transport slice. Physical DAC timing, latency compensation, recording alignment, and performance qualification remain separate.

## 2. Clock contract

1. `engine_get_transport_frame` retains its explicit render-cursor meaning for engine scheduling and the existing recording integration. It is not the user-visible playhead.
2. `EngineClockSnapshot` reports an epoch, rendered and consumed stream-frame counts, their mapped project positions, remaining queued frames, and requested/applied transport serials. Sequence checks reject a snapshot intersecting producer publication, callback consumption, or a discontinuity. Reads are bounded and may return false without changing the caller's output.
3. Consumed means copied into an SDL callback buffer. Silence inserted for an underrun does not advance the project timeline. While paused, audition output may consume stream frames but does not advance the project position.
4. Presentation is a software estimate: interpolate from the current callback's starting project position using `core_time`, never farther than the real frames delivered in that callback. Before the first callback, hold the epoch origin. After an underrun, hold the last delivered frame. Map positions through the same half-open loop arithmetic, including the intro before a loop and multiple wraps in one callback.
5. `hardware_position_known` is false. SDL does not supply a DAC timestamp or downstream hardware latency through this adapter. The callback-delivered end is not proof that the speaker has played those samples. The estimate may differ by endpoint buffering/latency and is not a recording compensation signal.
6. Timeline, transport, inspector, MIDI editor, meter-history display, and editing-at-playhead callers now use `engine_get_presentation_frame`. Its bounded-read fallback is the last callback-delivered cursor. Recording retains its prior explicit render position until S2.5; session/export transport observations are likewise not a hardware timestamp.

## 3. Transport behavior

| Operation | Position and state | Buffered audio / histories |
| --- | --- | --- |
| Play | Resume the retained cursor; repeated play is idempotent. | Discard paused audition backlog on transition to playback. |
| Pause / Space / recording Play toggle | Stop advancement at the callback-delivered cursor, retaining it for resume. | Discard render-ahead audio; clear source, EQ, FX histories and active audition voices. |
| Stop button | Stop and return to project frame zero. | Discard render-ahead audio; clear source, EQ, FX histories and audition voices. |
| Seek | Preserve play/pause mode; establish the requested position. If playing with a loop enabled, positions at/after loop end map into that loop. | Discard old queued audio and clear processor histories. |
| Set/disable loop | Preserve mode and the old callback-delivered position, then map it into the new enabled interval when playing. An enabled empty/reversed interval is rejected. | Discard audio prepared under old loop settings and clear histories. |
| Ordinary loop wrap | Repeat `[start, end)` sample-accurately, even for a loop shorter than a render block; material before loop start plays as an intro. | Reset timeline source traversal at wrap; retain FX tails across ordinary repeats. |
| Engine shutdown | Stop endpoint/workers, reset clocks and pending serials. | Reclaim buffers only after endpoint/worker quiescence. Startup preserves an offline-selected render cursor. |

All accepted playback, seek, and loop commands have transport serials. Application occurs in FIFO order; the applied serial is published after the operation's reset work. Existing playback-intent snapshots remain available separately. Stop/pause retain the overload safety lane; coalesced safety application acknowledges the latest accepted serial, and older transport commands cannot revive playback afterward. Rejected requests do not advance accepted serials. Loop UI fields update only on acceptance; rejected seeks do not clear presentation histories or submit redundant play requests.

## 4. Discontinuity implementation and limits

The worker is the sole producer; SDL is the sole output consumer. A transport discontinuity briefly locks the SDL endpoint, waits for an in-progress callback to finish, resets queue indices, and publishes a new clock origin/epoch. No allocation, media work, or DSP reset runs inside that exclusion window. Source/EQ/FX history reset follows after unlocking and before command acknowledgement or further rendering. Offline operations use the same path without an open endpoint.

This intentionally trades a short endpoint exclusion window for a precise software flush boundary. It is not a lock-free discontinuity protocol, a hard-real-time deadline proof, or a guarantee that downstream hardware buffers can be retracted. Large effect-history resets can delay subsequent rendering; measuring and reducing those costs belongs to sustained-performance/diagnostics work. Abrupt reset fades and processing-quality behavior remain S3 work. Analyzer calibration and full analysis-history/epoch propagation are not claimed here.

## 5. Shared reuse decision

- **Reuse-adopted:** existing `core_time` monotonic nanoseconds for callback anchoring and bounded presentation interpolation.
- **Reuse-deferred:** `core_queue` has a non-threadsafe pointer ring and a mutex/condition queue, not this audio SPSC stream/flush contract. Retain the existing app-owned audio ring. `core_sched`, `core_jobs`, `core_workers`, `core_wake`, and `core_kernel` do not own DAW timeline/loop/sample-delivery semantics. Existing render/worker scheduling remains unchanged.
- `core_data`/`core_pack` storage, scene/space/math contracts, and UI kits do not supply this endpoint clock mapping. Keep DAW transport policy and SDL exclusion in the application; add no shared module, shared API change, version bump, or adoption-matrix change.

## 6. Acceptance

`tests/engine_transport_clock_test.c` exercises the real engine clock, worker, mixer, command processor, SDL adapter, and a deterministic monotonic-time provider used only before worker startup.

1. Render-ahead versus delivered versus displayed positions; partial callbacks; interpolation bounded by delivered samples; silent underruns and empty callbacks that do not advance the project.
2. Pause with unconsumed audio, paused audition that leaves timeline position fixed, resume without skipped source samples, stopped and playing seeks, stop-to-zero, invalid loop rejection, and overflow-safe timeline arithmetic.
3. Actual audio samples through a three-frame loop inside a 64-frame render block, intro/wrap correspondence with the callback clock, and removal of stale samples after pause, seek, loop disable, and stop.
4. A nonzero delay-tail baseline followed by seek proves old processor history is cleared.
5. One hundred live seek/play/pause sequences with serial acknowledgement; shutdown clears clock counts. Existing command delivery tests cover queue pressure, reserved capacity, emergency stop, and playback intent/application separation.

See [evidence/s2-clocks-transport.json](evidence/s2-clocks-transport.json) for final command outcomes, retained output, full-log hashes, and source fingerprints. SDL dummy-device and deterministic software tests do not establish physical timing or visual/listening acceptance.

## 7. Stop boundary and remaining work

S2.3/S2.4 are implemented at the software boundary above. The next bounded slice is **S2.5 recording alignment and media durability**: timestamp captured frames, represent discontinuities, reconcile capture/output clocks, propagate WAV write/close errors, and establish recoverable-take/media-publication ordering. Physical loopback validation is separate. **S2.6 diagnostics** still needs deadline/underrun/command-age reporting and performance budgets; clock queue occupancy is available now but does not close that slice.

S3 remains truthful DSP/analysis: fade consistency, lookahead/dynamics latency, smoothing, contiguous/calibrated analysis and tap identity, resampling, release/aliasing, and deterministic export/reset/tail behavior. This slice establishes timing inputs without claiming those processing contracts are complete. Existing S1 deferrals and S2 device-activation/recovery-feedback limits remain in their respective closeout reports. Stop and report before starting S2.5.

## 8. Verification result

| Check | Result |
| --- | --- |
| Full `make test-stable test-legacy` | Passed. |
| AddressSanitizer: clocks, commands, recording, project transactions | Passed. |
| ThreadSanitizer: clocks and command delivery | Passed. |
| Final recording Play/REC caller adjustment: clock and recording tests | Passed; manual UI acceptance remains separate. |
| Final `make` | Passed. |
| `git diff --check`, receipt fingerprints, document links | Passed. |

The final clock/command runs include a deterministic redundant-overload-notification check: after a safety request has already been consumed, a repeated notification cannot turn pause into stop-to-zero. The recording Play/REC button now uses the same position-retaining pause operation as Space. All completed changes remain local and uncommitted.

Subsequent status: S2.5 and S2.6 are implemented at the software boundary documented in [recording/durability](S2-RECORDING-DURABILITY.md) and [diagnostics](S2-DIAGNOSTICS.md). Earlier future-work statements above describe this document's original handoff boundary.
