# soniCs Docs Index

Start here for soniCs public documentation.
Last audited: 2026-06-19.

Repository and source-level identifiers still use `daw`.

## Scaffold State
- `docs/current_truth.md`: current scaffold/runtime state and verification snapshot.
- `docs/demo_proof.md`: R6 demo-proof contract, canonical proof command,
  package proof lane, and manual-proof boundary.
- `docs/future_intent.md`: intended scaffold convergence path and next migration phases.
- `docs/memory_check_audit.md`: default-off fisiCs memory-check audit lane.
- Intel `x86_64` packaging/runtime hardening is active in the current truth and desktop packaging docs.
- MIDI/instrument and audio-recording state is summarized in current truth and future intent; detailed implementation history stays in the private DAW planning lane.
- Current diagnostics coverage is summarized in `docs/current_truth.md`.
- migration-friendly verification gates:
  - `make -C daw run-headless-smoke`:
    canonical demo proof; aggregate non-interactive smoke coverage with
    expected success line `daw headless smoke passed (non-interactive)` plus
    `demo-proof:` summary lines for separate package/manual proof lanes
  - `make -C daw visual-harness`:
    build-only visual readiness, not an unattended runtime pass
  - `make -C daw visual-artifact`:
    source-render first-frame proof at
    `visual_artifacts/daw_first_frame.bmp`
  - `make -C daw test-midi-editor-shell`
  - `make -C daw test-audio-capture-device`
  - `make -C daw test-audio-recording`
  - `make -C daw test-track-role`
  - `make -C daw test-stable`
  - `make -C daw package-desktop-self-test`
  - `make -C daw memory-check-audit`:
    default-off fisiCs audit lane, not part of the one-command demo proof
  - `make -C daw test-legacy`
- Manual packaged-app microphone validation remains outside automated gates;
  the latest live proof plus S24 follow-through leaves audio recording in a
  solid current state. Hard track typing, durable record-arm controls,
  input-device picker UI, user preset storage, and external MIDI input are
  future fresh-slice work.
- Security posture remains trusted local desktop use with R4 local path and
  package-artifact hardening; see `../SECURITY.md` for the current boundary
  notes.

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
