# S3.2 — Calibrated spectrum and spectrogram meaning

## 1. Implemented contract

1. Both existing analyzers use one UI-independent Hann-windowed tonal-amplitude calculation. Window coherent gain and the one-sided amplitude factor are accounted for; DC/Nyquist endpoints use the corresponding single-sided factor. Spectrum's hidden A-weighting is removed. Output is flat tonal peak-amplitude dBFS, not loudness, noise density, true peak, or an integrated band-power measurement.
2. Existing channel reduction is preserved and explicitly labeled **Mid**: `(L+R)/2` for stereo and the sole channel for mono. Opposite-polarity stereo cancels by definition. This slice does not add selectable L/R/side or A-weighted modes. Outputs with more than two channels still analyze the first two; no surround energy claim is made.
3. Spectrum retains 2048-sample windows, 256 log-spaced display frequencies, and two-window smoothing, now as an arithmetic mean of powers followed by dB conversion. No additional UI-frame-rate-dependent smoothing or stale result hold is applied. Display scale maps the full -60..+6 dBFS interval linearly; the EQ response retains its separate gain scale.
4. Spectrogram retains 1024-sample windows, 128 log-spaced frequencies, 160 history columns, and -60..0 dBFS colors. Equal measurements now have equal colors at every history age. Its time guides describe relative captured-window age, newest at the left, rather than inferred UI/beat timing. Relative age ends at the latest captured window, not wall-clock now; a non-running insert produces no new measurements. At 48 kHz the windows are about 21.33 ms and the oldest-column offset is about 3.392 s.
5. Frequency grids end at the smaller of 20 kHz and Nyquist. The spectrum trace stops at that frequency within the existing EQ frequency axis. Spectrogram labels use its actual sample rate. Display-bin density is not frequency resolution: window duration still limits separation of nearby tones. Off-grid tones, very low frequencies, and frequencies near Nyquist exhibit finite-window leakage; arbitrary tones are not guaranteed to land on a displayed bin.
6. Capture packets identify selection, transport epoch, sequence number, and first sample relative to the capture segment. Transport discontinuities discard producer-owned partial windows; consumers reject old epochs, and getters hide old results immediately. Sequence gaps reset spectrum averaging and spectrogram history rather than bridging missing windows. A render block with no selected-tap callback (for example a bypassed meter) also discards partial assembly and marks a new capture segment on resumption. Ordinary loop wraps remain continuous rendered audio and can contribute their real waveform discontinuity to analysis.
7. Signal labels describe actual existing taps: track spectrum is post track FX/EQ and pre pan; master spectrum is post master FX/sanitization; spectrogram measures the selected meter insert, after preceding inserts and before following inserts. Spectrogram snapshots include the actual track/instance identity and captured-window timing metadata. No taps were moved.

The changes affect measurement/display, not the audio processing signal. Previously selected analysis views can show different levels because the earlier scaling and weighting were misleading. Other meter families (LUFS, gain reduction, phase views), general DSP processing, and physical device latency are outside this slice.

## 2. Acceptance

- `test-analysis-calibration`: known half-scale tones at 1/8 kHz across 44.1/48/96 kHz and both window sizes; silence; DC/Nyquist scaling; arithmetic power mean; Nyquist-limited grid; actual spectrum and spectrogram consumers; gap resets; epoch invalidation; mid cancellation; effect identity and sample-based history metadata; bypass/resume partial-window and history isolation.
- `test-engine-analysis-lifecycle`: whole-window overflow and sequence stamps, selection changes, partial-window isolation, startup rollback, repeated restarts, and live analyzer edits.
- `test-kitviz-meter-adapter`: existing palette/range tests plus equal-color-at-equal-level across history ages.
- Full stable/legacy, focused ASan/TSan, and final build results are recorded in [the receipt](evidence/s3-analysis-calibration.json).

Tests prove software signal/state behavior. Manual visual/listening acceptance is not claimed. No efficient FFT replacement or sustained-performance certification is included; the existing log-frequency analysis kernel remains bounded per window and runs on analyzer threads.

## 3. Reuse and boundary

Existing `core_math`/`kit_viz` ownership and the DAW's vendored adapters were considered. **Reuse adopted:** retain existing window queues, analyzer workers, rendering adapter, and history-grid drawing. **Reuse deferred:** keep the specific tonal normalization/window/tap contract in `engine/analysis_math.h`; no general shared DSP library or new viewing mode is warranted by this correction. Changes to the meter adapter are DAW-local. No shared versions or adoption metadata change.

S3.2's spectrum/spectrogram implementation is complete at this boundary. The optional move into S3.3 produced a fresh limiter audit and reproducible probe, documented in [S3-DYNAMICS-AUDIT.md](S3-DYNAMICS-AUDIT.md). Limiter production behavior has not been changed in this slice.
