# S3.4 — Existing controls and audio transitions

## 1. Implemented behavior

1. The existing Gain effect ramps linear amplitude over 20 ms, counted in samples. Repeated targets do not restart a ramp; retargeting begins at the current amplitude. Initial configuration and explicit reset snap to the accepted value. Channels share one gain per frame. Its finite -96..24 dB control retains the existing lower gain limit; explicit mixer zero is exact silence.
2. Live mixer gain, clip gain, mute/solo gating, and stereo balance changes ramp over 5 ms. Prepared source revisions transfer current ramp state by stable clip identity and surviving track identity. Stopped edits use their authored target immediately. Stopped audition has separately prepared gain ramps and the same balance behavior. Muted/solo-excluded sources stay prepared so they can transition; settled zero-gain sources skip source rendering. This increases preparation/storage cost for muted material; scalability remains S4.
3. Existing zero-latency effects crossfade dry/wet bypass over 5 ms using prepared scratch storage. Re-enabling a fully bypassed processor clears its stale history before fading in. A reversal during a transition starts from the current blend. The Gain flag `FX_FLAG_SAMPLE_PARAM_SMOOTHING` prevents the manager from adding a second block-level smoothing stage to this sample ramp. Other existing parameter smoothers are unchanged.
4. Latency-bearing effects retain S3.3's explicit block-boundary timing/history reset. They do not blend time-misaligned dry and wet samples. Bypass does not preserve unlimited effect tails; track mute/solo still gates source input before the existing effects, so existing wet tails may continue.
5. Stop, pause, seek, loop-configuration changes, and explicit idle-audition reset retain the clock/queue reset contract. They snap controls and clear relevant DSP histories. Ordinary timeline loop wraps reset source positions while preserving mixer ramps and effect histories. These are deliberate discontinuity policies, not a guarantee of seamless seeking, topology edits, or delay modulation.
6. During bypass blending, processor reduction telemetry describes the processed branch; it is not a measurement of the final blended signal. Fully bypassed dynamics report zero reduction. Meter-only inserts retain their existing enable behavior.

## 2. Acceptance

`test-control-transitions` verifies 44.1/48/96 kHz Gain ramps, 17-versus-1024-frame partitions, retarget/reset/rejection, stereo linking, exact zero, manager revision handoff, bypass in both directions, real mixer gain/pan/mute/solo, and ordinary source-reset versus explicit control-reset behavior. Existing dynamics, parameter-transaction, and live mixer ownership suites cover the surrounding render path and rejection/ownership contracts. The new target is part of `test-stable`.

Final commands, results, source fingerprints, and proof limits are recorded in [the S3.4 receipt](evidence/s3-control-transitions.json). Software tests do not substitute for listening or physical output-device acceptance.

## 3. Placement and remaining boundaries

`effects/sample_ramp.h` owns this small DAW sample-domain helper. Existing shared queue/runtime ownership is retained; no shared library or adoption version changes. This slice does not introduce a general FX automation system, redesign pan law, smooth every effect parameter, or promise click-free structural edits. S3.7 export architecture remains separate.
