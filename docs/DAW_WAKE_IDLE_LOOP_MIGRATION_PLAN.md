# DAW Wake/Idle Loop Migration Plan (IDE-Parity)

## Objective
Move DAW to the same wake-driven runtime model as IDE so the UI thread mostly sleeps when idle and only wakes for:
- user input,
- due timers/background jobs,
- explicit wake signals from async/background completion,
- bounded heartbeat refreshes.

Target behavior:
- Idle (no input, not playing, no background work): main-thread CPU ~2-5%.
- Active editing/playback: responsive updates and render pacing without regressions.

## Current State Snapshot

### IDE (reference model already in use)
- Wake/event blocking exists (`mainthread_wake_wait_for_event` + timeout).
- Frame invalidation gates rendering (`hasFrameInvalidation`, `requestFullRedraw`, `consumeFrameInvalidation`).
- Pane-level dirty flags + invalidation reasons exist (`paneMarkDirty`, `paneClearDirty`).
- Loop timers/jobs/messages/kernel wrappers are wired:
  - `mainthread_timer_scheduler_*`
  - `mainthread_jobs_*`
  - `mainthread_message_queue_*`
  - `mainthread_kernel_*`
- Runtime computes wait timeout from active interaction, invalidation, timer deadlines, and heartbeat.

### DAW (current gap)
- `App_Run` polls events and calls `handleUpdate` every loop iteration (busy-spin risk).
- Render is only throttled by `renderThreshold`, but update still runs continuously.
- No global frame invalidation contract.
- `Pane` has no dirty/invalidation metadata.
- No wake/timer/jobs/message queue integration in DAW main loop.
- UI state updates (hover/follow/transport sync/etc.) currently run each update tick.

## Shared-Core Reuse Strategy
Adopt shared core libraries directly for DAW loop runtime:
- `core_wake`
- `core_sched`
- `core_jobs`
- `core_queue`
- `core_kernel`
- `core_time`
- optional later: `core_workers` for DAW UI-adjacent background tasks

Do not copy IDE app-specific code. Port the architecture and contracts, then implement DAW-specific adapters.

## North-Star Runtime Contract (DAW)
1. Input events invalidate only affected UI regions/panes.
2. Background completions enqueue main-thread messages and wake UI thread.
3. Render happens when:
   - there is frame invalidation, or
   - heartbeat interval elapsed (for minimal liveness), or
   - transport/playback policy requires periodic visual updates.
4. If no immediate work, main loop blocks on wake/event with deadline timeout.
5. After successful render, dirty/invalidation state is consumed/cleared.

## Phased Execution Plan

## Phase 0 - Instrumentation Baseline
Goal: quantify current idle/active behavior before changing loop behavior.

Deliverables:
- DAW loop diagnostics counters:
  - frames, renders, loop iterations,
  - blocked ms vs active ms,
  - wake count, timer-fired count,
  - invalidation-trigger count.
- Env toggles for diagnostics (`DAW_LOOP_DIAG_LOG`, `DAW_LOOP_MAX_WAIT_MS`).
- Baseline capture notes (idle, playback idle UI, drag-resize, heavy editing).

Exit criteria:
- 60s idle trace and 60s playback trace collected and documented.

## Phase 1 - Introduce DAW Loop Runtime Adapters (No Behavior Change Yet)
Goal: wire reusable loop primitives without changing external behavior yet.

Deliverables:
- New DAW runtime loop adapters (module naming can vary):
  - wake bridge (SDL event registration + signal/wait bridge)
  - timer scheduler wrapper (backed by `core_sched`)
  - jobs wrapper (backed by `core_jobs`)
  - message queue wrapper (backed by `core_queue`)
  - kernel wrapper (backed by `core_kernel`)
- Init/shutdown wiring in DAW boot/shutdown.
- Compile/run parity with current behavior.

Exit criteria:
- DAW runs unchanged functionally with adapters initialized and torn down cleanly.

## Phase 2 - Frame Invalidation + Pane Dirty Model
Goal: create DAW equivalents of IDE invalidation and pane-dirty semantics.

Deliverables:
- Add pane dirty state fields in DAW `Pane`:
  - `dirty`, `dirty_reasons`, `last_render_frame_id` (minimum)
  - optional for later: dirty rect/cache metadata.
- Add DAW invalidation API:
  - `daw_invalidate_pane(...)`
  - `daw_invalidate_all(...)`
  - `daw_request_full_redraw(...)`
  - `daw_has_frame_invalidation()`
  - `daw_consume_frame_invalidation(...)`
- Map DAW input/window events to invalidation reasons.

Exit criteria:
- Render gate can rely on invalidation state instead of unconditional cadence alone.

## Phase 3 - Replace Busy Loop with Wake-Aware Loop
Goal: stop continuous update spinning and block when idle.

