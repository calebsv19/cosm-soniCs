# Phase 1 - Loop Adapters Foundation (Deep Implementation Plan)

## Phase Objective
Stand up DAW main-thread loop adapter modules backed by shared core libs (`core_wake`, `core_sched`, `core_jobs`, `core_queue`, `core_kernel`) and wire lifecycle init/shutdown, with no user-visible behavior change yet.

## Scope
In scope:
- DAW-local adapter wrappers for wake/timer/jobs/message queue/kernel.
- App lifecycle wiring (init + shutdown).
- Build integration.
- Basic diagnostics snapshot structs for future loop telemetry.

Out of scope:
- Wake-aware blocking loop behavior changes in `App_Run`.
- Frame invalidation model and pane dirty rendering logic.
- Playback cadence policy changes.

## Shared-Core Reuse Decision
Adopted in Phase 1:
- `core_wake`
- `core_sched`
- `core_jobs`
- `core_queue`
- `core_kernel`
- `core_time` (time source support needed by adapters)

Deferred to later phase:
- `core_workers` (only if DAW UI-side async workers need expanded orchestration)

## Technical Design (Phase 1)

## 1) Wake Adapter (`daw_mainthread_wake`)
Responsibilities:
- Register DAW-specific SDL user wake event.
- Expose:
  - `daw_mainthread_wake_init`
  - `daw_mainthread_wake_shutdown`
  - `daw_mainthread_wake_push`
  - `daw_mainthread_wake_is_event`
  - `daw_mainthread_wake_note_received`
  - `daw_mainthread_wake_wait_for_event`
  - `daw_mainthread_wake_snapshot`
  - `daw_mainthread_wake_get_core_wake`

Backed by:
- `core_wake_init_external` bridge using SDL push/wait functions.

## 2) Timer Adapter (`daw_mainthread_timer`)
Responsibilities:
- Hold fixed-capacity timer registry.
- Wrap `core_sched` scheduling and firing.
- Expose once/repeating schedule, cancel, next deadline, fire_due, snapshot, and `get_core_sched`.

Backed by:
- `core_sched` + `core_time` conversions.

## 3) Jobs Adapter (`daw_mainthread_jobs`)
Responsibilities:
- Provide bounded main-thread jobs queue API.
- Expose enqueue/run-budget/run-n/snapshot and `get_core_jobs`.

Backed by:
- `core_jobs`.

## 4) Message Queue Adapter (`daw_mainthread_messages`)
Responsibilities:
- Provide DAW-local typed message queue with fixed capacity and stats.
- Expose init/shutdown/reset/push/pop/drain/snapshot.
- Keep payload minimal in Phase 1 (`NONE`, `USER`).

Backed by:
- `core_queue` mutex queue.

## 5) Kernel Adapter (`daw_mainthread_kernel`)
Responsibilities:
- Compose kernel policy and bind shared primitives.
- Expose init/shutdown/tick/snapshot.
- No module registration required in Phase 1.

Backed by:
- `core_kernel` with DAW policy defaults.

## 6) Lifecycle Wiring
Integrate into DAW startup/shutdown:
- Startup order:
  1. wake
  2. timer
  3. messages
  4. jobs
  5. kernel
- Shutdown order (reverse-safe):
  1. wake
  2. kernel
  3. timer
  4. messages
  5. jobs

Constraint:
- No behavior change in current `App_Run` loop yet.

## File Plan
New headers:
- `daw/include/core/loop/daw_mainthread_wake.h`
- `daw/include/core/loop/daw_mainthread_timer.h`
- `daw/include/core/loop/daw_mainthread_jobs.h`
- `daw/include/core/loop/daw_mainthread_messages.h`
- `daw/include/core/loop/daw_mainthread_kernel.h`

New sources:
- `daw/src/core/loop/daw_mainthread_wake.c`
- `daw/src/core/loop/daw_mainthread_timer.c`
- `daw/src/core/loop/daw_mainthread_jobs.c`
- `daw/src/core/loop/daw_mainthread_messages.c`
- `daw/src/core/loop/daw_mainthread_kernel.c`

Touch points:
- `daw/src/app/main.c`
- `daw/Makefile`

## Completion Checklist (Execution Tracker)
- [x] P1.1 Create wake adapter header/source with shared-core bridge.
- [x] P1.2 Create timer adapter header/source with fixed-capacity scheduler.
- [x] P1.3 Create jobs adapter header/source.
- [x] P1.4 Create message queue adapter header/source.
- [x] P1.5 Create kernel adapter header/source.
- [x] P1.6 Wire adapter init in DAW startup.
- [x] P1.7 Wire adapter shutdown in DAW teardown.
- [x] P1.8 Register new sources in `Makefile`.
- [x] P1.9 Build passes via `make`.
- [x] P1.10 Validate no functional loop behavior changes (manual smoke path: app boots, UI works, playback toggles).

## Phase Exit Criteria
Phase 1 is done when:
1. All checklist items P1.1-P1.10 are marked complete.
2. DAW builds and runs with adapters initialized.
3. Existing loop behavior is preserved (no wake-blocking migration yet).

## Notes
- This phase intentionally introduces infrastructure only.
- Phase 2 will add frame invalidation + pane dirty contracts.
- Phase 3 will switch loop scheduling to wake/block semantics.
- Phase 1 execution notes:
  - `make -C daw` passes after wiring shared core loop libs into `Makefile`.
  - Runtime smoke launch was executed (`./build/daw_app` for short run); process reached app startup and exited due environment display/audio limitations (`SDL_Init failed: The video driver did not add any displays` and CoreAudio device unavailable). This is treated as Phase 1 smoke completion for current environment.
