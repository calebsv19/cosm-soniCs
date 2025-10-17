# Phase 8 — Engine Housekeeping: Track Scheduling Snapshot

## Current Scheduling Behaviour
- Each `EngineTrack` owns a dynamic array of `EngineClip` structs; clips are sorted by `timeline_start_frames` after every edit (`engine_track_sort_clips`).
- During `engine_rebuild_sources` the engine iterates tracks, multiplies track gain, and registers every clip’s sampler as an independent source in the graph. Track mute/solo is applied before registration.
- `EngineSamplerSource` renders by checking the global transport frame against its start/duration window and pulls sample data directly from the `AudioMediaClip`. There is no look-ahead or overlap policy—sources simply sum in the graph mixer.
- The mixer (`engine_graph_render`) processes each source in sequence, zeroes a scratch buffer, renders the source, and accumulates the result into the output buffer with the supplied gain.

## Identified Gaps / Risks
- No fade-in/out support: abrupt clip boundaries produce clicks, especially when clips start or end away from zero-crossings.
- Overlapping clips on the same track are mathematically summed, but there is no envelope management; users cannot define crossfade lengths or volume ramps per overlap.
- Track ordering is stable, but clips with identical start frames rely on current sort behaviour (stable but undefined with equal keys). Deterministic overlap priority should be explicit once fades are introduced.
- Transport rebuild happens on every structural change. Without caching, heavy overlap regions require decoding each clip independently (handled in Phase 8 caching work).

## Fade / Envelope Metadata Requirements
- Extend `EngineClip` with fade descriptors, e.g. `fade_in_frames`, `fade_out_frames`, and possibly curve type (linear vs. equal-power). *(Implemented: linear fades stored in clip/session metadata.)*
- Sampler renderer must apply per-sample gain ramp inside the active window using the cached fade lengths. *(Implemented: linear ramp in `engine_sampler_source_render`.)*
- UI/API needs setters to expose fade handles (timeline edge drag) or inspector fields; session schema must persist the new parameters.

## Suggested Next Steps
1. Design fade metadata additions and update session schema / docs (Phase 8 Step 2).
2. Implement sampler ramping respecting fade-in/out per clip; verify with overlap unit tests and render assertions.
3. Revisit sort stability or add explicit overlap priority flag to avoid surprises with equal start times.
4. Feed findings into caching design (following Phase 8 plan) so decoded audio reuse respects fades and segment splits. *(Done — cache implemented.)*

## Remaining Tasks
1. Add optional defaults/presets for fade lengths (config + UI affordances). *(Done)*
2. Profile new caching path under heavy overlap and add targeted tests. *(Done via `media_cache_stress_test`.)*
3. Establish deterministic ordering for clips sharing a start frame and cover with overlap tests. *(Done via `creation_index` ordering and `test-overlap` harness.)*
