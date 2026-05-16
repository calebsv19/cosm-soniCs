# Directory: include/audio

Purpose: Audio asset definitions exposed to both engine and UI.

## Files
- `audio_capture_device.h`: SDL-backed capture-device wrapper for default or named microphone input, negotiated float32 capture specs, start/stop controls, and capture error reporting.
- `media_clip.h`: Describes `AudioMediaClip` buffers plus the unified loader (`audio_media_clip_load`) that accepts WAV/MP3 input and the lifetime helpers (`audio_media_clip_free`).
