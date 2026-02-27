# Phase 7 - Gate Enforcement and Scope Producer Wiring (Deep Implementation Plan)

## Phase Objective
Close remaining producer gaps and make performance gates executable in-loop so idle/playback behavior is continuously validated by diagnostics output.

## Why This Phase Exists
Phase 6 added typed producer messages and coalesced wake signaling, but one producer path (`FX scope`) is not yet posting main-thread messages and gate evaluation is still manual.

## Scope
In scope:
- Wire FX scope producer callback to typed producer message posting.
- Add loop diagnostics gate evaluation mode with pass/fail outputs.
- Add scenario-aware gate thresholds (`idle`, `playback`, `interaction`) via env knobs.
- Update docs and phase checklist with executed validation.

Out of scope:
- New DSP behavior.
- UI redesign.
- Cross-process telemetry export.

## Shared-Core Reuse Decision
Continue reuse through DAW adapters:
- `core_queue`, `core_jobs`, `core_wake`, `core_time`, `core_sched`, `core_kernel`

No direct `shared/core/*` source edits planned.

## Runtime Contract (Phase 7 Target)
1. Producer callbacks (`FX meter`, `FX scope`, transport tick) post typed main-thread messages.
2. Wake signaling remains coalesced and bounded.
3. Diagnostics can run in gate mode and print pass/fail summary per window.
4. Gate logic is scenario-aware and configurable by env.

## File Plan
Primary files:
- `daw/src/engine/engine_scope_host.c`
- `daw/src/app/main.c`
- `daw/src/app/README.md`
- `daw/docs/wake_loop_migration/phases/phase_07_gate_enforcement_and_scope_producer_wiring.md`

## Execution Checklist
- [x] P7.1 Add Phase 7 deep plan doc.
- [x] P7.2 Wire FX scope producer to typed main-thread message post API.
- [x] P7.3 Add diagnostics gate mode/env policy in main loop diagnostics.
- [x] P7.4 Implement scenario-aware pass/fail checks for idle/playback/interaction windows.
- [x] P7.5 Ensure gate outputs include concrete metrics and threshold values.
- [x] P7.6 Update app docs for gate-mode usage.
- [x] P7.7 Build passes (`make -C daw`).
- [x] P7.8 Run smoke/scenario command set and capture notes.
- [x] P7.9 Mark checklist complete with execution notes.

Execution notes:
- `daw/src/engine/engine_scope_host.c`
  - `engine_fx_scope_tap_callback(...)` now posts `DAW_MAINTHREAD_MSG_ENGINE_FX_SCOPE` via `daw_mainthread_message_post(...)`, completing scope producer participation in the Phase 6 message+wake contract.
- `daw/src/app/main.c`
  - Added gate policy parsing from env:
    - `DAW_LOOP_GATE_EVAL=1`
    - `DAW_SCENARIO=idle|playback|interaction`
    - optional threshold overrides: `DAW_GATE_MIN_WAITS_PLAYBACK`, `DAW_GATE_MAX_ACTIVE_PCT_IDLE`, `DAW_GATE_MIN_BLOCKED_PCT_IDLE`, `DAW_GATE_MAX_ACTIVE_PCT_INTERACTION`
  - Added scenario-aware `[LoopGate]` pass/fail logging in diagnostics callback with concrete metric and threshold values.
    - idle: validates active% and blocked%
    - playback: validates waits/renders/render-wait cadence signal
    - interaction: validates active% ceiling
- `daw/src/app/README.md`
  - Added gate diagnostics usage note for scenario-based pass/fail output.
- Validation:
  - `make -C daw` passes.
  - Scenario launch attempts executed for `idle`, `playback`, and `interaction` with gate mode enabled.
  - This environment still cannot reach full loop execution due backend constraints (`SDL_Init failed: The video driver did not add any displays`, CoreAudio device unavailable), so gate outputs are ready but require display/audio-capable runtime for full observation.

## Acceptance Criteria
Phase 7 is complete when:
1. P7.1-P7.9 are complete.
2. FX scope producer path participates in message+wake contract.
3. Gate mode emits deterministic pass/fail diagnostic lines.
4. Build and smoke checks pass in current environment constraints.

## Risks and Mitigations
- Risk: too-noisy scope producer events.
  - Mitigation: rely on message coalescing from Phase 6.
- Risk: gate false negatives in non-interactive environments.
  - Mitigation: scenario labels + explicit “skipped/inconclusive” output paths.
