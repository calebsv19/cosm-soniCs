# Minimal DAW Prototype — Sprint Roadmap

This repository hosts an in-progress, minimal C-based DAW prototype built on SDL2. The sprint-style roadmap below tracks feature phases, while this snapshot captures the current state and near-term goals.

## Project Snapshot *(Phase 9 in progress)*
- **Goal**: Multi-track DAW prototype with SDL2 front end and a lock-free, real-time-safe audio engine. Makefile builds `build/daw_app`.
- **Architecture**: C sources under `src/` (app, engine, audio, input, UI) with matching headers in `include/`; runtime assets in `assets/`; configuration defaults in `config/`.
- **Performance HUD**: Uses the shared TimerHUD library with a DAW adapter in `src/render/timer_hud_adapter.c` and settings in `config/timer_hud_settings.json`.
- **Vulkan Backend**: Builds against the shared Vulkan renderer in `shared/vk_renderer`, with app setup in `SDLApp/sdl_app_framework.c`.
- **Runtime Threads**: Real-time audio callback executes the graph; engine thread handles graph edits, transport, and command queues; background workers manage media decode.
- **UI State**: `AppState` mediates timeline layout, transport bar, loop state, track mute/solo, clip inspector overlay, and library interactions.
- **Current Feature Set**:
  - Timeline supports multi-track layout, clip drag/trim with snapping, loop band, mute/solo headers, and selection aware inspector.
  - Transport bar exposes play/stop, seek bar with MM:SS.mmm counter, loop toggles with draggable in/out handles, zoom sliders, grid toggle, and fit width/height controls.
- Engine honors seek/loop commands, rebuilds track sources respecting mute/solo, and emits silence when idle (no fallback tone).
- Session persistence auto-loads from and auto-saves to `config/last_session.json` (Phase 7 complete).
- Library/decoder pipeline accepts both WAV and MP3 assets (MP3 decoded via CoreAudio on macOS).
- Decoded audio cache reuses media buffers across clips referencing the same file/sample-rate to reduce load overhead.
- Mixer pane shows a master effects rack when no clip is selected, with a two-layer add-effect overlay (category → effect) that includes wheel/scroll-bar navigation, plus stacked sliders that dynamically resize with the pane for live tweaks and removal buttons.
- Configurable diagnostics: F7/F8/F9 toggle engine, cache, and timing logs with an on-screen status strip; library browser displays clip durations and drop previews match the real clip length.
- **Focus Ahead**: Phase 8 engine housekeeping—finalize caching QA, configurable fade defaults, and overlap priority polish.

## Phase 1 — Timeline Rendering Pass *(complete)*
- Draw timeline grid with time ruler and playhead tied to `engine_get_transport_frame`.
- Render clip labels using file names and show a ghost clip during library drag, snapping to the current grid subdivision.

## Phase 2 — Clip Model Enhancements *(complete)*
- Expand `EngineClip` with timeline start, duration, and source offset.
- Ensure sampler playback respects clip offsets/length; renderer uses new fields.

## Phase 3 — Clip Manipulation *(complete)*
- Support dragging clips along the timeline (with snapping) and trimming via edge handles.
- Highlight selected clips and track selection state in `AppState`.

## Phase 4 — Clip Inspector & Commands *(complete)*
- Double-click to open a clip inspector (rename, gain).
- Wire per-clip gain into engine; add shortcuts for delete/duplicate.

## Phase 5 — Multi-Track Foundations *(complete)*
- Stack timeline lanes per `EngineTrack`, with headers for rename/mute/solo.
- Support library drag/drop into tracks, track selection state, and clip rebuilds that honor mute/solo.

## Phase 6 — Transport UI Upgrade *(complete)*
- Transport play/stop, seek bar with draggable handle, and real-time MM:SS.mmm display.
- Loop toggle plus draggable loop start/end handles with tinted band and cross-track snapping.
- Zoom sliders, grid toggle, and fit width/height controls integrated with timeline layout; inspector overlays render after track content.

## Phase 7 — Session Persistence v1 *(complete)*
- Defined the canonical session schema (`include/session.h`, `src/session/README.md`) covering tracks, clips, loop/layout/library state, and transport info.
- Added JSON save/load helpers with validation plus a smoke test (`make test-session`) to guard round-trips.
- Integrated autosave on shutdown and autoload on startup via `config/last_session.json`, restoring layout, loop, selection, and clip placement.
- Updated documentation to reflect the new persistence workflow.

## Phase 8 — Engine Housekeeping *(completed)*
- ✅ Fade metadata persisted in session and applied at runtime with per-sample ramps (`engine_clip_set_fades`, sampler fades).
- ✅ Timeline/inspector controls expose fades (Alt-drag overlays, sliders + presets).
- ✅ Audio media cache shares decoded buffers across clips.
- ✅ Configurable default fade lengths/presets surfaced in UI/config.
- ✅ Stress-test media cache (duplicate-heavy sessions, rapid add/remove) and document the coverage.
- ✅ Deterministic overlap priority for clips sharing a start frame with regression coverage.
- **Next**: Phase 8 housekeeping checklist complete; roll remaining effort into Phase 9 polish.

## Phase 9 — Integration & Polish *(in progress)*
- ✅ Logging toggles with config + F7/F8/F9 shortcuts, verbose cache tracing, and audio worker timing instrumentation.
- ✅ Library browser durations and timeline drop previews aligned to real clip length.
- 🔄 Continue end-to-end QA sweeps (timeline edits, transport loop/seek, autosave restore) and capture remaining polish items.
- 🔄 Refresh docs/tests as new instrumentation lands (see test targets below).

Keep this roadmap up to date as each phase completes or is re-scoped. Future chats can reference this file to pick up where the sprint left off.

## Test Targets
- `make test-session` — Session serialization round-trip.
- `make test-cache` — Media cache stress harness.
- `make test-overlap` — Equal-start clip ordering regression.
- `make test-smoke` — Engine API smoke (load clip, fades, loop/play toggles).
