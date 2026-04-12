# DAW Current Truth

Last updated: 2026-04-11

## Program Identity
- Repository directory: `daw/`
- Public product name in README: `DAW (Alpha)`
- Primary runtime entry path today:
  - `src/app/main.c` (`main()` delegates to `daw_app_main_run()`)
  - canonical lifecycle wrapper entry:
    - `include/daw/daw_app_main.h`
    - `src/app/daw_app_main.c`

## Current Structure
- Required scaffold lanes are present:
  - `docs/`, `src/`, `include/`, `tests/`, `build/`
- Active source subsystem lanes:
  - `app`, `audio`, `config`, `core`, `effects`, `engine`, `export`, `input`, `render`, `session`, `time`, `ui`, `undo`
- Header strategy:
  - include-dominant (`include/*`) with one local private header in `src/`:
    - `src/session/session_io_read_internal.h`

## Seam Decomposition Snapshot (2026-04-11)
- Internal seam split wave landed across `app`, `engine`, `input`, `session`, `ui`, and `undo` lanes without changing DAW public behavior contracts.
- Main runtime decomposition highlights:
  - app: `src/app/main_bounce.c` extracted from `src/app/main.c`
  - engine clip lane: `src/engine/engine_clips_automation.c`, `src/engine/engine_clips_no_overlap.c`
  - input helpers: `src/input/effects_panel_input_helpers.c`, `src/input/inspector_input_numeric_edit.c`, timeline mouse split helpers
  - session parse split: `src/session/session_io_read_parse_engine.c`, `src/session/session_io_read_parse_effects_panel.c`, `src/session/session_io_read_parse_track_clips.c`, `src/session/session_io_read_parse_track_fx.c`, `src/session/session_io_read_parse_master_fx.c`
  - UI split seams: `src/ui/clip_inspector_controls.c`, `src/ui/clip_inspector_waveform.c`, `src/ui/timeline_view_clip_pass.c`, `src/ui/timeline_view_grid.c`, `src/ui/timeline_view_controls.c`, `src/ui/effects_panel/spec_panel_render.c`
  - undo split: `src/undo/undo_manager_stack.c`
- Current worktree path normalization (staged, behavior-preserving):
  - `src/ui/overlay/layout_modal_overlays.c`
  - `src/ui/overlay/timeline_view_overlays.c`
  - `src/ui/overlay/timeline_view_runtime_overlays.c`
  - `src/render/adapters/timer_hud_adapter.c`

## Runtime/Verification Contract (Current)
- Build:
  - `make -C daw clean && make -C daw all`
- Scaffold smoke gate (non-interactive):
  - `make -C daw run-headless-smoke`
- Visual harness build gate:
  - `make -C daw visual-harness`
- Packaging gates:
  - `make -C daw package-desktop`
  - `make -C daw package-desktop-smoke`
  - `make -C daw package-desktop-self-test`
  - `make -C daw package-desktop-refresh`
  - `/Users/<user>/Desktop/soniCs.app/Contents/MacOS/daw-launcher --print-config`
- Release readiness gates:
  - `make -C daw release-contract`
  - `make -C daw release-bundle-audit`
  - `make -C daw release-verify APPLE_SIGN_IDENTITY="Developer ID Application: <Name> (<TEAMID>)"`
  - `make -C daw release-distribute APPLE_SIGN_IDENTITY="Developer ID Application: <Name> (<TEAMID>)" APPLE_NOTARY_PROFILE="cosm-notary"`

Stable test lane:
- `make -C daw test-stable`
- current composition:
  - `test-pack-contract`
  - `test-trace-contract`
  - `test-trace-async-contract`
  - `test-kitviz-adapter`
  - `test-kitviz-fx-preview-adapter`
  - `test-kitviz-meter-adapter`
  - `test-waveform-pack-warmstart`
  - `test-layout-sweep`
  - `test-data-path-contract`
  - `test-library-copy-vs-reference-contract`

Legacy test lane:
- `make -C daw test-legacy`
- current composition (known stale/failing under active migration):
  - `test-session` (link-time missing engine/session symbols)
  - `test-cache` (compile-time API mismatch in test call signature)
  - `test-overlap` (link-time missing engine symbols)
  - `test-smoke` (link-time missing engine symbols)
  - `test-shared-theme-font-adapter`

## Dependency Lane Snapshot
- Locked lane policy:
  - `third_party/` (vendored shared subtree lane)
  - `extern/` (compatibility/include lane only; no silent feature-lane drift)
  - `SDLApp/` (legacy top-level exception lane for framework glue)
- New app-level public entry APIs route through:
  - `include/daw/...`

## Temp/Runtime Ignore Snapshot
- temp lane is gitignored:
  - `tmp/`
- runtime/generated lanes are gitignored:
  - `config/runtime/`
  - `config/cache/`
  - legacy compatibility lanes:
  - `config/library_index.json`
  - `config/last_session.json`
  - `config/font_zoom_step.txt`
  - `config/projects/*.json`

## Data Path Contract Finalization
- DAW data-path contract upgrades (`P3`) close with a standardized lane commit title:
  - `DAW Data Path Contract Foundations`
- `P3-S0` through `P3-S5` are complete:
  - explicit runtime contract fields are active:
    - `input_root`
    - `output_root`
    - `library_copy_root`
  - runtime path persistence uses:
    - `config/runtime/data_paths.cfg`
  - ingest-mode matrix is now explicit:
    - library-pane external file ingest => `copy` into `library_copy_root`
    - timeline drop ingest => `reference` (no implicit duplicate copy)
  - output-root migration is active for project/session lanes with compatibility fallback to legacy `config/*`.

