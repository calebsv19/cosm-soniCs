# Directory: include/engine

Purpose: Engine graph data structures and source interfaces.

## Files
- `buffer.h`: Lightweight view (`EngineBuffer`) used when mixing channel-aligned frame blocks.
- `buffer_pool.h`: Shared scratch allocator with `engine_buffer_pool_init`, `_acquire`, and `_release` for reuse-safe mixing buffers.
- `graph.h`: Rendering graph API that owns sources—`engine_graph_create/configure`, `engine_graph_add_source`, and `engine_graph_render/reset`.
- `sampler.h`: Clip sampler interface; `engine_sampler_source_*` functions schedule portions of `AudioMediaClip` data on the transport.
- `sources.h`: Built-in tone generator declarations plus `engine_tone_source_ops` used as a silent/diagnostic fallback source.
- `engine.h`: High-level engine entry points, including `engine_add_clip_segment` for cloning sub-regions during timeline drops and `engine_remove_track` for pruning timeline lanes.
