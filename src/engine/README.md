# Directory: src/engine

Purpose: Real-time audio engine, graph, and source implementations.

## Files
- `buffer_pool.c`
  - `engine_buffer_pool_init/free`: Allocate contiguous scratch storage for multi-channel blocks.
  - `engine_buffer_pool_acquire/release`: Hand out per-frame channel views and mark them as in-use.
- `engine.c`
  - Transport/device control (`engine_create/start/stop/destroy`, `engine_is_running`, `engine_transport_play/stop/is_playing`).
  - Clip + track management (`engine_add_track`, `engine_remove_track`, `engine_track_set_name`, `engine_track_set_muted`, `engine_track_set_solo`, `engine_add_clip[_to_track]`, `engine_remove_clip`, `engine_duplicate_clip`, setters for timeline/gain/name/region).
  - Transport helpers (`engine_transport_play/stop/is_playing/seek/set_loop`) and command queue plumbing for worker-thread safe updates.
  - Segment utilities (`engine_add_clip_segment`) to clone portions of an existing clip when splitting around drops.
  - Graph wiring (`engine_queue_graph_swap`, `engine_rebuild_sources`) and command processing (`engine_post_command`, worker thread loop).
  - Utility helpers for sampler refresh, clip sorting, and transport frame tracking.
- `graph.c`
  - `engine_graph_create/destroy/configure`: Own the mixing graph and its buffer pool.
  - `engine_graph_add_source/clear_sources`: Register sampler/tone sources with optional reset callbacks.
  - `engine_graph_render/reset`: Mix active sources into an interleaved block each engine tick.
  - `engine_graph_get_*`: Expose graph configuration (channels/sample-rate/max block/pool).
- `sampler.c`
  - `engine_sampler_source_create/destroy`: Manage per-clip sampler instances.
  - `engine_sampler_source_set_clip`: Map a media clip segment onto the global transport timeline.
  - `engine_sampler_source_render/reset`: Produce interleaved audio frames from the scheduled segment.
  - Accessors for start frame, frame counts, offsets, and underlying media.
- `source_tone.c`
  - `engine_tone_source_create/destroy`: Allocate a diagnostic tone generator.
  - `engine_tone_source_render/reset`: Fill buffers with a simple sine tone, keeping phase continuity.
  - `engine_tone_source_ops`: Populate an `EngineGraphSourceOps` struct for registration.
