# S3.5 — Supported import and sample-rate conversion

## 1. Implemented behavior

1. Native little-endian RIFF WAV decoding supports unsigned PCM8, signed PCM16/24/32, and IEEE float32, including their supported WAVEFORMATEXTENSIBLE subtypes. Channels retain their interleaved identity. This does not add speaker-layout remapping, automatic downmixing, RF64, RIFX, compressed WAV, or float64 support.
2. The decoder checks RIFF/chunk bounds, padding, format length, supported subtype, valid-bit fields, channel-mask population when present, block alignment, byte rate, whole frames, nonempty data, allocation sizes, and finite float samples. Malformed input returns failure without replacing the caller's output object. Integer container decoding uses explicit little-endian assembly and sign extension.
3. Offline conversion now uses normalized Blackman-windowed sinc coefficients with 32 lobes, 1024 interpolated phase intervals, and a cutoff at 94% of the lower Nyquist frequency. The guard band intentionally attenuates material near that limit. Conversion supports target/source ratios from 1/32 through 32. Work and coefficient storage are bounded by that ratio; decoded and output audio remain full-file allocations.
4. Output length is nearest-integer `source_frames * target_rate / source_rate`, with a minimum of one frame for nonempty input. Rational source positioning avoids accumulated position drift. Normalized coefficients preserve DC; edges extend the nearest source sample. Same-rate audio preserves the decoded samples exactly. Filtering may overshoot; samples are not silently clipped or normalized.
5. Apple MP3 decoding requests source-rate float samples, then uses the same conversion path as WAV. The FFmpeg fallback similarly decodes source-rate float WAV before common conversion. This fixes the previous source-frame/destination-frame duration mismatch. Unknown-length platform reads now start with a complete read buffer, and growth/empty-output handling is checked. FFmpeg remains an optional fallback executable; decoder padding can differ between implementations.
6. Decode, file IO, coefficient preparation, and conversion remain outside the audio callback. This is an import correctness repair, not streaming or large-library cache architecture.

## 2. Acceptance

`test-media-conversion` covers native integer/float/extensible fixtures, mono/stereo identity, odd chunk padding, malformed alignment/truncation/NaN input, allocation failures preserving output, 25 rate pairs across 8/44.1/48/96/192 kHz, DC, short input, same-rate exactness, unsupported ratio/size rejection, centered channel-isolated impulses, passband gain, downsample alias rejection, and upsample image rejection. It is part of `test-stable`.

A generated 48 kHz MP3 fixture additionally exercises the native Apple path and a separately compiled FFmpeg fallback, comparing converted duration with each path's own decoded source duration. The fallback build on macOS proves that C path with the installed FFmpeg; it is not a Linux runtime qualification.

Measured 96-to-48 kHz conversion preserves a 0.5-amplitude 1 kHz tone at approximately 0.49984994 amplitude and leaves approximately 0.000003577 amplitude at the folded 18 kHz component from a 30 kHz input. These are fixture-specific measurements, not a universal filter specification. Final commands and fingerprints are in [the S3.5 receipt](evidence/s3-media-conversion.json).

## 3. Placement and remaining boundaries

The offline DAW converter is `audio/resample.h` / `src/audio/resample.c`; no shared dependency/version changes are needed. Full-file memory use, asynchronous decode scheduling, very large files, and sustained throughput belong to S4. No additional file-format UI or sample-rate setting was introduced.
