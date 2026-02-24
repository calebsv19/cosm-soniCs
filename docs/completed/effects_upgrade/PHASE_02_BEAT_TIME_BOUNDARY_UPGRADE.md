# Phase 02: Beat/Time Boundary Upgrade

## Objective
Replace global hardcoded beat sync limits with per-parameter beat boundaries derived from each parameter spec and current tempo, and ensure value conversion paths are bounded and musically stable.

## File-Level Change List
1. `include/effects/param_utils.h`
2. `src/effects/param_utils.c`
3. `src/input/effects_panel_input.c`
4. `src/ui/effects_panel/spec_panel.c`
5. `src/ui/effects_panel/slot_view.c`
6. `src/effects/effects_manager.c`

## Ordered Implementation Checklist
- [x] Step 1: Add a shared helper for per-spec beat bounds in param utils (header + implementation).
- [x] Step 2: Update beat quantization table constants to exact rhythmic fractions.
- [x] Step 3: Replace hardcoded beat ranges in effects panel input paths with helper-based bounds.
- [x] Step 4: Replace hardcoded beat ranges in spec panel and slot view display/control paths.
- [x] Step 5: Add spec-range clamping in manager apply path after beat/native conversion.
- [x] Step 6: Run build validation and confirm no hardcoded global beat bounds remain in upgraded paths.
- [x] Step 7: Mark this phase complete and ready to move to `docs/completed/effects_upgrade/`.

## Validation Checklist
- [x] `make` passes.
- [x] Beat-bound logic in updated paths uses helper-derived per-param bounds.
- [x] No regressions in tempo-sync compile paths.

## Risks and Rollback Notes
1. Existing user sessions with beat-mode values near previous global extremes may map differently after per-spec clamping.
2. DSP behavior remains safety-clamped; this phase focuses on control-surface correctness and conversion consistency.
3. Rollback path is restoring prior beat-bound logic if interaction regressions are discovered.

## Completion Criteria
1. Updated UI/input paths use per-spec beat bounds instead of universal constants.
2. Manager conversion path clamps native values to declared spec bounds.
3. Build succeeds and checklist is fully complete.
