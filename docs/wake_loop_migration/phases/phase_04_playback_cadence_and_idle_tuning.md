# Phase 4 - Playback Cadence and Idle Tuning (Deep Implementation Plan)

## Phase Objective
Tune the wake-blocked loop so playback and active transport rendering run on a timed cadence instead of busy spinning, while preserving smooth UI updates and low idle CPU when transport is stopped.

## Why This Phase Exists
Phase 3 introduced wake-blocked execution and invalidation-driven renders, but playback is still flagged as immediate work. That prevents blocking between frame deadlines and can keep CPU usage higher than needed during active transport.

## Scope
In scope:
- Convert playback from "always immediate" to cadence-driven periodic work.
- Ensure update callbacks run at render cadence when required.
- Add wait-time computation that includes next render deadline.
- Keep responsiveness for true immediate work (input, invalidation, due timers/jobs/messages, bounce, swapchain events).
- Add diagnostics fields for cadence behavior.

Out of scope:
- Per-pane partial draw caching (Phase 5).
- Worker-driven async graph changes (later phase).
- Audio engine architectural changes.

## Shared-Core Reuse Decision
Adopt and continue using:
- `core_time`
- `core_sched`
- `core_jobs`
- `core_queue`
- `core_wake`
- `core_kernel`

No new shared-core modules added.

## Runtime Contract (Phase 4 Target)
Per loop cycle:
1. Drain input/internal wake events.
2. Run background runtime slice (timers/jobs/messages/kernel).
3. Compute whether render cadence deadline is due.
4. Run update when:
   - immediate work exists, or
   - render deadline is due (playback cadence tick).
5. Render when invalidated, playback cadence due, or heartbeat elapsed.
6. If no immediate work and render not due, block until nearest timer/render/heartbeat deadline.

## Key Design

## 1) Distinguish Urgent Work vs Periodic Playback Cadence
- Keep immediate predicate strict:
  - include: swapchain recreate, invalidation, due timers/jobs/messages, bounce
  - exclude: transport playing by itself
- Let playback cadence be handled by render deadline logic and timeout computation.

## 2) Update-on-Render-Due Guarantee
- In `SDLApp` loop:
  - compute render due first
  - run update when either immediate work exists or render is due
- This keeps playhead/transport state updates aligned with frame cadence.

## 3) Wait Timeout Includes Render Deadline
- In DAW wait-time callback:
  - compute remaining ms until next throttled render (`renderThreshold - timeSinceLastRender`)
  - clamp with existing max wait and timer deadlines
- Result: loop blocks between frames during playback instead of spinning.

## 4) Diagnostics Extension
- Extend loop diagnostics logs with cadence-oriented context:
  - estimated cadence wait contribution
  - active% trends during playback
- Keep knobs env-based (`DAW_LOOP_*`) and default-safe.

## File Plan
Primary files:
- `daw/SDLApp/sdl_app_framework.c`
- `daw/src/app/main.c`

Doc updates:
- `daw/docs/wake_loop_migration/phases/phase_04_playback_cadence_and_idle_tuning.md`
- `daw/src/app/README.md` (small loop behavior note)

## Execution Checklist
- [x] P4.1 Add Phase 4 doc with scope, design, and checklist.
- [x] P4.2 Adjust immediate-work predicate to remove playback-only busy behavior.
- [x] P4.3 Update loop runner to execute update on render-due cadence ticks.
- [x] P4.4 Extend wait-time computation with next render deadline timing.
- [x] P4.5 Keep render gate smooth for active transport with throttled cadence.
- [x] P4.6 Add/adjust diagnostics fields or log output for cadence visibility.
- [x] P4.7 Build passes (`make -C daw`).
- [x] P4.8 Smoke validation run completed and notes captured.
- [x] P4.9 Mark Phase 4 checklist complete with execution notes.

Execution notes:
- `daw/src/app/main.c`
  - `daw_loop_has_immediate_work` now excludes transport-playing as urgent work, so playback no longer forces continuous non-blocking cycles.
  - `daw_loop_compute_wait_timeout_ms` now includes render cadence deadline (`renderThreshold - timeSinceLastRender`) when transport is playing, clamped against `DAW_LOOP_MAX_WAIT_MS` and timer deadlines.
  - loop diagnostics now include `render_wait_ms` to make cadence-driven sleeps visible.
- `daw/SDLApp/sdl_app_framework.c`
  - main loop now computes render-due state before update dispatch and runs update when either urgent work exists or render is due.
  - render gate is re-evaluated post-update so each cadence tick can update + render coherently.
- `daw/src/app/README.md` updated to reflect urgency-vs-cadence loop callback responsibilities.
- `make -C daw` passes after Phase 4 changes.
- Smoke run completed; this environment still exits before a full interactive loop due backend limits (`SDL_Init failed: The video driver did not add any displays`, CoreAudio device unavailable), so on-device playback cadence metrics must be validated on a machine with active display/audio devices.

## Acceptance Criteria
Phase 4 is complete when:
1. P4.1-P4.9 are complete.
2. DAW still builds and launches in current environment.
3. Playback loop no longer relies on busy spin between frame deadlines.
4. Idle transport behavior remains wake-blocked and responsive.

## Risks and Mitigations
- Risk: under-updating playhead during playback.
  - Mitigation: update-on-render-due guarantee.
- Risk: increased input latency.
  - Mitigation: immediate work still bypasses waits.
- Risk: cadence jitter due to timeout rounding.
  - Mitigation: use capped deadlines and 1ms floor where needed.
