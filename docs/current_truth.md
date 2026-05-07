# DAW Current Truth

Last updated: 2026-05-04

## Program Identity
- Repository directory: `daw/`
- Public product name: `DAW (Alpha)`
- Primary runtime entry:
  - `src/app/main.c` -> `daw_app_main_run()`
  - wrapper shell: `include/daw/daw_app_main.h`, `src/app/daw_app_main.c`

## Current Shipped State
- Core seam decomposition wave is landed across app/engine/input/session/ui/undo lanes.
- Data-path contract foundation (`P3`) is complete with explicit runtime path fields and persistence.
- Release/desktop packaging lanes are complete through the shared target-contract flow.
- Intel `x86_64` packaging passed local gates after launcher runtime shader-lane hardening.
- Public release version is now `0.2.0`.

## Structure
- Required lanes: `docs/`, `src/`, `include/`, `tests/`, `build/`
- Active subsystems:
  - `app`, `audio`, `config`, `core`, `effects`, `engine`, `export`, `input`, `render`, `session`, `time`, `ui`, `undo`
- Include strategy remains include-dominant with small private-header surface in `src/`.

## Runtime and Data Path Contract
- Explicit runtime roots are active (`input_root`, `output_root`, `library_copy_root`).
- Runtime path persistence is normalized in runtime config lanes.
- Ingest-mode and library copy-vs-reference contract is explicit and test-covered.

## Verification Contract
- Build/harness:
  - `make -C daw clean && make -C daw all`
  - `make -C daw run-headless-smoke`
  - `make -C daw visual-harness`
- Stable tests:
  - `make -C daw test-stable`
- Legacy tests:
  - `make -C daw test-legacy`
- Packaging/release gates:
  - `make -C daw package-desktop*`
  - `make -C daw package-desktop-refresh`
  - `make -C daw release-contract`
  - `make -C daw release-bundle-audit`
  - `make -C daw release-verify ...`
  - `make -C daw release-distribute ...`

## Dependency and Runtime Policy
- `third_party/` remains vendored shared subtree lane.
- `extern/` and `SDLApp/` remain compatibility lanes by policy.
- Temp/runtime generated lanes remain ignored and normalized.

## Current Boundary
- Preserve seam decomposition stability and data-path contract correctness while closing the remaining manual Intel launch proof step.
- Keep launcher/runtime shader-copy hardening aligned with the packaged Vulkan/runtime contract.

## History and Deep Lane References
- Full lane history is in:
  - `/Users/calebsv/Desktop/CodeWork/docs/private_program_docs/daw/`
- This file is the compressed public current-state contract.
