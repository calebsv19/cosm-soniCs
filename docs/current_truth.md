# DAW Current Truth

Last updated: 2026-03-27

## Program Identity
- Repository directory: `daw/`
- Public product name in README: `DAW (Alpha)`
- Primary runtime entry path today:
  - `src/app/main.c` (`main()` delegates to `daw_app_main_run()`)
  - canonical lifecycle wrapper entry:
    - `include/daw/daw_app_main.h`
    - `src/app/daw_app_main.c`

## Current Structure
- Required scaffold lanes are present:
  - `docs/`, `src/`, `include/`, `tests/`, `build/`
- Active source subsystem lanes:
  - `app`, `audio`, `config`, `core`, `effects`, `engine`, `export`, `input`, `render`, `session`, `time`, `ui`, `undo`
- Header strategy:
  - include-dominant (`include/*`) with one local private header in `src/`:
    - `src/session/session_io_read_internal.h`

## Runtime/Verification Contract (Current)
- Build:
  - `make -C daw clean && make -C daw all`
- Scaffold smoke gate (non-interactive):
  - `make -C daw run-headless-smoke`
- Visual harness build gate:
  - `make -C daw visual-harness`

Stable test lane:
- `make -C daw test-stable`
- current composition:
  - `test-pack-contract`
  - `test-trace-contract`
  - `test-trace-async-contract`
  - `test-kitviz-adapter`
  - `test-kitviz-fx-preview-adapter`
  - `test-kitviz-meter-adapter`
  - `test-waveform-pack-warmstart`

Legacy test lane:
- `make -C daw test-legacy`
- current composition (known stale/failing under active migration):
  - `test-session` (link-time missing engine/session symbols)
  - `test-cache` (compile-time API mismatch in test call signature)
  - `test-overlap` (link-time missing engine symbols)
  - `test-smoke` (link-time missing engine symbols)
  - `test-shared-theme-font-adapter` (runtime assertion failure)

## Dependency Lane Snapshot
- Locked lane policy:
  - `third_party/` (vendored shared subtree lane)
  - `extern/` (compatibility/include lane only; no silent feature-lane drift)
  - `SDLApp/` (legacy top-level exception lane for framework glue)
- New app-level public entry APIs route through:
  - `include/daw/...`

## Temp/Runtime Ignore Snapshot
- temp lane is gitignored:
  - `tmp/`
- runtime/generated lanes are gitignored:
  - `config/runtime/`
  - `config/cache/`
  - `config/library_index.json`
  - `config/last_session.json`
  - `config/projects/*.json`

## Active Scaffold Migration State
- Private migration plan:
  - `../docs/private_program_docs/daw/2026-03-27_daw_scaffold_standardization_switchover_plan.md`
- Baseline freeze:
  - `../docs/private_program_docs/daw/2026-03-27_daw_s0_baseline_freeze_and_mapping.md`
- Completed phases:
  - `DAW-S0`, `DAW-S1`, `DAW-S2`, `DAW-S3`, `DAW-S4`, `DAW-S5`
- Next phase:
  - scaffold migration complete; next work is normal feature/fix flow with `test-stable` as baseline gate
