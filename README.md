# soniCs

`soniCs` is a C-based desktop digital audio workstation built with SDL2 and a shared Vulkan renderer.

The repository and source-level program key remain `daw`.

## Current State

- Stage: alpha, actively developed.
- Build output: `build/daw_app`.
- Platform focus: macOS-first local desktop workflows.
- Public version: `0.2.0`.
- License: Apache-2.0.

## Implemented Today

- Multi-track timeline editing with clip selection, drag, trim, and snap behavior.
- Transport controls (play/stop/seek), loop region controls, zoom, and grid options.
- Library browser for local audio assets (`.wav` and `.mp3`).
- Track mute/solo handling and core timeline/track interaction loop.
- Effects panel and parameter control path for the current built-in effects set.
- MIDI regions with timeline creation/selection, piano-roll editing, QWERTY
  audition/recording, note clipboard commands, quantize, velocity editing,
  built-in instruments, per-region overrides, track-level defaults, instrument
  automation, and bounce-to-WAV coverage.
- Audio recording with an SDL capture wrapper, transport-gated recording,
  active waveform preview, selected/armed-track placement, derived target-role
  timeline status, MIDI-only target refusal, undoable inserted audio clips, and
  session round-trip coverage.
- Session/project persistence with deterministic startup fallback:
  1. `<output_root>/projects/last_project.txt`
     (legacy fallback: `config/projects/last_project.txt`)
  2. `<output_root>/last_session.json`
     (legacy fallback: `config/last_session.json`)
  3. `config/templates/public_default_project.json`
  4. fresh in-memory bootstrap
- Runtime roots are explicit and persisted with the session contract:
  - `input_root`
  - `output_root`
  - `library_copy_root`
- Runtime diagnostics toggles for engine/cache/timing logging, plus bounded
  parser, restore, data-path, package, capture-device, recording, and opt-in
  engine graph diagnostics.
- Target-aware desktop packaging and Intel `x86_64` release artifact flow for `soniCs`.
- An isolated persistent Main Edit package profile for ongoing development:
  `soniCs Main Edit.app`, a separate bundle/runtime/log identity, and embedded
  exact-source provenance.

## Current Gaps

- Buses/sends are not implemented yet.
- Effects are functional but still a basic subset.
- Hard audio-vs-MIDI track typing, external MIDI input, and user instrument
  preset storage remain future work.
- Durable record-arm controls, input-device picker UI, and hard
  audio-vs-MIDI track typing remain future fresh-slice work before any external
  MIDI input lane.
- General alpha-level UI/engine glitches can still occur.

See [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) for the current issue list.

## Build and Run

Prerequisites:

- C11 compiler (`cc`/clang)
- SDL2 + SDL2_ttf
- Vulkan loader/dev headers (with Metal interop on macOS)

Shared runtime/modules are vendored in-repo at:

- `third_party/codework_shared/`

Commands:

```bash
make
make run
```

## Proof / Demo Commands

The public proof contract is in [`docs/demo_proof.md`](docs/demo_proof.md).

Fast source/docs orientation:

```bash
rg -n "MIDI regions|Audio recording|run-headless-smoke|visual-harness" daw/README.md daw/docs
```

Deterministic automated proof:

```bash
make -C daw run-headless-smoke
```

Expected success line:

```text
daw headless smoke passed (non-interactive)
```

Expected summary lines:

```text
demo-proof: build=all tests=test-stable ui=not-launched package=separate manual=separate
demo-proof: package lane -> make -C daw package-desktop-self-test
demo-proof: manual microphone proof remains outside automated gates
```

Focused follow-up proofs:

```bash
make -C daw test-midi-editor-shell
make -C daw test-audio-capture-device
make -C daw test-audio-recording
```

Visual source-render proof:

```bash
make -C daw visual-artifact
```

Expected final line: `visual-artifact ready: visual_artifacts/daw_first_frame.bmp`.
The generated `daw/visual_artifacts/` directory is ignored by Git.

Package proof:

```bash
make -C daw package-desktop-self-test
```

Expected success lines include `self-test: ok` and
`package-desktop-self-test passed.` The packaged launcher may write runtime
state under `~/Library/Application Support/DAW/runtime` and logs under
`~/Library/Logs/DAW/launcher.log`, with tmp fallbacks.

