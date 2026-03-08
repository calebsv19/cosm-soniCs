# mem_console

`mem_console` is the planned standalone Memory Console host for the Memory DB system.

This is a showcase app, not a reusable shared kit.

Its job is to become the first interactive proving ground for:

- `core_memdb`
- `kit_render`
- `kit_ui`
- `kit_graph_struct` (active in current Phase 4 graph-mode rollout)

## Current State

The current app is a windowed Phase 3 console with the follow-on theme/font refinement lane implemented.

Right now it proves:
- the intended build and link shape is in place
- the host opens a target Memory DB through `core_memdb`
- the app runs a real SDL + Vulkan + `kit_render` frame loop
- the first split-pane shell renders with `kit_ui`
- the console now uses the shared additive `kit_ui` text-tier hooks for clearer hierarchy
- the console now uses `kit_ui` theme-scale style sync so control spacing/density follows the active shared theme preset
- the console now supports the standard shared live theme-cycle shortcut (`Cmd/Ctrl+Shift+T` forward, `Cmd/Ctrl+Shift+Y` backward)
- the left pane now supports app-owned typed search and visible filtered result rows
- the left pane now includes project-scope quick-filter chips (`ALL PROJECTS` + multi-select project toggles) for scoped browsing
- the result list is synchronously queried through `core_memdb` and supports click-to-select
- the result list now uses scalable query windowing (`LIMIT` + `OFFSET`) so large match sets can be traversed through scrolling
- the list scroll range now supports top-anchoring final rows (last row can be scrolled to top of viewport)
- result rows now include project tags in labels when project metadata is present
- the right pane now exposes create + explicit title/body edit flows
- title/body editing now runs through dedicated edit modes with explicit save/cancel actions (search text remains filter-oriented)
- active text input now supports cursor movement/edit keys and paste, with debounced search refresh
- right-pane actions are now compacted into a horizontal control bar above the graph area so graph/list remain primary visual surfaces
- the detail pane now exposes `pinned` and `canonical` toggle actions for the selected memory
- the detail pane now renders a DB-backed one-hop graph preview for the selected memory
- graph node click now selects that memory and refreshes the detail pane
- graph mode now has explicit `GRAPH MODE` toggle and `REFRESH GRAPH` controls
- graph mode now includes a bounded edge-kind filter segmented control (`ALL`, `SUPPORTS`, `DEPENDS`, `REFS`, `SUMMARY`, `RELATED`)
- detail pane now shows compact selected-node connection summaries in the top metadata row (right side of title/id area)
- graph edges now render compact kind labels so connection semantics are visible in preview
- graph edges touching selected node now use subtle directional tinting (outbound=green-tinted white, inbound=red-tinted white, bidirectional=white)
- graph preview layout is now cached by graph-signature and viewport bounds so draw/click paths reuse one computed layout instead of recomputing twice
- graph preview now draws routed orthogonal edge polylines from `kit_graph_struct` route helpers (boundary-attached endpoints)
- graph edge labels now use route-aware placement with bounded density controls (auto overlap-cull on denser neighborhoods and zoom-threshold hide policy)
- graph edge/label hit navigation now uses shared `kit_graph_struct` helpers for deterministic edge-click selection
- graph viewport now supports wheel zoom + drag pan with drag-release click suppression to avoid accidental node selection while panning
- graph click selection now requires click-start inside graph viewport for deterministic node/edge pick behavior
- the UI can synchronously reload a DB summary through the existing shared DB boundary
- runtime theme and font preset cycling are live and visible in-app
- current panel/background/row colors are resolved through shared theme tokens
- text rendering runs through the shared TTF-first `kit_render` path with fallback retained
- periodic async DB refresh is now wired through shared runtime libs (`core_workers` + `core_queue` + `core_wake`) with main-thread apply safety guards
- idle-loop pacing now uses timed waits to reduce churn when no input/work is pending
- refresh requests now coalesce while a worker refresh is in-flight, keeping latest intent
- refresh intent matching/coalescing now includes selected project-filter sets (not only search/selection/offset)
- runtime observability counters are now surfaced in the left pane for async submitted/applied/dropped/error/coalesced status
- optional kernel-bridge evaluation mode is available (`--kernel-bridge`) and surfaces compact kernel telemetry in the left pane
- theme/font preset selection now persists through an app-local `.pack` prefs file (`<db_path>.ui.pack`) using `core_pack`

It does not yet provide a full multiline editor widget or richer styling controls.

Current source layout:
- `src/mem_console.c`:
  - app lifecycle + SDL event loop + action dispatch
- `src/mem_console_types.h`:
  - shared state/types/constants
- `src/mem_console_state.h/.c`:
  - argument parsing, theme/font cycles, layout/search/text helpers
- `src/mem_console_db.h/.c`:
  - DB reads/writes and graph neighborhood load
- `src/mem_console_runtime.h/.c`:
  - background refresh scheduling + worker completion handoff
- `src/mem_console_ui.h/.c`:
  - frame rendering, controls, graph preview draw/hit-test

## Build

```sh
make -C shared/showcase/mem_console
```

Quick run:

```sh
make -C shared/showcase/mem_console run
```

`run` launches from the repo root so shared asset-relative paths (fonts, demo assets) resolve consistently.

Run with explicit demo DB:

```sh
make -C shared/showcase/mem_console run-demo
```

Pass extra args:

```sh
make -C shared/showcase/mem_console run RUN_ARGS="--db /tmp/mem_console_phase7.sqlite"
make -C shared/showcase/mem_console run RUN_ARGS="--db /tmp/mem_console_phase7.sqlite --kernel-bridge"
```

## Run

```sh
./shared/showcase/mem_console/build/mem_console
./shared/showcase/mem_console/build/mem_console --db /path/to/memory.sqlite
./shared/showcase/mem_console/build/mem_console --db /path/to/memory.sqlite --kernel-bridge
```

Default behavior:
- running without `--db` resolves in this order:
  1. `CODEWORK_MEMDB_PATH` (if set)
  2. `~/Desktop/CodeWork/data/codework_mem_console.sqlite` (if `~/Desktop/CodeWork/data` exists)
  3. fallback `shared/showcase/mem_console/demo/demo_mem_console.sqlite`
- UI preset prefs are read from/written to `<db_path>.ui.pack` for whichever DB path is active

Local workspace behavior:
- `make -C shared/showcase/mem_console run` and `run-demo` prefer `~/Desktop/CodeWork/data/codework_mem_console.sqlite` when `~/Desktop/CodeWork/data` exists
- demo helper scripts (`reset_demo_db.sh`, `seed_large_list.sh`) also prefer the same Data-path DB when available
- `CODEWORK_MEMDB_PATH` can override all script defaults explicitly

Runtime shortcuts:
- `Cmd/Ctrl+Shift+T`: cycle theme preset forward
- `Cmd/Ctrl+Shift+Y`: cycle theme preset backward
- `Cmd/Ctrl+Shift+U`: cycle font preset forward
- `Cmd/Ctrl+Shift+I`: cycle font preset backward

Large-list scroll audit helpers:
- `shared/showcase/mem_console/demo/seed_large_list.sh`
- `shared/showcase/mem_console/demo/LARGE_LIST_AUDIT.md`

Demo reset helper:
- `shared/showcase/mem_console/demo/reset_demo_db.sh`
  - now seeds a connected memory/link dataset for immediate graph validation

## Near-Term Target

The next implementation steps are:
- add richer link editing affordances on top of the current graph mode controls
- finish graph-mode audit closure and Codex skill packaging validation
