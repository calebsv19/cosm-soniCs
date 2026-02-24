# Phase 03: Coverage Gap Closure

## Objective
Add explicit parameter specs for `Phaser`, `FormantFilter`, and `CombFF`, register them as spec-backed effects, and remove fallback metadata usage for these active effect IDs.

## File-Level Change List
1. `include/effects/param_specs/filter_tone_param_specs.h`
2. `src/effects/effects_builtin.c`
3. `src/ui/effects_panel/spec_panel.c`

## Ordered Implementation Checklist
- [x] Step 1: Add DSP-accurate param specs for `Phaser` (ID 44), `FormantFilter` (ID 45), and `CombFF` (ID 46).
- [x] Step 2: Add spec-count macros for the new filter/tone spec arrays.
- [x] Step 3: Convert registry entries for IDs `44/45/46` from `FX_ENTRY` to `FX_ENTRY_SPEC`.
- [x] Step 4: Enable spec panel routing for IDs `44/45/46` so they use spec-driven controls.
- [x] Step 5: Validate no remaining unspecced active filter/tone effects in the registry path.
- [x] Step 6: Run build validation.
- [x] Step 7: Mark phase complete and move to `docs/completed/effects_upgrade/`.

## Validation Checklist
- [x] `make` passes.
- [x] IDs `44/45/46` return explicit param spec arrays via registry.
- [x] No fallback spec path is required for the Phase 03 targets.

## Risks and Rollback Notes
1. UI labels and control scaling for these three effects will change from fallback defaults to explicit spec metadata.
2. Existing sessions keep parameter values; only control metadata/behavior alignment is changed.
3. Rollback path is restoring prior registry + spec declarations for IDs `44/45/46`.

## Completion Criteria
1. `Phaser`, `FormantFilter`, and `CombFF` have explicit spec arrays and registry spec bindings.
2. Effects panel can render spec-driven UI for these IDs.
3. Build succeeds and checklist is fully complete.