Main Edit package proof:

```bash
make -C daw package-desktop-main-edit-self-test
```

This produces and validates the isolated development bundle without replacing
or launching either Desktop app. See
[`docs/main_edit_worktree.md`](docs/main_edit_worktree.md) for the persistent
lane and integration gates.

Default-off audit proof:

```bash
make -C daw memory-check-audit
```

This is not part of the one-command demo proof and depends on the local fisiCs
toolchain path.

Manual packaged-app microphone proof remains separate from automated gates. The
latest live `soniCs.app` proof recorded selected-track recording, record-armed
solo setup, live waveform preview, and play/pause-gated capture as functioning
well enough for the current audio-recording lane. The manual checklist lives in
`daw/docs/demo_proof.md`; future hard track typing, durable record-arm controls,
input-device picker UI, and external MIDI input should start as fresh
planning/proof slices rather than block `run-headless-smoke`.

### Shared Subtree Update

```bash
git -C daw fetch shared-upstream main
git -C daw subtree pull --prefix=third_party/codework_shared shared-upstream main --squash
```

Rebuild check:

```bash
make -C daw clean && make -C daw
```

## Scaffold Lane Policy

- `third_party/codework_shared/` is the vendored shared-subtree lane and remains the DAW dependency source of truth for shared modules.
- `extern/` is a compatibility/include lane only; new DAW feature implementation should not silently expand this lane.
- `SDLApp/` is a documented legacy exception lane for SDL framework glue; new app/domain logic should stay in `src/` and `include/`.
- New app-level public entry APIs should route through `include/daw/...`.
- Temporary files belong in `tmp/`, and runtime-generated config/cache lanes stay gitignored.

## Tests

Available targets:

```bash
make run-headless-smoke
make visual-harness
make visual-artifact
make test-stable
make test-legacy
make test-cache
make test-overlap
make test-midi-editor-shell
make test-audio-capture-device
make test-audio-recording
make test-track-role
make test-smoke
make test-kitviz-adapter
make test-waveform-pack-warmstart
make test-kitviz-fx-preview-adapter
make test-kitviz-meter-adapter
make test-shared-theme-font-adapter
make package-desktop-self-test
make package-desktop-main-edit-self-test
make memory-check-audit
```

`run-headless-smoke` is aggregate non-interactive smoke coverage.
`visual-harness` is build-only readiness for the interactive shell binary, not
an unattended runtime proof.
`visual-artifact` launches a one-shot development binary run, writes an ignored
first-frame image artifact, and exits.
`test-stable` is the current deterministic migration gate lane.
`test-legacy` runs known stale/failing test targets to keep breakage visible while those lanes are being repaired.

## Public Release Hygiene

- This repo intentionally ships without bundled user audio content in `assets/audio`.
- Runtime-generated caches and local session/project state are excluded from public commits.
- Fallback serialization behavior is documented in `docs/SERIALIZATION_FALLBACK.md`.
- Security model and safe-usage guidance are documented in `SECURITY.md`.

## Diagnostics Baseline

- Invalid `engine.cfg` entries and loop-policy environment values emit bounded
  diagnostics without changing defaults or clamp behavior.
- Startup restore logs the candidate set and final loaded/fallback source.
- Project and data-path filesystem failures include path role, OS reason, and
  fallback/source context.
- Packaged launcher diagnostics expose runtime root, launcher log path, shader
  root, Timer HUD settings path, Vulkan ICD files, and MoltenVK dylib path
  through `package-desktop-self-test` and launcher `--print-config`.
- Audio capture and recording diagnostics are lifecycle/failure summaries only;
  capture callbacks, drain loops, per-frame paths, and audio-thread work remain
  log-free.

## Repository Layout

- `src/` and `include/`: DAW implementation and headers.
- `config/`: runtime config and public fallback template.
- `assets/`: runtime assets (no bundled public audio samples).
- `tests/`: unit/smoke/stress test targets.
- `docs/`: focused technical and release documentation (start at `docs/README.md`).
- `third_party/codework_shared/`: vendored shared core/kit/runtime modules.
