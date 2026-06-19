# Directory: src/core/loop

Purpose: DAW-owned main-thread runtime adapters around shared core loop
primitives.

## Ownership

These modules intentionally own process-local singleton adapter state for the
current SDL app lifetime. The singleton storage is local to each adapter file
and is reached through explicit init, shutdown, reset, work, and snapshot APIs.

The adapters are not persisted project/session state and are not renderer-owned
derived state. `daw/src/app/main.c` owns lifecycle ordering and calls these
adapters from the wake-blocked runtime loop.

## Adapters

- `daw_mainthread_wake.*`: SDL user-event wake bridge backed by `core_wake`.
- `daw_mainthread_timer.*`: main-thread timer scheduler backed by `core_sched`.
- `daw_mainthread_jobs.*`: bounded main-thread job queue backed by `core_jobs`.
- `daw_mainthread_messages.*`: typed producer-to-main-thread queue backed by
  `core_queue` plus coalesced wake signaling.
- `daw_mainthread_kernel.*`: shared runtime kernel adapter backed by
  `core_kernel`.
- `daw_render_invalidation.*`: frame invalidation owner for pane dirty flags
  and full-redraw requests.

## Split Boundary

Do not split these adapters into `AppState` fields just to remove file-local
singleton storage. A context split is only justified if DAW needs multiple
independent runtime loop instances in one process, stronger test injection than
the current init/reset/snapshot APIs provide, or a non-SDL host with different
wake ownership.