Deliverables:
- Refactor `App_Run` to:
  - process pending SDL events,
  - execute due timers/jobs/messages,
  - run bounded update work,
  - render only when required by invalidation/heartbeat/playback policy,
  - otherwise block using wake/event wait with computed timeout.
- Wait timeout policy includes:
  - active interaction (dragging/editing),
  - frame invalidation,
  - next timer deadline,
  - fallback heartbeat deadline,
  - optional debug max-wait clamp.

Exit criteria:
- Idle mode is predominantly blocked wait (low active-loop time).

## Phase 4 - Playback-Aware Render Policy
Goal: keep timeline/playhead/meter UX responsive without reverting to busy-spin.

Deliverables:
- Define render policy states:
  - `Idle`: invalidation + low heartbeat (e.g., 250-500 ms)
  - `Playing`: fixed visualization cadence (e.g., 30/60 Hz configurable)
  - `Dragging/Resizing/Text edit`: near-frame cadence with low timeout.
- Ensure transport changes and playback status transitions trigger wake/invalidation.
- Tie meter/spectrum refresh paths to timer/message-based invalidation rather than full-loop polling.

Exit criteria:
- Smooth playback visuals while idle CPU remains low when stopped.

## Phase 5 - Smart Pane Repaint Scope (Incremental)
Goal: reduce redraw work to changed panes/regions.

Deliverables:
- Per-pane invalidation wiring in DAW UI modules:
  - transport pane,
  - timeline pane,
  - inspector/effects pane,
  - library pane,
  - modal overlays.
- Clear pane dirty flags post-render commit.
- Optional: add simple pane cache/version metadata if needed.

Exit criteria:
- Input affecting one pane no longer forces whole-UI redraw by default.

## Phase 6 - Async Work + Wake Hygiene
Goal: guarantee any background completion wakes UI thread exactly when needed.

Deliverables:
- Standardize UI-thread message queue posts for async producers.
- On queue push, signal wake bridge.
- Main thread drains queue with budget and applies state changes + invalidation.
- Add throttling/coalescing for high-frequency status/progress messages.

Exit criteria:
- No stale UI after async completion; no wake storms from noisy producers.

## Phase 7 - Validation, Tuning, and Rollout Guardrails
Goal: lock in performance and correctness before defaulting the new loop.

Deliverables:
- Test matrix:
  - idle stopped,
  - playback active,
  - drag-resize stress,
  - timeline heavy edit,
  - bounce/export flow,
  - minimize/restore and resize events.
- Regression checks:
  - input latency,
  - transport correctness,
  - no dropped essential renders,
  - no hangs on shutdown.
- Runtime toggles:
  - fallback to legacy loop during hardening window.

Exit criteria:
- New loop default-on with measurable idle CPU reduction and no major UX regressions.

## File-Level Initial Work Map

Primary DAW files expected to change first:
- `daw/SDLApp/sdl_app_framework.c`
- `daw/SDLApp/sdl_app_framework.h`
- `daw/src/app/main.c`
- `daw/include/ui/panes.h`
- `daw/src/ui/panes.c`
- `daw/src/ui/layout.c`
- `daw/src/input/input_manager.c`

New DAW runtime loop adapter files (proposed):
- `daw/src/core/loop/daw_mainthread_wake.[ch]`
- `daw/src/core/loop/daw_mainthread_timer.[ch]`
- `daw/src/core/loop/daw_mainthread_jobs.[ch]`
- `daw/src/core/loop/daw_mainthread_messages.[ch]`
- `daw/src/core/loop/daw_mainthread_kernel.[ch]`
- `daw/src/core/loop/daw_render_invalidation.[ch]`

## Risks and Controls
- Risk: over-throttled rendering causes stale playback visuals.
  - Control: playback-specific cadence policy + forced invalidation on transport progress.
- Risk: missed invalidation leads to frozen pane visuals.
  - Control: per-pane invalidation audit + heartbeat fallback.
- Risk: wake storms from frequent events/messages.
  - Control: coalescing and min-interval throttles for noisy channels.
- Risk: shutdown race with queued async messages.
  - Control: ordered shutdown (stop producers -> drain -> destroy loop primitives).

## Success Metrics
- Idle stopped session CPU: sustained ~2-5% on target hardware.
- No input-to-render latency regression for drag/edit interactions.
- Playback visual smoothness preserved.
- Reduced unnecessary render count and update-loop active time in diagnostics.

## Implementation Order Recommendation
1. Phase 0
2. Phase 1
3. Phase 2
4. Phase 3
5. Phase 4
6. Phase 5
7. Phase 6
8. Phase 7

This order keeps risk low: first wire runtime primitives, then add invalidation semantics, then change loop scheduling.
