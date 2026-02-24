# Phase 04: Metering Parameterization Foundation

## Objective
Define and implement a spec-backed parameter foundation for metering effects so meter behavior can evolve through explicit metadata, while keeping audio pass-through behavior unchanged.

## Metering Parameter Strategy
1. `Read-only telemetry` remains in `EngineFxMeterSnapshot` and is not represented as automatable params.
2. `Controllable meter settings` are modeled as explicit effect params (window/decay/range/mode/palette).
3. Phase 04 introduces these controls as hidden metadata-backed params to avoid accidental UX regressions.
4. DSP audio path for meter effects remains pass-through (`input == output` behavior unchanged).

## File-Level Change List
1. `include/effects/param_specs/metering_param_specs.h`
2. `src/effects/effects_builtin.c`
3. `src/effects/metering/fx_correlation_meter.c`
4. `src/effects/metering/fx_mid_side_meter.c`
5. `src/effects/metering/fx_vectorscope_meter.c`
6. `src/effects/metering/fx_peak_rms_meter.c`
7. `src/effects/metering/fx_lufs_meter.c`
8. `src/effects/metering/fx_spectrogram_meter.c`
9. `src/ui/effects_panel/spec_panel.c`

## Ordered Implementation Checklist
- [x] Step 1: Define meter spec arrays for IDs `100..105` with meaningful control metadata (window, release/decay, range, mode, palette).
- [x] Step 2: Register meter effects with `FX_ENTRY_SPEC` and metering spec counts.
- [x] Step 3: Expand meter `get_desc` models from 0 params to explicit foundational params.
- [x] Step 4: Add non-audio-impact `set_param` clamping/state storage in each meter effect.
- [x] Step 5: Keep metering DSP pass-through behavior unchanged.
- [x] Step 6: Route meter IDs through spec panel path so hidden params do not surface as fallback controls.
- [x] Step 7: Run build validation and confirm compile stability.
- [x] Step 8: Mark phase complete and move to `docs/completed/effects_upgrade/`.

## Validation Checklist
- [x] `make` passes.
- [x] Meter IDs `100..105` now provide explicit spec-backed metadata in registry.
- [x] Meter audio processing remains pass-through.

## Risks and Rollback Notes
1. Session snapshots for meter effects now include foundational param slots instead of zero params.
2. Controls are hidden in this phase; UI wiring for visible meter controls is deferred to a later UX-focused phase.
3. Rollback path is restoring prior zero-param meter descriptors and `FX_ENTRY` registration.

## Completion Criteria
1. Metering effects have explicit metadata models aligned with future controllable settings.
2. Registry and descriptors are spec-backed for meter IDs `100..105`.
3. Build succeeds with no audio-path regressions in meter modules.
