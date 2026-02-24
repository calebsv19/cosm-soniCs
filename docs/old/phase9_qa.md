# Phase 9 QA Sweep — Initial Findings

## Manual Scenarios to Exercise
- **Timeline editing**: Drag existing clips across tracks, trim edges, and ensure snap/grid obeys loop boundaries. Confirm fade handles remain responsive after trims and inspector edits.
- **Library drops**: Drag multiple assets from the library into the timeline, including rapid consecutive drops into the same track, and verify the preview ghost duration matches the clip length.
- **Transport loop/seek**: Toggle loop mode while playing, scrub the playhead, and ensure the loop band updates in both timeline and transport UI.
- **Session persistence**: Start a fresh session, create edits, quit to trigger autosave, then relaunch and verify layout, selection, fades, and loop state restore correctly.

## Observed Gaps / Risks
- **Overlap policy**: Ensure the new `engine_track_apply_no_overlap` helper continues to enforce trims without introducing audible pops; add stress tests with dense edits to validate batching efficiency.
- **Fade feedback**: Inspector preset buttons correctly update fades, but timeline handles do not currently visualise preset changes until the next hover update. A hover/drag refresh pass will smooth the UX.
- **Logging visibility**: F7/F8/F9 keyboard toggles now expose engine/cache/timing diagnostics with an on-screen status strip; ensure QA enables them when collecting traces.

## Next Actions
1. Introduce an automated smoke test that drives engine start → clip load → loop toggle via the existing API to catch regressions.
2. Add logging toggles for audio engine, transport timing, and media cache reuse.
3. Polish UI feedback for fade presets and drop previews (e.g., derive ghost width from clip frame count before applying defaults).
4. Revisit overlap splitting to reference cached media or defer duplication to background workers.
