# Phase 01: Spec and DSP Truth Alignment

## Objective
Align effect parameter specs with the actual DSP parameter models so UI, engine, and processing behavior all use the same trusted ranges, defaults, and semantics.

## File-Level Change List
1. `include/effects/param_specs/dynamics_param_specs.h`
2. `include/effects/param_specs/basics_param_specs.h`
3. `include/effects/param_specs/distortion_param_specs.h`

## Ordered Implementation Checklist
- [x] Step 1: Align `Compressor` spec bounds/default assumptions to DSP clamps.
- [x] Step 2: Replace `DeEsser` spec with DSP-accurate 8-parameter model and ordering.
- [x] Step 3: Replace `AutoTrim` spec with DSP-accurate 4-parameter model and ordering.
- [x] Step 4: Replace `StereoBlend` spec with DSP-accurate `balance` + `keep_stereo` model.
- [x] Step 5: Replace `BitCrusher` spec with DSP-accurate `bits` + `srrate` + `mix` model.
- [x] Step 6: Replace `Decimator` spec with DSP-accurate 5-parameter model and ordering.
- [x] Step 7: Run build validation and record result.
- [x] Step 8: Mark this phase complete and ready to move to `docs/completed/effects_upgrade/`.

## Validation Checklist
- [x] `make` passes.
- [x] No known spec vs DSP count mismatches for the phase targets.
- [x] No known spec vs DSP range mismatches for the phase targets.

## Validation Result
1. `make` completed successfully after Phase 01 changes.

## Risks and Rollback Notes
1. Session data using prior implicit UI assumptions may display different labels/ranges after alignment.
2. DSP behavior is unchanged; this phase is metadata/truth alignment only.
3. Rollback path is limited to restoring prior spec tables if regressions are detected.

## Completion Criteria
1. Targeted effects in this phase have spec definitions matching DSP `get_desc` + `set_param` behavior.
2. Build succeeds.
3. Checklist is fully marked complete.
