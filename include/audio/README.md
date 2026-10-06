# Directory: include/audio

Purpose: Audio asset definitions exposed to both engine and UI.

## Files
- `audio_capture_device.h`: SDL-backed capture-device wrapper for default or named microphone input, negotiated float32 capture specs, start/stop controls, and capture error reporting.
- `media_clip.h`: Describes `AudioMediaClip` buffers plus the unified loader (`audio_media_clip_load`) that accepts WAV/MP3 input and the lifetime helpers (`audio_media_clip_free`).

S4.6 exposes `WavStreamWriter` in `wav_writer.h` for checked chunked output with continuous dither, abort, and precise publication/durability results. `take_journal.h` supports fixed-buffer prefix recovery and optional exact-length finalization through `daw_take_journal_publish`. See [S4.6 contracts](../../docs/improvement/S4-STREAMING.md).


S4.7 exposes controlled media decode/probe, prepared cache/registry adoption, resident-byte accounting and the opaque media-job API. The default sample-buffer limit is 64 MiB per source/output buffer; this does not cap active project residency. See [S4.7](../../docs/improvement/S4.7-MEDIA.md).
