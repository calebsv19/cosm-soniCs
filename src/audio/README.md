# Directory: src/audio

Purpose: Platform audio plumbing, buffering utilities, and media decoding.

## Files
- `audio_queue.c`
  - `audio_queue_init/free`: Allocate or dispose of the interleaved float ring buffer.
  - `audio_queue_write/read`: Push or pull only complete interleaved frames, including layouts whose frame stride does not divide the byte-ring capacity.
  - `audio_queue_available_frames/space_frames`: Inspect how many frames are queued or free.
  - `audio_queue_clear`: Publish the producer's flush boundary; the consumer discards through that boundary on its next read without either side resetting live indices.
- `device_sdl.c`
  - `audio_device_open`: Require float callbacks at the engine's requested rate/channel layout, allow block-size negotiation, and close any endpoint violating that contract.
  - `audio_device_close`: Stop playback and close the SDL device.
  - `audio_device_start/stop`: Control SDL's pause state for streaming callbacks.
- `audio_capture_device_sdl.c`
  - `audio_capture_device_open`: Initialise SDL audio capture, choose the default or named input device, and require float32 capture callbacks while allowing sample-rate/channel/block-size negotiation.
  - `audio_capture_device_start/stop/close`: Control microphone capture lifetime and keep callback teardown outside timeline/engine recording policy.
  - Capture callbacks should enqueue frames only; DAW-local recording policy drains and finalizes takes in `src/app/audio_recording.c`.
- `media_clip.c`
  - `audio_media_clip_load`: Detect file type (WAV/MP3), decode to float32, optionally resample, and fill an `AudioMediaClip`.
  - `audio_media_clip_free`: Release heap-backed sample buffers.
- `media_cache.c`
  - `audio_media_cache_*`: Manage a simple in-memory cache so the engine can reuse decoded clips across multiple timeline references.
- `ringbuf.c`
  - `ringbuf_init/free/reset`: Manage the underlying byte buffer and indices.
  - `ringbuf_write/read`: Lock-free producer/consumer operations with wrap handling.
  - `ringbuf_available_write/read`: Report write/read capacity in bytes.


Media-registry saves now stage, flush, sync, and atomically publish a complete file; failed publication retains the previous manifest and its in-memory dirty flag for retry. See the [S2.1 contract](../../docs/improvement/S2-SAVING.md). WAV durability is now handled separately by the S2.5 writer described below; metadata and audio still do not form one multi-file transaction.

The engine output consumer publishes delivered-frame counts and a monotonic callback timestamp for software presentation estimation. SDL callback delivery is not a measured DAC position. Transport flushes now use a brief SDL endpoint exclusion while the worker resets the output ring and clock epoch; other generic `AudioQueue` users retain the producer-requested consumer-owned flush API.

`wav_writer.c` now stages checked PCM16/float WAV output through `DawSaveFile`; boolean success requires file and directory sync, while the detailed PCM result distinguishes visible-but-not-synced publication. `take_journal.c` appends ordered checksummed float records and recovers a complete prefix while preserving the source. Capture endpoints keep the requested project rate/channel layout. See [S2.5](../../docs/improvement/S2-RECORDING-DURABILITY.md); S4.6 now bounds the recent preview and streams journal recovery/finalization.

`media_clip.c` checks supported little-endian PCM8/16/24/32 and float32 WAV chunks, including extensible subtypes, without replacing caller output on failure. Apple/FFmpeg MP3 paths decode at source rate before common conversion. `resample.c` performs offline normalized windowed-sinc conversion with interpolated phases, bounded rate ratios, explicit rounded duration, and unchanged channel identity. Full-file memory use and streaming remain S4 work. See [S3.5](../../docs/improvement/S3-MEDIA-CONVERSION.md).

`WavStreamWriter` appends arbitrary complete-frame chunks with continuous dither state and patches the final RIFF header before durable publication. Failed appends poison the candidate; abort preserves an existing destination. `daw_take_journal_publish` can require an exact complete frame count for Finish; explicit recovery permits a validated prefix. Both use fixed conversion/read buffers. See [streaming and failure contracts](../../docs/improvement/S4-STREAMING.md).


S4.7 adds `media_jobs.c`: one shared decoder worker, four owned request/result slots, cancellation and per-buffer admission limits. Metadata probing and registry content hashing run on workers; cache/registry adoption stays on control. Cache versions coexist while pinned, with zero inactive retention. See [the contracts and measured limits](../../docs/improvement/S4.7-MEDIA.md).
