# Phase 3 - Wake-Blocked Loop Execution (Deep Implementation Plan)

## Phase Objective
Replace DAW’s continuous busy update loop with a wake-aware blocked loop that only wakes for immediate work, while preserving responsiveness and avoiding behavior regressions.

## Why This Phase Exists
After Phase 1 and Phase 2:
- Runtime adapters exist (`wake/timer/jobs/messages/kernel`).
- Invalidation + pane dirty contracts exist.

But DAW still updates continuously in `App_Run` and only throttles render cadence. This phase migrates loop control so idle state sleeps instead of spinning.

## Scope
In scope:
- Introduce work-gated wake/wait behavior in `SDLApp` loop runner.
- Add computed wait timeout policy.
- Integrate timer/jobs/messages/kernel ticks into loop cycle.
- Render gate uses invalidation contract and heartbeat policy.

Out of scope:
- Playback-specific render cadence tuning (Phase 4).
- Fine-grained pane redraw optimization (Phase 5).
- Broad async producer refactors (Phase 6).

## Shared-Core Reuse Decision
Adopt and use directly:
- `core_wake` via `daw_mainthread_wake`
- `core_sched` via `daw_mainthread_timer`
- `core_jobs` via `daw_mainthread_jobs`
- `core_queue` via `daw_mainthread_messages`
- `core_kernel` via `daw_mainthread_kernel`
- `core_time`

Deferred:
- `core_workers` for later async expansion.

## Runtime Contract (Phase 3 Target)
Per loop cycle:
1. Drain immediate SDL events.
2. Run due timers/jobs/messages/kernel tick.
3. Run app update callback only when policy says update work is due.
4. Render only when invalidated or heartbeat due.
5. If no immediate work remains, block on wake/event until timeout/deadline.

## Key Design

## 1) App Framework Policy Surface
Extend `AppContext` with loop policy controls:
- heartbeat interval (idle refresh fallback)
- max wait clamp
- loop diagnostics toggle

Add (or extend) APIs in `sdl_app_framework`:
- set wake-aware loop policy values
- optional diagnostics snapshot for active vs blocked time

## 2) Immediate Work Predicate
Implement `app_has_immediate_work(ctx)` using:
- pending SDL events (or just-drained event state)
- pending swapchain recreation
- pending invalidation (`daw_has_frame_invalidation()`)
- non-empty DAW message queue
- due timer deadline now/overdue
- app-specific active interaction hints (Phase 3 minimal: rely on invalidation + events)

## 3) Wait Timeout Computation
Implement `app_compute_wait_timeout_ms(ctx)`:
Inputs:
- next timer deadline (`daw_mainthread_timer_scheduler_next_deadline_ms`)
- heartbeat deadline
- max wait clamp env/config

Rules:
- If invalidated or active event stream: short timeout (0-16 ms)
- Else prefer nearest timer/heartbeat deadline
- Hard cap with max wait value

## 4) Wake Event Handling
In event drain path:
- If event is wake event (`daw_mainthread_wake_is_event`), record received and skip normal input routing.
- For non-wake events, call existing input callback path.

## 5) Background Tick Slice
Each frame iteration should run:
- `daw_mainthread_timer_scheduler_fire_due(...)`
- `daw_mainthread_jobs_run_budget_ms(...)`
- drain some DAW messages with fixed budget
- `daw_mainthread_kernel_tick(core_time_now_ns())`

Keep budgets conservative in Phase 3 and configurable via env later.

## 6) Render Gate
Render should occur when either:
- `daw_has_frame_invalidation()` is true, or
- heartbeat elapsed (to keep UI alive even if missed invalidation edge)

No render if minimized/invalid swapchain (existing guard remains).

## 7) Blocked Wait
When no immediate work and no render due:
- call `daw_mainthread_wake_wait_for_event(timeout_ms, &event)`
- if signaled with event, process it immediately (same handling path)
- measure blocked duration for diagnostics

