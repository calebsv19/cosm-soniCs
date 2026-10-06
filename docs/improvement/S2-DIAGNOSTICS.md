# S2.6 — Bounded runtime diagnostics

## 1. Measurements and meaning

`engine_get_diagnostics` exposes engine-lifetime atomic totals and bounded occupancy observations:

| Measurement | Meaning |
| --- | --- |
| Callback count | All valid output callback invocations. |
| Underrun callbacks / frames | Requested frames unavailable while the software timeline is advancing; startup shortages count. Stopped/paused silence and idle audition are excluded. This is not a hardware xrun counter. |
| Render blocks / over-budget blocks / maximum duration | Whole worker-block processing duration, including loop chunks and publication, compared with `frames / sample_rate`. Command processing and waiting before the block are outside this duration. A slow block need not cause an output gap if queued audio covers it. OS scheduling lateness is not independently measured. |
| FIFO command last / maximum age | Monotonic time from successful FIFO admission to dispatch. Coalesced safety fallback is counted separately and has no FIFO age. This is not command completion latency. |
| Output queue / observed high-water frames | Prepared-but-undelivered software audio, and the largest producer-observed occupancy. Concurrent callback consumption can hide a transient peak. A failed bounded clock read reports current occupancy as zero. |
| Pending commands / accepted / rejected / applied / safety fallback | Approximate bounded occupancy plus the existing command-admission counters. An incoherent occupancy observation reports zero rather than unsigned wraparound. |

Counters are independent observations, not one global transaction. They survive pause, seek, stop, and device restarts within the engine, and reset when an engine is created. Call snapshot/format APIs while the engine and its queues are alive; the UI/control owner serializes lifecycle changes.

## 2. Overhead and presentation

1. The output callback adds one atomic callback increment and, only on a playing shortage, two atomic additions. No formatting, allocation, file IO, logging, retries, or mutexes were added to the callback.
2. Each worker block adds two monotonic reads and a fixed number of atomic loads/stores/additions. Each queued command adds an admission timestamp and one dispatch timestamp. No samples or unbounded event history are retained for telemetry.
3. The existing timeline status banner shows lifetime audio failure totals after an output gap, slow block, rejected command, or safety fallback. Enabling timing logs also exposes the healthy status. Recording status takes precedence. Text is clipped to the existing banner rather than changing layout; full details remain available through the snapshot and timing log.
4. Existing optional worker timing reports include the full compact diagnostic text, at most once per second while rendering. Counters themselves are always enabled. This bounds instrumentation work structurally; no universal wall-clock overhead guarantee or hardware deadline certification is claimed.
5. Recording's existing banner shows checkpointed duration, dropped capture frames, and the latest software alignment difference in frames. Transport discontinuity and checkpoint/publication failures retain explicit action messages. Alignment is an observation, not a correction or hardware latency estimate.

## 3. Acceptance and boundaries

`test-engine-transport-clock` exercises exact missing-frame counts, paused-silence exclusion, counters across transport epochs, render-budget equality versus overrun, FIFO age with a deterministic monotonic clock, formatting, and concurrent occupancy reads during real worker transitions. Command-delivery overload tests continue to cover reserved capacity, rejection, and safety fallback. Recording tests verify checkpoint/gap status inputs and durable recovery after a rejected nonfinite packet.

Full stable/legacy tests, focused ASan/TSan runs, and final application build are recorded in [the evidence receipt](evidence/s2-recording-diagnostics.json). SDL dummy endpoints and deterministic callback tests establish software behavior. Physical loopback latency, hardware xrun observation, scheduler tracing, manual visual acceptance, recovery-browser UI, and long-take background streaming remain separate acceptance/work items.

## 4. Next phase

The software implementation slices S2.1–S2.6 now have defined contracts and test coverage. Existing S2.2 device-activation and user-facing restore/recovery feedback limits remain; S2.5 does not close physical calibration or S4 streaming. S3 can begin as a separately authorized phase covering truthful DSP/analysis: fade consistency, lookahead/dynamics latency, smoothing, contiguous/calibrated analysis and tap identity, resampling, release/aliasing, and deterministic export/reset/tail behavior. No S3 implementation is included here.
