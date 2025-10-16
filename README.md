# Minimal DAW Prototype — Sprint Roadmap

This repository hosts an in-progress, minimal C-based DAW prototype built on SDL2. The current focus is a short-term sprint that incrementally grows the engine, media pipeline, and UI. Below is the working checklist for the sprint (Phases 1–9) and their intent.

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

## Phase 5 — Multi-Track Foundations
- Allow multiple `EngineTrack` instances; stack lanes in timeline UI.
- Provide UI controls to add/remove tracks and route newly loaded clips.

## Phase 6 — Transport UI Upgrade
- Add seek bar with drag-to-seek and time display.
- Introduce loop in/out markers editable from the transport area.

## Phase 7 — Session Persistence v1
- Define JSON format for tracks, clips, layout ratios, loop settings.
- Implement save-on-exit/load-on-start behavior.

## Phase 8 — Engine Housekeeping
- Support multiple clips per track (sorted schedule) with fade-in/out ramps.
- Cache decoded/resampled audio to avoid redundant work.

## Phase 9 — Integration & Polish
- End-to-end QA: drag/trim across tracks, transport loop/seek, session restore.
- Add optional logging toggles; stabilize UX for the new interactions.

Keep this roadmap up to date as each phase completes or is re-scoped. Future chats can reference this file to pick up where the sprint left off.
