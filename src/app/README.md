# Directory: src/app

Purpose: Application bootstrap and SDL event loop integration.

## Files
- `main.c`
  - `handle_input`: Feeds the current SDL event into the input manager when available.
  - `handle_update`: Ensures layout sizing is up to date and advances input state when work is due.
  - `handle_render`: Clears the renderer, draws panes/controls/overlays, and presents the frame.
  - Wake-loop callbacks: Defines urgent-work checks, render-cadence timeout selection, wake-event filtering, background tick, render-gate, and diagnostics hooks consumed by `SDLApp` wake-blocked loop execution.
  - Input invalidation routing: Pointer events now invalidate targeted panes via pane hit-testing; global/layout events still invalidate all panes for correctness.
  - Async producer bridge: Worker-thread producers post typed main-thread messages (`daw_mainthread_message_post`) that coalesce wake signaling and drive targeted UI invalidation in the background tick.
  - Gate diagnostics mode: `DAW_LOOP_GATE_EVAL=1` with `DAW_SCENARIO=idle|playback|interaction` emits pass/fail threshold checks from loop diagnostics windows.
  - Loop diagnostics JSON mode: `DAW_LOOP_DIAG_FORMAT=json` (or `DAW_LOOP_DIAG_JSON=1`) emits schema-1 `LoopDiag` lines for cross-program sleep/wake calibration parity.
  - Gate harness: run `daw/tools/run_loop_gates.sh` to execute all gate scenarios and emit a summarized `pass/fail/inconclusive` report with scenario logs.
    - headless validation: set `HEADLESS=1` for no-display/no-swapchain loop gate checks.
  - Quick commands:
    - `make -C daw loop-gates` (default profile, inconclusive => exit 2)
    - `make -C daw loop-gates-strict` (strict profile, inconclusive => failure exit)
  - `main`: Loads config, restores the last session from `config/last_session.json` (or seeds defaults), initialises UI subsystems, configures wake-loop policy, runs the SDL framework loop, and auto-saves the session on shutdown.
