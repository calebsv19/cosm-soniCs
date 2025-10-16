# Directory: src/audio

Purpose: Platform audio plumbing, buffering utilities, and media decoding.

## Files
- `audio_queue.c`
  - `audio_queue_init/free`: Allocate or dispose of the interleaved float ring buffer.
  - `audio_queue_write/read`: Push frames to or pull frames from the queue.
  - `audio_queue_available_frames/space_frames`: Inspect how many frames are queued or free.
  - `audio_queue_clear`: Flush queued samples without freeing memory.
- `device_sdl.c`
  - `audio_device_open`: Initialise SDL audio, choose a device, and capture the negotiated spec.
  - `audio_device_close`: Stop playback and close the SDL device.
  - `audio_device_start/stop`: Control SDL's pause state for streaming callbacks.
- `media_clip.c`
  - `audio_media_clip_load`: Detect file type (WAV/MP3), decode to float32, optionally resample, and fill an `AudioMediaClip`.
  - `audio_media_clip_free`: Release heap-backed sample buffers.
- `ringbuf.c`
  - `ringbuf_init/free/reset`: Manage the underlying byte buffer and indices.
  - `ringbuf_write/read`: Lock-free producer/consumer operations with wrap handling.
  - `ringbuf_available_write/read`: Report write/read capacity in bytes.
