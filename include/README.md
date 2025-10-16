# Directory: include

Purpose: Public headers that expose the DAW engine, UI, and platform APIs.

## Files
- `app_state.h`: Central structure describing UI panes, drag state, engine pointers, and shared runtime data.
- `audio_device.h`: SDL-backed device wrapper (`audio_device_*` calls) that opens, starts, and stops the playback stream.
- `audio_queue.h`: Lock-free float ring buffer facade; see `audio_queue_*` helpers for pushing/pulling interleaved frames.
- `config.h`: Config loader surface with `config_set_defaults` and `config_load_file`.
- `engine.h`: High-level engine control/clip management API consumed by the UI.
- `ringbuf.h`: Low-level single-producer/single-consumer ring buffer primitives used by the audio queue and engine commands.

## Subdirectories
- `audio/`: Types for decoded media clips.
- `engine/`: Internal engine data structures, buffer pools, and graph/sampler interfaces.
- `input/`: Handlers that feed mouse/keyboard input into the app state.
- `ui/`: Layout, widgets, and rendering entry points.