## Active Scaffold Migration State
- Private migration plan:
  - `../../docs/private_program_docs/daw/2026-03-27_daw_scaffold_standardization_switchover_plan.md`
- Active UI text/layout migration plan:
  - `../../docs/private_program_docs/daw/2026-03-28_daw_text_scaling_layout_migration_plan.md`
  - timeline phase now includes shared measured geometry for header controls + track-header hit regions (render/input synchronized)
  - timeline lane clip/body rect geometry is now shared between render and input hit/testing paths (removed hardcoded `+8/-16` lane body offsets)
  - timeline ruler/grid labels now suppress overlaps at dense spacing using measured text widths
  - clip inspector now derives row spacing and phase/normalize/reverse control rects from font metrics with shared render/input geometry
  - effects panel header/list/overlay layout now derives row and menu metrics from measured text heights, with clipped button/menu/title labels to prevent overflow at larger zoom
  - effects slot body/spec rendering now derives row/control/toggle sizing from measured text metrics and clips slot/spec labels/values so high zoom stays overlap-safe
  - effects EQ detail now shares measured selector/toggle/graph geometry between render/input, and meter detail now uses measured split/toggle/info-row sizing with clipped text labels
  - meter subtype renderers (correlation/levels/lufs/mid-side/vectorscope/spectrogram) now derive header spacing, stat-row placement, axis label placement, and meter paddings from font metrics with clipped text bounds
  - phase 4 baseline verification now includes `test-layout-sweep` (zoom `-4..+5` x small/medium/large windows) with effects header/list/detail + EQ/meter helper rect overlap/in-bounds assertions
  - repeated font zoom changes now use renderer-safe font cache invalidation to prevent text disappearing after many Cmd/Ctrl `+/-` cycles
  - post-scaffold font-size standardization lane is treated as complete; standardized closeout title is:
    - `Post-Scaffold Font Size Standardization`
- Baseline freeze:
  - `../../docs/private_program_docs/daw/2026-03-27_daw_s0_baseline_freeze_and_mapping.md`
- Completed phases:
  - `DAW-S0`, `DAW-S1`, `DAW-S2`, `DAW-S3`, `DAW-S4`, `DAW-S5`
- Next phase:
  - scaffold migration complete; connection-pass baseline closeout is complete
  - `DAW-CP0` through `DAW-CP5` are complete (routing map refresh + context/stage hardening + runtime dispatch seam extraction + update/render separation + deterministic wrapper teardown hardening + verification/docs/memory closeout)
  - optional future lane: `CP6+` deeper legacy-runtime extraction when prioritized

## Connection Pass Status (Current)
- top-level wrapper now owns explicit lifecycle stage state via `DawAppStage` and `DawAppMainContext`.
- deterministic stage transitions are guarded in `src/app/daw_app_main.c`.
- runtime handoff is routed through explicit dispatch seam (`daw_app_dispatch_runtime(...)`) before legacy runtime delegation.
- update-driven invalidation effects and render-decision derivation are now separated with explicit `DawUpdateDerivation` / `DawRenderDerivation` contracts in `src/app/main.c`.
- wrapper-owned lifecycle teardown now releases ownership flags in deterministic reverse order through centralized shutdown helpers in `src/app/daw_app_main.c`.
- DAW CP baseline closeout is complete for the current connection-pass contract.
- legacy runtime behavior remains intentionally centralized in `daw_app_main_legacy()` for parity; deeper extraction remains optional future work.

## Wrapper Contract State
- cross-program wrapper initiative status:
  - `W0` complete
  - `W1` complete for `daw`
  - `W2` complete for `daw`
- wrapper diagnostics normalization (`W2`) now includes:
  - function-context stage transition violation logging with expected/actual/next stage values
  - explicit wrapper error taxonomy logging at lifecycle boundary failures
  - dispatch summary exit tracking (`dispatch_succeeded`, `last_dispatch_exit_code`)
  - final wrapper exit summary line (`stage`, `exit_code`, dispatch summary, wrapper error code)
- execution note:
  - `../../docs/private_program_docs/daw/2026-04-02_daw_w1_w2_wrapper_hardening.md`

## App Packaging Status (Current)
- DAW packaging + release-readiness lane is complete with standardized target set:
  - `package-desktop`
  - `package-desktop-smoke`
  - `package-desktop-self-test`
  - `package-desktop-copy-desktop`
  - `package-desktop-sync`
  - `package-desktop-open`
  - `package-desktop-remove`
  - `package-desktop-refresh`
  - `release-contract`
  - `release-clean`
  - `release-build`
  - `release-bundle-audit`
  - `release-sign`
  - `release-verify`
  - `release-verify-signed`
  - `release-notarize`
  - `release-staple`
  - `release-verify-notarized`
  - `release-artifact`
  - `release-distribute`
  - `release-desktop-refresh`
- packaging assets/launcher:
  - `tools/packaging/macos/Info.plist`
  - `tools/packaging/macos/daw-launcher`
  - `tools/packaging/macos/bundle-dylibs.sh`
- launcher diagnostics:
  - `--print-config`
  - startup logs at `~/Library/Logs/DAW/launcher.log` (tmp fallback)
- release identity:
  - product app name: `soniCs.app`
  - bundle id: `com.cosm.sonics`
  - notarized artifact lane: `build/release/soniCs-<version>-macOS-stable.zip`
