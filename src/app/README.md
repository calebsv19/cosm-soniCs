# Directory: src/app

Purpose: Application bootstrap and SDL event loop integration.

## Files
- `main.c`
  - `handle_input`: Feeds the current SDL event into the input manager when available.
  - `handle_update`: Ensures layout sizing is up to date and advances input state each frame.
  - `handle_render`: Clears the renderer, draws panes/controls/overlays, and presents the frame.
  - `main`: Loads config, creates the audio engine, initialises UI subsystems, and runs the SDL framework loop.
