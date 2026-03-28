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

Shared runtime/modules are vendored in-repo at:

- `third_party/codework_shared/`

Commands:

```bash
make
make run
```

### Shared Subtree Update

```bash
git -C daw fetch shared-upstream main
git -C daw subtree pull --prefix=third_party/codework_shared shared-upstream main --squash
```

Rebuild check:

```bash
make -C daw clean && make -C daw
```

## Scaffold Lane Policy

- `third_party/codework_shared/` is the vendored shared-subtree lane and remains the DAW dependency source of truth for shared modules.
- `extern/` is a compatibility/include lane only; new DAW feature implementation should not silently expand this lane.
- `SDLApp/` is a documented legacy exception lane for SDL framework glue; new app/domain logic should stay in `src/` and `include/`.
- New app-level public entry APIs should route through `include/daw/...`.
- Temporary files belong in `tmp/`, and runtime-generated config/cache lanes stay gitignored.

## Tests

Available targets:

```bash
make run-headless-smoke
make visual-harness
make test-stable
make test-legacy
make test-cache
make test-overlap
make test-smoke
make test-kitviz-adapter
make test-waveform-pack-warmstart
make test-kitviz-fx-preview-adapter
make test-kitviz-meter-adapter
make test-shared-theme-font-adapter
```

`test-stable` is the current deterministic migration gate lane.
`test-legacy` runs known stale/failing test targets to keep breakage visible while those lanes are being repaired.

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
- `docs/`: focused technical and release documentation (start at `docs/README.md`).
- `third_party/codework_shared/`: vendored shared core/kit/runtime modules.
