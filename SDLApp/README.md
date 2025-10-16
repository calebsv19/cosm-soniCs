# Directory: SDLApp

Purpose: SDL2 application harness that wraps window creation, the render loop, and callback wiring for the DAW.

## Files
- `sdl_app_framework.h`: Declares the minimal framework API and `AppContext` plumbing.
  - `App_Init`: Create the window/renderer pair and prepare timing state.
  - `App_SetRenderMode`: Switch between always-on rendering and throttled redraws.
  - `App_Run`: Drive the event loop, run callbacks, and honor the chosen render cadence.
  - `App_Shutdown`: Dispose of SDL objects and shut the subsystem down safely.
- `sdl_app_framework.c`: Implements the framework helpers declared in the header.
  - Polls SDL events, tracks delta time, and enforces the optional render throttle.
