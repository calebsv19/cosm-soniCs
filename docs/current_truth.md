# soniCs Current Truth

Last updated: 2026-06-19

## Program Identity
- Repository directory: `daw/`
- Public product name: `soniCs`
- Primary runtime entry:
  - `src/app/main.c` -> `daw_app_main_run()`
  - wrapper shell: `include/daw/daw_app_main.h`, `src/app/daw_app_main.c`

## Current Shipped State
- Core seam decomposition wave is landed across app/engine/input/session/ui/undo lanes.
- Data-path contract foundation (`P3`) is complete with explicit runtime path fields and persistence.
- Release/desktop packaging lanes are complete through the shared target-contract flow.
- Intel `x86_64` packaging passed local gates after launcher runtime shader-lane hardening.
- Public release version is now `0.2.0`.
- MIDI regions are first-class engine/session objects with timeline creation/selection, piano-roll editing, QWERTY audition/recording, note clipboard/duplicate commands, quantize, velocity editing, bounce-to-WAV, and session round-trip coverage.
- Built-in MIDI instruments now use grouped factory presets, per-region overrides, track-level instrument defaults, and instrument parameter automation for region-local and inherited track-level lanes.
- Audio recording now has a DAW-local SDL capture wrapper and recording coordinator that arms from timeline `R`, captures only while transport is moving, previews the active waveform, finalizes to `recordings/recording*.wav`, and inserts the result as a normal undoable/session-persisted audio clip on the selected track.
- Latest manual packaged-app proof recorded selected-track recording,
  record-armed solo setup, live waveform preview, and play/pause-gated capture
  as functioning well enough to move the next audio lane to recording UX polish
  and audio-vs-MIDI track typing.
- R4 trusted-local security hardening now covers unsafe session/runtime write
  roots, library import/rename path components, destructive package/release
  helper destinations, and public package/release artifact hygiene.
- The current mixed-track model still allows audio and MIDI regions on the same track; hard audio-vs-MIDI track typing remains future work.
- Diagnostics now cover invalid config/env values, startup restore candidates,
  data-path/project filesystem context, package launcher config, audio capture
  lifecycle/failures, recording coordinator begin/finish/cancel outcomes, and
  opt-in engine graph rebuild summaries.

## Structure
- Required lanes: `docs/`, `src/`, `include/`, `tests/`, `build/`
- Active subsystems:
  - `app`, `audio`, `config`, `core`, `effects`, `engine`, `export`, `input`, `render`, `session`, `time`, `ui`, `undo`
- Include strategy remains include-dominant with small private-header surface in `src/`.

## Runtime and Data Path Contract
- Explicit runtime roots are active (`input_root`, `output_root`, `library_copy_root`).
- Runtime path persistence is normalized in runtime config lanes.
- Ingest-mode and library copy-vs-reference contract is explicit and test-covered.
- Startup reopen order is explicit:
  1. `<output_root>/projects/last_project.txt`
     (legacy fallback: `config/projects/last_project.txt`)
  2. `<output_root>/last_session.json`
     (legacy fallback: `config/last_session.json`)
  3. `config/templates/public_default_project.json`
  4. fresh in-memory bootstrap
- Media-placement roots are also ordered explicitly:
  - imported/source browsing resolves from `input_root`
  - session and recording persistence resolve under `output_root`
  - library copy targets resolve under `library_copy_root` before falling back
    to `output_root`

## Diagnostics Contract
- Config and loop-policy diagnostics are bounded parser-owner messages; invalid
  values keep the existing default, clamp, or partial-parse semantics.
- Startup restore diagnostics report the ordered candidate set and the final
  loaded/fallback source without changing restore order.
- Project and data-path filesystem diagnostics include the path role, OS
  reason, and fallback/source context.
- Package diagnostics are exposed by:
  - `make -C daw package-desktop-self-test`
  - `build/targets/macOS-arm64/dist/soniCs.app/Contents/MacOS/daw-launcher --print-config`
