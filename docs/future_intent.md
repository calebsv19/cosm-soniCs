# DAW Future Intent

Last updated: 2026-05-04

## Scaffold Alignment Intent
1. Preserve DAW's existing subsystem decomposition strengths.
2. Add scaffold-locked lifecycle wrapper entrypoint without behavior regressions.
3. Normalize verification gates into clear stable vs legacy lanes.
4. Lock naming/path policy for optional dependency lanes and legacy top-level outliers.

## Planned Next Structural Intent
- `DAW-S1` (completed):
  - public scaffold docs floor added:
    - `docs/current_truth.md`
    - `docs/future_intent.md`
  - public docs index lane added:
    - `docs/README.md`
  - private migration docs index updated with scaffold plan + freeze references.

- `DAW-S2` (completed):
  - add scaffold verification aliases in `Makefile`:
    - `run-headless-smoke`
    - `visual-harness`
  - define and enforce split test lanes:
    - `test-stable`
    - `test-legacy`
  - explicitly repair or quarantine currently stale failing test targets.

- `DAW-S3` (completed):
  - introduce canonical wrapper entry API:
    - `include/daw/daw_app_main.h`
    - `src/app/daw_app_main.c`
  - lock lifecycle stage symbol names for bootstrap/load/init/run/shutdown.
  - keep legacy runtime body behavior via delegated `daw_app_main_legacy()` transition path.

- `DAW-S4` (completed):
  - lock naming/path policy for `third_party/`, `extern/`, and top-level `SDLApp/`.
  - confirm temp/runtime ignore lanes are scaffold-consistent.

- `DAW-S5` (completed):
  - run stabilization closeout docs sync and final verification gates.
  - scaffold completion commit created: `Project Scaffold Standardization`.

## Post-Scaffold Font Pass
- text scaling and layout-safety migration lane is complete for current DAW scope.
- standardized closeout commit title for this lane:
  - `Post-Scaffold Font Size Standardization`
- runtime expectations kept by this lane:
  - Cmd/Ctrl `+`, `-`, `0` controls remain active.
  - font growth/shrink reflows pane geometry and list/header spacing from measured metrics.
  - repeated zoom commands preserve text visibility via renderer-safe cache invalidation.

## App Packaging Intent
- DAW release-readiness lane is now complete (`DAW-RL0` through `DAW-RL5`):
  - standardized `package-desktop*` + `release-*` targets are landed
  - notarization/staple/verification lane is integrated in Makefile
  - launcher runtime model is hardened for writable runtime + Vulkan ICD exports
- Intel target-contract follow-up is now in maintenance/proof mode:
  - `TARGET_ARCH=x86_64` builds and release artifacts are active
  - target-aware dependency closure and runtime shader-copy hardening are landed
  - next step is manual Intel launch verification on refreshed package output
- next packaging posture:
  - maintenance-only parity with ecosystem release contract updates
  - keep Desktop/Finder launch + `release-verify-notarized` evidence in packaging-affecting closeout gates

## Data Path Contract Intent
- DAW data-path contract lane is complete (`P3-S0` through `P3-S5`).
- next posture:
  - maintenance-only updates for explicit root handling and ingest-mode matrix (`copy` in library pane, `reference` in timeline drop).
  - keep `test-data-path-contract` and `test-library-copy-vs-reference-contract` in stable verification lane.

## Connection Pass Intent
- completed:
  - `DAW-CP0` baseline routing map refresh
  - `DAW-CP1` top-level context + stage ownership hardening in `src/app/daw_app_main.c`
  - `DAW-CP2` explicit runtime dispatch seam extraction (`daw_app_dispatch_runtime(...)`)
  - `DAW-CP3` update/render separation via explicit derivation contracts in `src/app/main.c`
  - `DAW-CP4` deterministic wrapper-owned lifetime release + reverse-order teardown hardening in `src/app/daw_app_main.c`
  - `DAW-CP5` verification/docs/memory closeout
- next:
  - optional `CP6+`: deeper migration of legacy runtime ownership into wrapper-managed subsystems

## Seam Split Intent (Post-Refactor)
- current state:
  - abstract seam split wave is landed across `app`, `engine`, `input`, `session`, `ui`, and `undo` lanes (`1c05136`).
  - live worktree path normalization is active for overlay/adapters files under:
    - `src/ui/overlay/*`
    - `src/render/adapters/*`
- next posture:
  - maintenance-only seam refinements with behavior parity as default.
  - keep documentation and source paths aligned whenever overlay/adapter files are moved between directories.

## Cross-Program Wrapper Initiative
- `W0` complete (canonical wrapper contract frozen)
- `W1` complete for `daw` (typed context/stage/dispatch wrapper shape aligned)
- `W2` complete for `daw` (structured wrapper diagnostics normalization + wrapper exit summary logging)
- execution note:
  - `../../docs/private_program_docs/daw/2026-04-02_daw_w1_w2_wrapper_hardening.md`

## Non-Goals During Scaffold Migration
- No feature expansion unrelated to scaffold alignment.
- No shared subtree redesign inside scaffold migration commits.
- No broad one-pass naming churn; changes stay bounded per migration slice.
