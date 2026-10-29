# Dedicated EQ and captured channel measurements

Main Edit exposes `get_eq`, `edit_eq`, `undo_eq` and `redo_eq` for the native dedicated EQ processor. Omit `target` for master; use a generation-bound track handle for source children, group buses or returns. Dedicated EQ is separate from the insert chain addressed by effect-chain tools. It uses the existing four peaking bands plus fixed 12 dB/oct low/high cuts and the same production DSP as native input.

## Curve editing

`get_eq` returns a complete `curve` and `expected_state`. Each cut has Boolean `enabled` and `frequency_hz`; each of the four bands has Boolean `enabled`, `frequency_hz`, `gain_db` and `width_octaves`. Frequency is 20–20,000 Hz; enabled frequencies must also be at most 0.45 times the sample rate, reported as `enabled_frequency_max_hz`. Gain spans -20 to +20 dB. Bandwidth spans 0.1–4 octaves, not numerical Q. There is no editable cut-slope field because the DSP uses fixed two-pole cuts.

`edit_eq` submits the complete curve, exact token, current endpoint instance and immutable request ID. One accepted curve creates one reserved undo step. Eight copied states are retained; identical curves reuse tokens. Tokens cover project/track lifetime and every stored curve field, including disabled controls. They exclude mixer values, inserts, notes and instruments. MCP writes/history require stopped transport, no recording and no conflicting authoring/text/modal owner. Native continuous edits retain their existing gesture owner but publish through the common exact-curve guard.

`undo_eq` and `redo_eq` accept only dedicated EQ for the matching target at the shared history cursor. They refuse a changed current curve or recycled lifetime; they never skip a different command or replace unrelated sound settings. Native resets/drags use the same guarded history. Recover uncertain writes with `request_status` using the original instance/request ID before deliberate retry. Reacquire target handles after project replacement.

Native Save/Load persists the accepted curve independently of UI drafts. Group EQ applies after child processing in the group bus path; master EQ precedes master inserts. Captured group analysis includes descendant sources and selected-bus processing plus master output; it is not a pre-master bus tap.

## Captured channel measurements

`analyze_composition` adds `metrics.channels`, preserving existing aggregate fields and plot arrays. Each channel reports zero-based index/label, linear `sample_peak` and `rms`, `sample_peak_dbfs`, `rms_dbfs`, Boolean `silent`, and exact decimal-string `full_scale_samples` / `overload_samples`. Mono is labelled mono, stereo is left/right; other supported layouts use indexed channels. Up to eight channels are supported by this bounded capture analysis.

Measurements cover all returned float frames, including the explicit tail. Peak is the maximum absolute sample; RMS is unweighted square-root mean energy over that channel's frame count. dBFS is 20 log10 of amplitude with 1 as full scale. Silence returns linear zero, `silent: true` and JSON null for both dBFS values, representing negative infinity without sending nonfinite JSON. Full-scale counts include abs(sample) >= 1; overload counts include abs(sample) > 1. Aggregate `full_scale_samples` remains compatible and `overload_samples` is added separately. A full-scale hit is not proof of analog/device clipping.

These are immutable offline captures, not live meters, LUFS or oversampled true-peak. No normalization, inserted metering processor, authored edit or mutation receipt is needed for analysis. Existing 10-second material, five-second tail, four-capture and 64 MiB limits remain. The 128-point Hann spectrum still analyzes a mean-of-channels window and can hide anti-phase stereo energy; channel RMS/peak retain the independent channel signals.

## Unicode persistence

The native session string reader now decodes JSON Unicode escapes into the same UTF-8 bytes as raw UTF-8, including surrogate pairs. Valid track/group/region names retain their spelling on Save/Load. Malformed escapes, invalid UTF-8, embedded NUL, unterminated strings and destination overflow refuse the document rather than replacing characters with question marks or truncating them. This does not repair already-saved question marks or change an open user's project; reload corrected source names only deliberately after saving edits.

Qualification uses private endpoints and isolated known signals / sampled violin and clarinet fixtures. It verifies calibration, DSP parity, native/MCP interleavings, exact scoped history, failure rollback, receipts, saved/reopened curves/audio and raw/escaped Unicode. The open normal Main Edit app is preserved; a verified candidate package requires a separate saved/closed installation boundary.
