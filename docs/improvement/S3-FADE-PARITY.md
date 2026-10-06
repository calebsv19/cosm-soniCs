# S3.1 — Existing audio fade controls agree with playback

## 1. Bounded contract

Refine the four existing audio-clip fade choices: linear, S-curve, logarithmic, and exponential. No new curve mode, crossfade system, MIDI envelope behavior, file format, or interface control is introduced.

1. The existing UI formulas now live in the UI-independent `engine/fade_curve.h` helper. The UI wrapper delegates to it. The formulas remain `t`, `t*t*(3-2*t)`, `pow(t,0.35)`, and `pow(t,2.2)` for progress clamped to [0,1]. Enum values and serialized fields remain unchanged.
2. Fade-in gain is the selected function of elapsed fade progress. Fade-out gain is one minus that function of elapsed fade-out progress, matching the existing editor's overlay direction. Linear fade-out retains the original remaining-frames division exactly.
3. Audio sampler state, independent render clones, and copied segments retain curve choices. Gain is evaluated once per frame and applied to every output channel. Selecting a shape publishes through the established scalar transaction; rejection restores metadata and sampler settings. The inspector already checks the setter result before recording history.
4. Existing time conventions remain: clips are half-open intervals, fade-in begins at zero, and the final included fade-out sample precedes the zero boundary at clip end. Zero duration disables a fade. Existing engine edits clamp fade lengths to the clip and avoid overlap; low-level samplers retain their existing multiplicative behavior if supplied overlapping spans. This does not establish new crossfade semantics or preserve an original long fade's phase across splitting.
5. Playback and current offline bounce use the same sampler. This closes fade processing parity only; bounce normalization, effect reset/tails, and general export isolation remain later S3 work.

Existing projects with nonlinear audio fade selections will now sound like their displayed curves instead of the previous unintended linear playback. Linear projects retain their prior gain law. MIDI fade support is not added. Compound clipboard/whole-track/history transaction and rejection-feedback deferrals remain in the S1 ledger.

## 2. Acceptance

`make test-fade-processing` compares actual constant-source samples against independently written expected formulas for all four curves. It covers full-block and partitioned cloned-sampler rendering, overlapping low-level spans, offline bounce, UI evaluation, undo/redo sample output, JSON write/read curve identity, source-offset trim, segment copies, zero fades, and one-frame/overflow-bounded lengths.

`test-engine-parameter-transaction` now rejects curve publication and checks restored curve identity plus unchanged sampler output. `test-engine-source-lifetime` cycles curve selections during its existing 2,000 live source-edit iterations. Full stable/legacy and sanitizer outcomes are recorded in [the receipt](evidence/s3-fade-parity.json).

These are software sample/state tests. No manual listening or screenshot acceptance is claimed; the UI formulas/layout are preserved. No claim of full DSP, instrument, or export correctness is implied.

## 3. Reuse and next boundary

Shared governance preflight and reuse scan covered `core_math` for numeric helpers and `kit_viz` for presentation; runtime/time, storage, and scene modules do not own this clip envelope contract. Decision: **reuse-deferred** for a generic shared fade API, because the formulas and enum are existing DAW-specific editing semantics. Consolidate them in the existing DAW domain header and keep the UI as a caller. No shared module, version, or adoption metadata changes.

Stop after S3.1. The next proposed slice is S3.2 analysis calibration and signal-source identification; it is not part of this implementation.
