# Effects Upgrade North Star Plan

## Purpose
This document is the top-level implementation plan for upgrading the DAW effects system so parameter ranges, tempo sync behavior, and UI controls are consistent, safe, and musically purposeful.

## North Star Outcomes
1. Every effect parameter has a single trusted definition of meaning, range, unit, and behavior.
2. UI, engine, serialization, and DSP clamps all agree on parameter boundaries.
3. Beat/time mode uses parameter-aware bounds instead of global hardcoded limits.
4. Tempo-synced controls feel intentional and stable across tempo changes.
5. Effects without metadata coverage are brought under the same spec-driven system.

## Scope
In scope:
1. Effect parameter metadata and spec coverage.
2. Beat/time conversion and quantization behavior.
3. Range enforcement and mismatch elimination across UI/engine/DSP.
4. Missing spec coverage for currently untyped effects.
5. Metering metadata planning and staged implementation.

Out of scope for this upgrade track:
1. New DSP algorithms unrelated to parameter safety/consistency.
2. Full metering analyzer redesign beyond parameterization scaffolding.
3. Non-effects UI redesign work.

## Core Problems To Resolve
1. Spec/DSP mismatches (different param counts, names, ranges, meanings).
2. Hardcoded beat mode bounds (`1/64 .. 8 beats`) applied to all syncable params.
3. Beat quantization constants using approximations instead of exact rhythmic fractions.
4. Missing param specs for some registered effects.
5. Inconsistent control purpose caused by fallback metadata and duplicated clamp logic.

## Phase Roadmap

### Phase 1: Spec and DSP Truth Alignment
Goal: eliminate mismatches so each effect has accurate metadata.

Deliverables:
1. Fix incorrect specs for effects with known mismatches (Compressor, DeEsser, AutoTrim, StereoBlend, BitCrusher, Decimator).
2. Verify each spec range/default maps correctly to each DSP `set_param` implementation.
3. Add/adjust unit/curve/step/enum metadata where needed.
4. Validate session load/save compatibility implications for renamed/reordered params.

Definition of done:
1. No known spec vs DSP param-count mismatches remain.
2. No known spec vs DSP clamp-range mismatches remain.
3. Targeted regression build and behavior checks pass.

### Phase 2: Beat/Time Mode Boundary Upgrade
Goal: make sync mode parameter-aware and musically correct.

Deliverables:
1. Introduce spec-derived beat min/max calculations from native bounds + tempo.
2. Replace hardcoded beat ranges in input/spec UI/slot UI paths.
3. Clamp converted beat/native values through spec-aware constraints before DSP apply.
4. Upgrade beat quantization table to exact rhythmic fractions.

Definition of done:
1. Sync mode bounds are derived per parameter.
2. No UI path relies on global `1/64 .. 8 beats` as universal limits.
3. Tempo change resync behavior remains stable and predictable.

### Phase 3: Coverage Gap Closure
Goal: remove fallback behavior for active effect types.

Deliverables:
1. Add param specs for currently non-spec effects (Phaser, FormantFilter, CombFF).
2. Register those effects with `FX_ENTRY_SPEC`.
3. Confirm spec panel behavior is stable for all supported effect IDs.

Definition of done:
1. All production effect types in registry have explicit param specs.
2. Fallback generic spec path is no longer used for active effects.

### Phase 4: Metering Parameterization Foundation
Goal: define intentional control model for metering effects.

Deliverables:
1. Define meter param metadata strategy (read-only vs controllable params).
2. Add initial param specs for meter controls where meaningful (window, release, scale/range, mode).
3. Keep pass-through behavior unchanged unless explicitly upgraded.

Definition of done:
1. Metering effects have a documented parameter model and staged implementation plan.
2. No accidental behavior regressions in existing meter display flows.

## Cross-Phase Engineering Rules
1. Specs are the first source of truth for UI meaning.
2. DSP still hard-clamps for safety; specs must match those clamps.
3. Any parameter rename/reorder must include session compatibility handling.
4. Every phase must end with `make` and targeted runtime validation notes.

## Document Workflow (Execution Standard)
1. Create one in-depth phase doc before implementation:
   `docs/effects_upgrade/PHASE_0X_<short_name>.md`
2. Inside each phase doc, list ordered tasks with checkbox status:
   `- [ ] Step N ...` then mark `- [x]` when completed.
3. Execute phase step-by-step and update status live.
4. When a phase is fully complete, move its doc to:
   `docs/completed/effects_upgrade/`
5. Start the next phase by creating its new in-progress doc in:
   `docs/effects_upgrade/`

## Suggested Phase Document Template
1. Objective
2. File-level change list
3. Ordered implementation checklist
4. Validation checklist (`make`, focused behavior checks)
5. Risks and rollback notes
6. Completion criteria

## Current Status
1. North Star plan established.
2. Ready to author `Phase 1` implementation doc and begin execution.
