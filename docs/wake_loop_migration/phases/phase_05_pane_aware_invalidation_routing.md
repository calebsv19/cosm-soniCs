# Phase 5 - Pane-Aware Invalidation Routing (Deep Implementation Plan)

## Phase Objective
Reduce invalidation fanout by routing high-frequency input invalidation (especially pointer events) to affected panes instead of marking all panes dirty for every event.

## Why This Phase Exists
Phase 4 removed playback busy-spin and aligned updates with render cadence. The next bottleneck is invalidation breadth: many input events still invalidate all panes, causing unnecessary dirty state churn and redraw pressure.

## Scope
In scope:
- Add pane hit-testing helpers in pane manager.
- Route pointer-driven invalidation to targeted panes.
- Keep full invalidation for layout/window/global events.
- Add diagnostics visibility for dirty pane counts.

Out of scope:
- True partial framebuffer compositing.
- Per-pane cached render targets.
- Deep input subsystem refactors.

## Shared-Core Reuse Decision
No new shared-core module changes in this phase. Continue using current loop stack:
- `core_time`, `core_sched`, `core_jobs`, `core_queue`, `core_wake`, `core_kernel`.

## Runtime Contract (Phase 5 Target)
For input-driven invalidation:
1. Pointer events invalidate hit/hover panes only.
2. Keyboard/drop/global events retain full invalidation for correctness.
3. Resize/layout events retain full invalidation.
4. Diagnostics expose dirty pane count per reporting interval.

## File Plan
Primary files:
- `daw/include/ui/panes.h`
- `daw/src/ui/panes.c`
- `daw/src/app/main.c`

Doc updates:
- `daw/docs/wake_loop_migration/phases/phase_05_pane_aware_invalidation_routing.md`
- `daw/src/app/README.md`

## Execution Checklist
- [x] P5.1 Add Phase 5 doc and checklist.
- [x] P5.2 Add pane hit-test helper API to pane manager.
- [x] P5.3 Add dirty-pane counting helper for diagnostics.
- [x] P5.4 Implement event-to-pane invalidation routing in app input path.
- [x] P5.5 Keep full invalidation paths for resize/global events.
- [x] P5.6 Extend loop diagnostics output with dirty pane count.
- [x] P5.7 Build passes (`make -C daw`).
- [x] P5.8 Smoke validation run completed and notes captured.
- [x] P5.9 Mark Phase 5 checklist complete with execution notes.

Execution notes:
- `daw/include/ui/panes.h` and `daw/src/ui/panes.c` now expose:
  - `pane_manager_hit_test(...)` for targeted pointer invalidation.
  - `pane_manager_count_dirty(...)` for diagnostics visibility.
- `daw/src/app/main.c` now routes invalidation by event class:
  - pointer events (`MOUSEMOTION`, button, wheel): invalidate hit/hover panes only.
  - global/layout events (`WINDOWEVENT`, keyboard/text/drop): retain full invalidation fanout for correctness.
- Loop diagnostics now include `dirty_panes=<n>` for each report interval.
- `daw/src/app/README.md` updated with the new pointer-vs-global invalidation behavior.
- `make -C daw` passes after Phase 5 changes.
- Smoke run completed; this environment still fails before interactive loop due backend limits (`SDL_Init failed: The video driver did not add any displays`, CoreAudio device unavailable), so interactive dirty-pane behavior validation must be confirmed on a display/audio-capable machine.

## Acceptance Criteria
Phase 5 is complete when:
1. P5.1-P5.9 are complete.
2. DAW builds successfully.
3. Pointer input no longer blindly invalidates all panes.
4. Existing interaction behavior remains intact.

## Risks and Mitigations
- Risk: stale visuals if targeted invalidation misses a dependent pane.
  - Mitigation: keep conservative full invalidation for non-pointer/global events.
- Risk: hit-testing mismatch with overlay clipping.
  - Mitigation: fallback full invalidation for ambiguous/global paths.
