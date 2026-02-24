# Phase 05: Meter Mode UX Polish

## Objective
Make the meter detail experience feel coherent and intentional by binding detail toggles to real effect params and keeping panel mode UI synchronized with effect state.

## File-Level Change List
1. `src/input/effects_panel_input.c`
2. `src/ui/effects_panel/panel.c`

## Ordered Implementation Checklist
- [x] Step 1: Add helper logic to map open meter slot params into panel mode state.
- [x] Step 2: On meter effect open in list mode, sync panel mode toggles from slot params.
- [x] Step 3: Wire meter detail toggle clicks to write corresponding meter effect params.
- [x] Step 4: Keep legacy panel mode fields updated for compatibility with current meter render paths.
- [x] Step 5: Sync meter mode state from engine snapshots during `effects_panel_sync_from_engine`.
- [x] Step 6: Run build validation.
- [x] Step 7: Mark phase complete and move to `docs/completed/effects_upgrade/`.

## Validation Checklist
- [x] `make` passes.
- [x] Meter detail mode toggles now update engine effect params for IDs `102/104/105`.
- [x] Panel mode display stays consistent after engine sync/session restore.

## Risks and Rollback Notes
1. Mode mapping now depends on meter param enum values; future enum reorderings must update these mappings.
2. Existing session panel-mode fields remain in use but are now continuously reconciled with meter params.
3. Rollback path is restoring local-only mode toggles in input/panel sync.

## Completion Criteria
1. Meter detail toggles are parameter-backed instead of UI-only state.
2. Meter mode state remains stable through panel sync cycles.
3. Build succeeds.