## 8) Diagnostics
Add optional env-driven diagnostics:
- `DAW_LOOP_DIAG_LOG=1`
- `DAW_LOOP_MAX_WAIT_MS=<n>`

Counters per 1s period:
- loop ticks
- waits called
- blocked ms
- active ms
- renders
- wake events received
- timers fired
- message queue depth peak

## File Plan
Primary files:
- `daw/SDLApp/sdl_app_framework.h`
- `daw/SDLApp/sdl_app_framework.c`
- `daw/src/app/main.c`

Likely touch:
- `daw/include/core/loop/daw_mainthread_messages.h` (if queue size/helper needed)
- `daw/src/core/loop/daw_mainthread_messages.c`
- `daw/src/core/loop/daw_mainthread_timer.c` (only if helper needed)

No planned changes to shared `shared/core/*` modules.

## Execution Checklist
- [x] P3.1 Define wake-aware loop policy fields/APIs in `sdl_app_framework`.
- [x] P3.2 Implement immediate-work predicate and wait-timeout computation.
- [x] P3.3 Integrate wake-event filtering/handling into SDL event path.
- [x] P3.4 Integrate timer/jobs/messages/kernel tick slice per loop cycle.
- [x] P3.5 Gate render by invalidation + heartbeat policy.
- [x] P3.6 Add blocked wait path using DAW wake bridge when no immediate work.
- [x] P3.7 Add optional loop diagnostics and env controls.
- [x] P3.8 Wire any required policy initialization in `main.c`.
- [x] P3.9 Build passes (`make -C daw`).
- [x] P3.10 Smoke validation run completed and notes captured.

Execution notes:
- `daw/SDLApp/sdl_app_framework.{h,c}` now exposes wake-loop policy and diagnostics APIs, plus callback hooks for background ticks, immediate-work checks, timeout selection, internal-event filtering, render gating, diagnostics, and wake bridge event waits.
- `daw/src/app/main.c` now wires DAW runtime callbacks for:
  - timer/jobs/messages/kernel background slice each loop iteration
  - immediate-work predicate using invalidation, transport/bounce state, timers, jobs, queue depth, and swapchain flags
  - wake-event filtering (`daw_mainthread_wake_is_event`) and wake-note accounting
  - render gate based on invalidation, transport-active cadence, and heartbeat fallback
  - timeout computation using next timer deadline and `DAW_LOOP_MAX_WAIT_MS`
  - diagnostics logging with `DAW_LOOP_DIAG_LOG=1`
- `daw/include/core/loop/daw_mainthread_messages.h` and `daw/src/core/loop/daw_mainthread_messages.c` now provide `daw_mainthread_message_queue_has_pending()` for low-cost immediate-work checks.
- `make -C daw` passes after Phase 3 changes.
- Smoke run completed; this environment still fails pre-loop window/audio startup (`SDL_Init failed: The video driver did not add any displays`, CoreAudio device unavailable), so runtime blocked-loop metrics could not be captured here.

## Acceptance Criteria
Phase 3 is complete when:
1. P3.1-P3.10 all completed.
2. DAW builds and launches in current environment.
3. Loop can block when idle (verified via diagnostics: non-trivial blocked ms and reduced active spin).
4. Existing user flows (input handling/render path startup) are functionally intact.

## Risks and Mitigations
- Risk: missed wakes causing stale UI.
  - Mitigation: heartbeat fallback + invalidation checks before wait.
- Risk: too-long waits hurting interaction latency.
  - Mitigation: short timeout when invalidated/event-active; capped max wait.
- Risk: timer/job starvation.
  - Mitigation: run due/background slice before blocking and after wake.

## Notes for Post-Compact Continuation
Start implementation in this order:
1. `sdl_app_framework` policy + loop skeleton changes.
2. wake event handling and wait timeout logic.
3. background tick integration.
4. diagnostics + tuning knobs.
5. build and smoke validation.

This preserves momentum and minimizes loop-regression risk.
