# Qualified compressor/limiter controls and live gain reduction

The native effects rack/list exposes Compressor (type 20) threshold, ratio, attack, release, makeup, knee and Peak/RMS detector controls. Limiter (type 21) exposes ceiling, lookahead and release. Use the existing insert controls on a child, group, return or master. Native drags commit one shared undo step; normal project save/reopen retains the authored settings. Existing parameter-mode/automation support remains processor-specific.

Headers display positive **GR** attenuation in dB, **hold** when stopped or stale, **GR --** before observation, and **GR bypass** when disabled. Header text has per-slot lifetime and occupies a separate region from the effect name. The optional preview can independently read the same qualified value; it no longer drains dynamics feedback from the history stream.

## MCP workflow

1. Discover a current target with get_routing. Omit target for master in get_effect_chain. The source adapter still discovers 70 tools.
2. Read authored effects and expected_state. The separate dynamics_feedback array identifies each compressor/limiter by effect_id/type_id, with available/enabled/fresh/playing, signed gain_db, positive reduction_db, observation age and exact sequence.
3. Use edit_effect_chain with the returned expected_state, current instance and immutable request_id to add, parameter-edit or enable/bypass the insert. Use undo_effect_chain/redo_effect_chain for matching shared history. Parameters use catalog indices and physical units. Repeated reads do not consume feedback or invalidate expected_state; observations are never serialized into musical settings or immutable write receipts.

Fresh means observed within one second. Previously measured values become stale rather than decaying into fabricated current attenuation. Removed, shifted or wrong-scope effect identities are rejected; project replacement starts fresh observations. Disabled effects report no available qualified attenuation. Private bounce/capture/export rendering never updates the live feedback.

## DSP meaning and qualification

The Compressor is feed-forward with **independent channel detectors**. Peak detection smooths rectified amplitude; RMS detection smooths squared amplitude and then takes its square root, with attack/release branch selection. This is an asymmetric envelope detector, not a fixed RMS window. Compression uses a continuous quadratic soft knee or hard knee. Ratio 1 is transparent without makeup; makeup is applied after compression. Transients can exceed the threshold while the detector attacks; compression is not a ceiling guarantee.

The Limiter links channel peaks, uses integer-sample 0–3 ms lookahead and prepared sliding-maximum/delay storage, instantaneous gain reduction and one-pole release toward the required gain. A lookahead change resets timing history; unchanged lookahead preserves it. Host compensation aligns source/group/return/master paths. Its ceiling qualifies **sample peaks at the active DSP ceiling**, not intersample true peaks or physical loudness. Host control smoothing, bypass transitions and disabled paths are distinct from settled enabled DSP behavior; use validated offline true-peak analysis on the completed render.

Feedback is the most negative DSP gain in the **last process call**, before makeup and bypass blending, aggregated across channels. The block maximum depends on callback partition size and is not the 400 ms production-meter window. No claim of per-channel GR history, loudness normalization, stereo-linked compression or external sidechain selection is added.

## Software proof and ownership

Production tests compare transfer/knee/makeup and independent channels at 44.1/48/96 kHz, analytic one-pole attack/release equations, linked sample ceiling, lookahead impulse delay, partition invariance, alignment, compatible revisions, invalid controls/reset behavior and zero render heap calls. Private real production fixtures verify child/group/return/master observations, repeated independent readers even after history fills, offline isolation, stale/bypass/identity guards and the native parameter/release/history path. Ordinary SDK fixtures verify existing guarded physical controls, immutable receipt replay, read-token stability, freshness and semantic save/reopen with renewed runtime IDs.

Feedback piggybacks the existing nonblocking scope publication under meter_mutex. Rendering try-locks and drops busy observations rather than blocking; control readers copy a latest snapshot under the same mutex. No allocation or mutable DSP borrow is added. core_time provides monotonic observation age; existing native Kit controls and app-specific DSP/FX ownership are reused. Shared source, adoption and versions are unchanged. Headless/package proofs are separate from graphical and audible human acceptance.
