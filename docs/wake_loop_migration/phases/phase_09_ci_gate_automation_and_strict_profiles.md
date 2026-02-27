# Phase 9 - CI Gate Automation and Strict Profiles (Deep Implementation Plan)

## Phase Objective
Operationalize loop-gate validation by adding CI workflow automation, strict threshold profiles, and Makefile entry points for repeatable local and CI execution.

## Why This Phase Exists
Phase 8 introduced a harness script, but there was no first-class CI integration and no standardized strict-profile mode for regression hardening.

## Scope
In scope:
- Add strict/profile controls to gate harness.
- Add Makefile targets for default and strict gate runs.
- Add CI workflow to run build + gate harness and upload artifacts.
- Document execution behavior and environment outcomes.

Out of scope:
- Full headless rendering backend enablement.
- Cross-platform CI matrix tuning.

## Shared-Core Reuse Decision
No shared-core source changes. Uses existing loop diagnostics exposed through DAW adapter layer.

## Runtime Contract (Phase 9 Target)
1. Developers can run:
   - `make loop-gates`
   - `make loop-gates-strict`
2. Harness supports:
   - `PROFILE=default|strict`
   - `STRICT=0|1`
   - summary file output.
3. CI runs harness and classifies outcomes:
   - fail -> job fails
   - inconclusive -> job continues with logs/artifacts

## File Plan
- `daw/tools/run_loop_gates.sh`
- `daw/Makefile`
- `daw/.github/workflows/loop-gates.yml`
- `daw/docs/wake_loop_migration/phases/phase_09_ci_gate_automation_and_strict_profiles.md`
- `daw/src/app/README.md`

## Execution Checklist
- [x] P9.1 Add Phase 9 deep plan doc.
- [x] P9.2 Add strict profile and summary output support in harness script.
- [x] P9.3 Add Makefile targets for normal/strict gate runs.
- [x] P9.4 Add CI workflow for build + harness + artifact upload.
- [x] P9.5 Update app docs with local gate commands.
- [x] P9.6 Build passes (`make -C daw`).
- [x] P9.7 Run local harness default and strict mode; capture results.
- [x] P9.8 Mark checklist complete with execution notes.

Execution notes:
- Harness enhancements (`daw/tools/run_loop_gates.sh`):
  - Added `PROFILE=default|strict`.
  - Added `STRICT=0|1` behavior (strict treats inconclusive as failure exit).
  - Added summary artifact output via `SUMMARY_FILE` (default: `<log_dir>/summary.txt`).
  - Added strict profile defaults:
    - `DAW_GATE_MAX_ACTIVE_PCT_IDLE=10`
    - `DAW_GATE_MIN_BLOCKED_PCT_IDLE=90`
    - `DAW_GATE_MIN_WAITS_PLAYBACK=2`
    - `DAW_GATE_MAX_ACTIVE_PCT_INTERACTION=85`
- Makefile integration (`daw/Makefile`):
  - Added `loop-gates` target.
  - Added `loop-gates-strict` target.
- CI workflow added:
  - `daw/.github/workflows/loop-gates.yml`
  - Builds DAW, runs harness, uploads `/tmp/daw_loop_gates_*` artifacts.
  - Fails job on harness exit `1`, keeps job non-failing on harness exit `2` (inconclusive).
- App docs updated (`daw/src/app/README.md`) with new command entry points.
- Validation results:
  - `make -C daw` -> pass.
  - `RUN_SECONDS=2 ./tools/run_loop_gates.sh` -> exit `2` (inconclusive in this environment).
  - `PROFILE=strict STRICT=1 RUN_SECONDS=2 ./tools/run_loop_gates.sh` -> exit `1` (strict mode converts inconclusive to failure).
  - `RUN_SECONDS=2 make -C daw loop-gates` -> exit `2` (expected default-profile inconclusive propagation).
  - Log directory from this run: `/tmp/daw_loop_gates_20260226_144926`.

## Acceptance Criteria
Phase 9 is complete when:
1. P9.1-P9.8 complete.
2. Local and CI entry points exist and are documented.
3. Strict mode is available and deterministic.
4. Inconclusive environments are classified clearly and artifacts are captured.

## Risks and Mitigations
- Risk: CI environment remains inconclusive due backend availability.
  - Mitigation: artifact-first workflow; fail only on explicit gate failures.
- Risk: strict thresholds cause noisy failures.
  - Mitigation: profile-based thresholds and env override knobs.
