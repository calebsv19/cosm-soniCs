# Directory: include/engine

Purpose: Engine graph data structures and source interfaces.

## Files
- `buffer.h`: Lightweight view (`EngineBuffer`) used when mixing channel-aligned frame blocks.
- `buffer_pool.h`: Shared scratch allocator with `engine_buffer_pool_init`, `_acquire`, and `_release` for reuse-safe mixing buffers.
- `graph.h`: Rendering graph API that owns sources—`engine_graph_create/configure`, `engine_graph_add_source`, and `engine_graph_render/reset`.
- `instrument.h`: Built-in MIDI instrument preset API, parameter metadata/default/clamp helpers, and source renderer for bounded MIDI note snapshots.
- `midi.h`: Model-only MIDI note list helpers for bounded note storage, validation, sorting, and mutation.
- `sampler.h`: Clip sampler interface; `engine_sampler_source_*` functions schedule portions of `AudioMediaClip` data on the transport.
- `sources.h`: Built-in tone generator declarations plus `engine_tone_source_ops` used as a silent/diagnostic fallback source.
- `timeline_contract.h`: App-local frame/beat/source-region helpers for half-open transport ranges, beat-to-frame conversion, and structural overlap planning.
- `engine.h`: High-level engine entry points, including audio clip APIs, MIDI clip/note/preset/parameter APIs, live MIDI audition note APIs, `engine_add_clip_segment` for cloning sub-regions during timeline drops, `engine_remove_track` for pruning timeline lanes, `engine_track_set_name`, `engine_track_set_muted`, `engine_track_set_solo` for per-track control, and transport helpers (`engine_transport_seek`, `engine_transport_set_loop`).