- Audio capture and recording diagnostics stay outside capture callbacks,
  drain loops, per-frame paths, and audio-thread work.
- Engine graph rebuild summaries are opt-in through the existing engine logging
  path rather than always-on runtime output.

## Verification Contract
- Demo proof:
  - `docs/demo_proof.md` defines the public R6 proof contract.
  - `make -C daw run-headless-smoke` is the canonical non-interactive proof
    command for a fresh checkout.
  - expected success line:
    `daw headless smoke passed (non-interactive)`
  - expected summary lines start with `demo-proof:` and point to the separate
    package and manual proof lanes
  - package proof and manual packaged-app microphone proof are separate lanes.
  - `make -C daw visual-artifact` is the source-render first-frame visual proof
    and writes `visual_artifacts/daw_first_frame.bmp` under an ignored
    artifact root.
  - the manual packaged-app microphone checklist is human-run evidence for
    selected-track recording, record-armed solo setup, live waveform preview,
    and play/pause-gated capture; it is not an automated R6 gate.
- Build/harness:
  - `make -C daw clean && make -C daw all`
  - `make -C daw run-headless-smoke`:
    aggregate non-interactive smoke coverage
  - `make -C daw visual-harness`:
    build-only readiness; does not execute the interactive DAW shell
  - `make -C daw visual-artifact`:
    display-backed first-frame proof artifact at
    `daw/visual_artifacts/daw_first_frame.bmp`
- Stable tests:
  - `make -C daw test-stable`
- Legacy tests:
  - `make -C daw test-legacy`
- Packaging/release gates:
  - `make -C daw package-desktop`
  - `make -C daw package-desktop-smoke`
  - `make -C daw package-desktop-self-test`:
    validates the packaged launcher, binary, plist, bundled resources,
    bundled public-resource hygiene, codesign/architecture checks, runtime
    shader lanes, runtime config root, launcher log path, Timer HUD settings
    path, Vulkan ICD files, and MoltenVK dylib path; expected success lines
    include `self-test: ok` and `package-desktop-self-test passed.`
  - `make -C daw package-desktop-refresh`
  - `build/targets/macOS-arm64/dist/soniCs.app/Contents/MacOS/daw-launcher --print-config`:
    prints the resolved app contents/resources paths, log file, runtime root,
    shader root, Timer HUD settings, Vulkan ICD files, and MoltenVK dylib path
  - `make -C daw release-contract`
  - `make -C daw release-bundle-audit`
  - `make -C daw release-verify ...`
  - `make -C daw release-distribute ...`
- Default-off audit lanes:
  - `make -C daw memory-check-audit` is a fisiCs memory-check audit lane, not
    part of the one-command demo proof.

## Dependency and Runtime Policy
- `third_party/` remains vendored shared subtree lane.
- `extern/` and `SDLApp/` remain compatibility lanes by policy.
- Temp/runtime generated lanes remain ignored and normalized.
- Public app bundles include only allowlisted public/default config resources
  and `assets/audio/README.md`; generated runtime/session/project state, local
  library-index metadata, local user audio, and private planning docs are
  excluded and audited.

## Current Boundary
- Preserve seam decomposition stability and data-path contract correctness while expanding MIDI/audio features.
- Current audio workflow boundary is recording UX polish and hard
  audio-vs-MIDI track typing before external MIDI input.
- Keep launcher/runtime shader-copy hardening aligned with the packaged Vulkan/runtime contract.
- Current security posture remains trusted local desktop use. R4 hardened the
  current local trust boundaries, but broader untrusted-project or sandboxed
  runtime safety still needs a fresh audit before being claimed.

## History and Deep Lane References
- Full lane history is in:
  - `/Users/calebsv/Desktop/CodeWork/docs/private_program_docs/daw/`
- This file is the compressed public current-state contract.
