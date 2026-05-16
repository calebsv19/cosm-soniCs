# DAW Docs Index

Start here for DAW public documentation.
Last audited: 2026-05-15.

## Scaffold State
- `docs/current_truth.md`: current scaffold/runtime state and verification snapshot.
- `docs/future_intent.md`: intended scaffold convergence path and next migration phases.
- Intel `x86_64` packaging/runtime hardening is active in the current truth and desktop packaging docs.
- MIDI/instrument and audio-recording state is summarized in current truth and future intent; detailed implementation history stays in the private DAW planning lane.
- migration-friendly verification gates:
  - `make -C daw run-headless-smoke`
  - `make -C daw visual-harness`
  - `make -C daw test-stable`
  - `make -C daw test-legacy`

## Existing Public Docs
- `docs/desktop_packaging.md`
- `docs/DAW_ARCH_EFFECTS_AUDIT.md`
- `docs/DAW_EFFECTS_PANEL_STATUS.md`
- `docs/DAW_WAKE_IDLE_LOOP_MIGRATION_PLAN.md`
- `docs/KEYBINDINGS.md`
- `docs/SERIALIZATION_FALLBACK.md`
- `docs/effects_upgrade/NORTH_STAR_EFFECTS_UPGRADE_PLAN.md`
- `docs/ui/README.md`

## Private Planning Docs
- Private DAW migration docs live at:
  - `../../docs/private_program_docs/daw/`
