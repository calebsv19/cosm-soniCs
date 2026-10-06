# S3.3 — Existing dynamics and processing delay

## 1. Implemented contract

S3.3 repairs the existing limiter, compressor knee, and reduction measurement, and connects their timing to the current serial-track/master mixer. The original findings and probe are retained in [the entry audit](S3-DYNAMICS-AUDIT.md).

1. **Limiter delay and detector:** the supported 0–3 ms lookahead rounds to integer project-rate samples, with a true zero setting. Audio is delayed by exactly that count. A prepared monotonic peak queue covers the delayed sample through the newest input sample, linking channels by their maximum absolute amplitude. Immediate gain reduction and exponential release enforce the configured sample-peak ceiling. This is not true-peak limiting or oversampling.
2. **Render storage and transitions:** the factory allocates audio and detector capacity for the full supported delay. Processing, reset, and parameter application do not allocate. Lookahead is a discrete timing change at a block boundary, not a smoothed sequence of delay lengths. Changing its rounded sample count clears limiter history and starts with silent fill. Repeating the same count preserves history. Ceiling/release retain their existing manager smoothing.
3. **Latency contract:** built-in `FX_API_VERSION` is now 2, adding optional render-owned active/max-latency and applied-reduction queries. The limiter advertises dynamic latency; its zero static descriptor field is not an authoritative delay. Built-ins are rebuilt together; this is not a binary plugin compatibility promise. Existing static-latency descriptors remain supported by the manager.
4. **Current mixer alignment:** a prepared render revision allocates per-track compensation capacity for the maximum supported delay in its current serial chains. Each block applies controls once, sums each enabled chain's signal delay, and delays shorter tracks before the master sum. Master processing adds a common delay. This covers the existing track-to-master path and stopped MIDI audition path, not a new send/return/sidechain routing graph. Effect meter inserts remain at their existing pre-compensation taps.
5. **History ownership:** compatible revisions transfer DSP and compensation history together, using the existing stable track mapping. A changed latency, latency-bearing topology, or compensation capacity resets effect/compensation histories at the block boundary to avoid mixing old timing with new timing. This policy can produce a short dropout and clears effect tails when timing changes; it does not claim seamless delay modulation. Explicit transport resets clear histories; ordinary loop wraps retain them.
6. **Clock and bounce meaning:** diagnostics expose the latest render block's common DSP delay separately from queue delay. Callback/project clocks keep their existing delivery semantics; they are not retroactively relabeled as hardware-audible positions. Exact-range offline bounce resets effect/EQ history, renders the reported extra delay, and removes that leading delay so the requested half-open range retains its final samples. Existing normalization remains; general tail export and isolated export architecture are separate slices.
7. **Compressor curve:** both existing compressor variants share a continuous soft-knee gain curve whose value and slope meet the hard-region lines at both boundaries. Ratio 1 gives exactly zero reduction. The ordinary compressor retains independent channel peak/RMS envelopes; the sidechain-capable variant retains its linked detector, left-input fallback, and explicit-key API. No external sidechain routing is added.
8. **Reduction measurement:** limiter and both compressors report the most negative applied gain in the last process block, before makeup gain. Their UI labels say `GR peak`; bypassed instances emit zero reduction. Other processors retain their existing input/output RMS-ratio values, now labeled `RMS delta` with a bipolar history scale. These values are not presented as detector reduction.

## 2. Acceptance evidence

The [source-bound receipt](evidence/s3-dynamics.json) records commands, log fingerprints, selected output, and proof limits.

| Behavior | Automated evidence |
| --- | --- |
| Exact 0–3 ms delay | Production impulse tests at 44.1/48/96 kHz with 1/2/6 channels and split windows; standalone before/after probe |
| Ceiling and lookahead | Over-ceiling transient, anticipatory reduction before delayed arrival, linked stereo ratios, bounded output |
| Release and continuity | Envelope recovery, reset, finite-control rejection, changed-delay silent fill, partition-identical samples |
| Compressor transfer | Hard/soft knee, both knee boundaries, peak/RMS detectors, ratio 1, makeup exclusion, independent/linked channel policies |
| Applied GR | Production callback telemetry with +12 dB makeup still reporting -13.5 dB reduction |
| Parallel timing | Differing serial track delays cancel after alignment; positive impulse survives mid-delay revision transfer |
| Engine integration | Real clip mixer at 192 samples common delay, repeated exact-range bounce preserving first/last impulses, live delay/bypass edits |
| Failure safety | Every instrumented dynamics/manager revision allocation boundary fails safely; active revision remains usable |
| Render heap discipline | Instrumented production limiter/compressor/sidechain-compressor/manager malloc/calloc/realloc/free calls remain zero inside guarded processing, setters, resets/transfers, and mixing |
| Regression and concurrency | Stable/legacy suite, focused ASan/TSan, final application build |

These are software sample/state and concurrency proofs. Manual listening, visual acceptance, and physical output-latency calibration are not claimed. The API reports the latest worker configuration, not per-buffer historical hardware latency during a live transition.

## 3. Shared reuse and placement

`core_math` provides generic math primitives, `core_queue` provides concurrent queue ownership, and `core_time` provides monotonic timing. None owns a limiter detector, serial effect latency, or DAW track alignment. Reuse is **deferred** for these new domain-specific helpers: they remain in the existing DAW effects/engine layer. Existing prepared revision ownership and shared runtime/queue integration are retained. No shared core/kit module, minimum version, or adoption contract changes. The version change is confined to the DAW's built-in effect API.

## 4. Remaining boundaries

S3.3 is complete at this bounded software contract; the receipt records the final checks. Further S3 work includes broader parameter ramps/discontinuities, supported media/resampling, instrument release/aliasing, and general export reset/tail/isolation behavior. This slice does not certify every other dynamics effect, introduce true-peak processing, enable external sidechain routing, or claim click-free lookahead edits.
