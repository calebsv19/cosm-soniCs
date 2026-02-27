# Phase 8 - Loop Gate Harness and Report (Deep Implementation Plan)

## Phase Objective
Create an executable gate harness that runs idle/playback/interaction scenarios, captures loop diagnostics and gate output, and produces a deterministic summary report (`pass` / `fail` / `inconclusive`).

## Why This Phase Exists
Phase 7 introduced in-loop gate evaluation, but validation is still manual and environment-dependent. We need a repeatable command path you can run on your local display/audio-capable machine after each change.

## Scope
In scope:
- Add a DAW-local gate harness script under `daw/tools/`.
- Standardize scenario execution (`idle`, `playback`, `interaction`).
- Capture per-scenario logs and produce a single summary.
- Return non-zero exit codes on gate failures.

Out of scope:
- CI integration.
- External telemetry backends.
- UI changes.

## Shared-Core Reuse Decision
No shared-core source changes required. This phase consumes existing diagnostics emitted by:
- DAW loop adapters (`core_time`, `core_queue`, `core_sched`, `core_jobs`, `core_wake`, `core_kernel` via DAW wrappers).

## Runtime Contract (Phase 8 Target)
Harness behavior:
1. Launch DAW with `DAW_LOOP_DIAG_LOG=1` and `DAW_LOOP_GATE_EVAL=1`.
2. Run each scenario for a configured duration.
3. Store logs per scenario.
4. Parse `[LoopGate]` lines:
   - `pass=yes` -> pass
   - `pass=no` -> fail
   - missing gate lines + backend startup failure markers -> inconclusive
5. Print summary and return code:
   - `0` all pass
   - `1` at least one fail
   - `2` no fails but one or more inconclusive

## File Plan
Primary files:
- `daw/tools/run_loop_gates.sh`
- `daw/docs/wake_loop_migration/phases/phase_08_loop_gate_harness_and_report.md`
- `daw/src/app/README.md` (usage note)

## Execution Checklist
- [x] P8.1 Add Phase 8 deep plan doc.
- [x] P8.2 Add executable harness script for loop gate scenarios.
- [x] P8.3 Add scenario log directory + summary output behavior.
- [x] P8.4 Implement pass/fail/inconclusive parser logic.
- [x] P8.5 Add exit code policy (`0 pass`, `1 fail`, `2 inconclusive`).
- [x] P8.6 Add usage note to app docs.
- [x] P8.7 Build passes (`make -C daw`).
- [x] P8.8 Run harness in current environment and capture output notes.
- [x] P8.9 Mark checklist complete with execution notes.

Execution notes:
- Added executable harness:
  - `daw/tools/run_loop_gates.sh`
  - runs `idle|playback|interaction` scenarios with `DAW_LOOP_DIAG_LOG=1`, `DAW_LOOP_GATE_EVAL=1`
  - captures logs per scenario under `/tmp/daw_loop_gates_<timestamp>/`
  - parses `[LoopGate]` lines and backend failure markers
  - returns:
    - `0` all pass
    - `1` one or more failures
    - `2` inconclusive scenarios with no failures
- Added app doc usage note in `daw/src/app/README.md`.
- Validation:
  - `make -C daw` -> pass (`Nothing to be done for all` in this run).
  - Harness run in current environment:
    - command: `RUN_SECONDS=2 ./tools/run_loop_gates.sh`
    - result: all scenarios inconclusive (`no_display_backend`)
    - exit code: `2`
    - logs: `/tmp/daw_loop_gates_20260226_144729`

## Acceptance Criteria
Phase 8 is complete when:
1. P8.1-P8.9 are complete.
2. Harness runs all scenarios and emits deterministic summary.
3. Failures are machine-readable via exit code.
4. Current environment limitations are clearly classified as inconclusive.

## Risks and Mitigations
- Risk: false failures from environment startup issues.
  - Mitigation: explicit inconclusive classification using backend failure markers.
- Risk: script drift from runtime env vars.
  - Mitigation: keep env knobs centralized and documented in script header.
