# DAW (Alpha)

A C-based desktop digital audio workstation prototype built with SDL2 and a shared Vulkan renderer.
This repository is an early public baseline intended for real-world testing and iterative improvement.

## Current State

- Stage: alpha, actively developed.
- Build output: `build/daw_app`.
- Platform focus: macOS-first local desktop workflows.
- License: Apache-2.0.

## Implemented Today

- Multi-track timeline editing with clip selection, drag, trim, and snap behavior.
- Transport controls (play/stop/seek), loop region controls, zoom, and grid options.
- Library browser for local audio assets (`.wav` and `.mp3`).
- Track mute/solo handling and core timeline/track interaction loop.
- Effects panel and parameter control path for the current built-in effects set.
- Session/project persistence with deterministic startup fallback:
  1. `config/projects/last_project.txt`
  2. `config/last_session.json`
  3. `config/templates/public_default_project.json`
  4. fresh in-memory bootstrap
- Runtime diagnostics toggles for engine/cache/timing logging.

## Current Gaps

- Buses/sends are not implemented yet.
- MIDI regions and instrument workflows are not implemented yet.
- Effects are functional but still a basic subset.
- General alpha-level UI/engine glitches can still occur.

See [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) for the current issue list.

## Build and Run

Prerequisites:

- C11 compiler (`cc`/clang)
- SDL2 + SDL2_ttf
- Vulkan loader/dev headers (with Metal interop on macOS)

Commands:

```bash
make
make run
```

## Tests

Available targets:

```bash
make test-cache
make test-overlap
make test-smoke
make test-kitviz-adapter
make test-waveform-pack-warmstart
make test-kitviz-fx-preview-adapter
make test-kitviz-meter-adapter
make test-shared-theme-font-adapter
```

`make test-session` is currently known to fail link in this branch and is tracked in `KNOWN_ISSUES.md`.

## Public Release Hygiene

- This repo intentionally ships without bundled user audio content in `assets/audio`.
- Runtime-generated caches and local session/project state are excluded from public commits.
- Fallback serialization behavior is documented in `docs/SERIALIZATION_FALLBACK.md`.
- Security model and safe-usage guidance are documented in `SECURITY.md`.

## Repository Layout

- `src/` and `include/`: DAW implementation and headers.
- `config/`: runtime config and public fallback template.
- `assets/`: runtime assets (no bundled public audio samples).
- `tests/`: unit/smoke/stress test targets.
- `docs/`: focused technical and release documentation.
