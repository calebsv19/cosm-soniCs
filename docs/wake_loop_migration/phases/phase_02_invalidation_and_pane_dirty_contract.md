# Phase 2 - Invalidation and Pane Dirty Contract (Deep Implementation Plan)

## Phase Objective
Introduce a DAW-wide frame invalidation contract and pane dirty metadata so rendering can be driven by explicit change reasons in later phases.

## Scope
In scope:
- Add dirty metadata to DAW `Pane`.
- Add invalidation reason bits + frame invalidation API.
- Map core visual input/layout events to invalidation state.
- Clear pane dirty state after render commit.

Out of scope:
- Wake-blocked loop scheduling (Phase 3).
- Full per-pane partial render optimizations/caching.
- Playback policy tuning.

## Shared-Core Reuse Decision
- No new shared-core dependency added in this phase.
- Phase builds on Phase 1 adapters already integrated.

## Technical Design

## 1) Pane Dirty Metadata
Add to `Pane`:
- `dirty`
- `dirty_reasons`
- `last_render_frame_id`

Add helpers:
- `pane_mark_dirty(Pane* pane, uint32_t reason_bits)`
- `pane_clear_dirty(Pane* pane)`

## 2) DAW Render Invalidation Module
Create DAW-local module:
- `include/core/loop/daw_render_invalidation.h`
- `src/core/loop/daw_render_invalidation.c`

State managed:
- frame invalidated flag
- full redraw required flag
- invalidation reason mask
- frame counter

APIs:
- `daw_render_invalidation_init()`
- `daw_invalidate_pane(...)`
- `daw_invalidate_all(...)`
- `daw_request_full_redraw(...)`
- `daw_has_frame_invalidation()`
- `daw_consume_frame_invalidation(...)`

## 3) Event-to-Invalidation Mapping
In DAW input callback path:
- Keyboard/text/mouse/drop events => input/content invalidation
- Resize/size-changed/exposed window events => layout/resize invalidation

## 4) Layout Change Mapping
- Change `ui_ensure_layout(...)` to return `bool` indicating geometry change.
- When true: mark layout/resize invalidation on all panes.

## 5) Render Commit Contract
In render callback:
- After draw pass, consume frame invalidation and commit frame id.
- On commit, clear pane dirty flags and store `last_render_frame_id`.

## File Plan
New files:
- `daw/include/core/loop/daw_render_invalidation.h`
- `daw/src/core/loop/daw_render_invalidation.c`

Changed files:
- `daw/include/ui/panes.h`
- `daw/src/ui/panes.c`
- `daw/include/ui/layout.h`
- `daw/src/ui/layout.c`
- `daw/src/app/main.c`
- `daw/Makefile`

## Completion Checklist (Execution Tracker)
- [x] P2.1 Add dirty metadata and pane dirty helper APIs.
- [x] P2.2 Add DAW invalidation module header/source.
- [x] P2.3 Wire invalidation init at app startup.
- [x] P2.4 Add input-event invalidation mapping in main input callback.
- [x] P2.5 Convert `ui_ensure_layout` to bool-return and map layout changes to invalidation.
- [x] P2.6 Add render commit path to consume invalidation and clear pane dirty flags.
- [x] P2.7 Register invalidation module source in `Makefile`.
- [x] P2.8 Build passes via `make`.
- [x] P2.9 Update phase doc checklist to completed.

## Phase Exit Criteria
Phase 2 is complete when:
1. P2.1-P2.9 are all complete.
2. DAW builds cleanly.
3. Invalidation and pane dirty state is actively updated from input/layout/render paths.

## Notes
- Phase 2 sets correctness contracts only.
- Phase 3 will use these contracts to gate render/update and block when idle.
- Execution notes:
  - `make -C daw` passes with the new invalidation and dirty-pane contracts in place.
  - Runtime smoke launch executed; startup path reached, but this environment still exits due unavailable display/audio backend (`SDL_Init failed: The video driver did not add any displays` and CoreAudio device unavailable).
