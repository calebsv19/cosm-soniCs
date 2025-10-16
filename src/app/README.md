# Directory: src/app

Purpose: Application bootstrap and SDL event loop integration.

## Files
- `main.c`
  - `handle_input`: Feeds the current SDL event into the input manager when available.
  - `handle_update`: Ensures layout sizing is up to date and advances input state each frame.
  - `handle_render`: Clears the renderer, draws panes/controls/overlays, and presents the frame.
  - `main`: Loads config, restores the last session from `config/last_session.json` (or seeds defaults), initialises UI subsystems, runs the SDL framework loop, and auto-saves the session on shutdown.
