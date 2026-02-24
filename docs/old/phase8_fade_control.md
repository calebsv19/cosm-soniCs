# Phase 8 — Fade Control UX Plan

## Goals
- Provide musicians clear, low-friction controls over clip fade-in/out lengths.
- Make fades visible in the timeline so crossfades can be aligned visually.
- Ensure inspector fields allow precise numeric edits and session persistence already in place reflects changes.

## Timeline UI Touch Points
- **Edge Handles**: Reuse clip trim handles with modifier (e.g., `Alt+drag`) to adjust fade lengths without affecting region bounds. *(Implemented)*
- **Overlay Glyphs**: Render triangular fade overlays at clip edges, scaling proportionally to `fade_in/out_frames` so users see ramps. *(Implemented)*
- **Minimum Size**: Snap fades to zero when handles collapse; enforce clamp at clip duration in engine.

## Inspector Additions
- Fields: `Fade In (ms)`, `Fade Out (ms)` derived from frames; editing updates `engine_clip_set_fades`. *(Implemented via inspector sliders.)*
- Include quick presets (e.g., 0, 10, 50, 100 ms) to make common fades accessible via buttons.

## Defaults & Behaviour
- New clips default to `fade_in=0`, `fade_out=0` (current behaviour); consider global preference for default ramp (e.g., 5 ms) once UI exists.
- Duplicate/segment actions already carry fades—document this in inspector tooltip.
- When clip length shrinks below existing fade length, clamp via existing setter and visually indicate through snapped overlay.

## Follow-up Tasks
1. Implement timeline fade handles + overlay rendering. *(Done)*
2. Surface inspector controls + hook to `engine_clip_set_fades`. *(Done)*
3. Optionally add preference for default fade length in config/session schema.
