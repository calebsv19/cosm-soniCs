# Known Issues (Alpha)

- Autosave/session behavior is still evolving; save project state frequently.
- Occasional runtime instability may occur during heavy editing/playback workflows.
- Performance and timing instrumentation can impact smoothness when enabled.
- Public release ships without bundled audio samples; add local `.wav`/`.mp3` files under `assets/audio`.
- Routing is still minimal: buses/sends are not implemented yet.
- MIDI regions and instrument workflows are not implemented yet.
- Effects are currently a basic subset; advanced/complex processing is still in progress.
- General alpha UI/engine glitches can still occur during normal use.
- `make test-session` currently fails to link in this branch due missing test-target dependencies; `make` app build remains the primary verification path for now.
