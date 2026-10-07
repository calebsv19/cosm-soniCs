# soniCs Current Truth

Last updated: 2026-10-06 (checkpoint and Main Edit transport UI adoption; historical release claims retain their original scope)

## 2026-10-06 Main Edit UI adoption

The installed runtime baseline was checkpointed at `fb832eb` and fast-forwarded
into persistent Main Edit before importing shared `09ff89a` in separate subtree
commit `9dc47a0`. Main Edit now adopts shared transport/menu button interaction,
focus and rounded measured presentation while preserving the existing direct
engine/project commands and product theme. Focused and stable headless gates
pass; actual dark/light frames and native Vulkan startup/resize/restart proof
are retained. This is partial UI adoption: bounded text/modal hosts, five editor control
groups and their discrete track/snapshot/EQ/meter/spec exceptions are now adopted.
Pane composition and full window lifecycle remain future slices. See
[shared UI rollout](shared_ui_rollout.md). Canonical functional UI and stable
Desktop package are not promoted by this work; VERSION remains 0.3.0.

## Program Identity
- Repository directory: `daw/`
- Public product name: `soniCs`
- Primary runtime entry:
  - `src/app/main.c` -> `daw_app_main_run()`
  - wrapper shell: `include/daw/daw_app_main.h`, `src/app/daw_app_main.c`

## Current Development Checkpoint — Runtime Improvements

The runtime implementation below is safely checkpointed locally as `fb832eb`, not a shipped release. The [runtime improvement plan](improvement/PLAN.md) is separate from the earlier scaffold `DAW-S*` numbering.

- S1 has a bounded engine-foundation closeout with explicit compound-edit, undo-identity, rejection-feedback, and physical-acceptance deferrals.
- S2's six software slices cover save durability, coherent project capture/restore, software clocks/transport, recording recovery, and diagnostics at their documented boundaries.
- S3.1–S3.3 closeouts cover fade parity, analysis calibration, and repaired dynamics/processing delay.
- [S3.4](improvement/S3-CONTROL-TRANSITIONS.md), [S3.5](improvement/S3-MEDIA-CONVERSION.md), and [S3.6](improvement/S3-INSTRUMENT-LIFECYCLE.md) now document control transitions, supported import/conversion, and instrument gate/release/alias improvements. Their receipts bind tests to their recorded source snapshots; S3.7 subsequently changes shared engine paths. Final application build and selected normal/ASan/TSan checks passed; physical listening/device acceptance is not claimed.
- [S3.7 export](improvement/S3-EXPORT.md) is complete at its software boundary: independent authored capture, private DSP reset, bounded preroll/tails, explicit normalization, and deterministic requested WAV writing. Stable/legacy, selected ASan/TSan, and final build checks passed; see [the current export receipt](improvement/evidence/s3-export.json). S3.7's stop before S4 was honored; the later S4.1 audit is summarized below.
- [S4.1 runtime audit](improvement/S4-RUNTIME-AUDIT.md) now records 30 optimized workload configurations (90 selected samples), five unoptimized comparisons (15 samples), source findings, and proposed S4 slices. This adds opt-in benchmark tooling, not engine optimizations or a new default build policy. Selected optimized export/instrument/transport checks and final default `make` passed. This is the historical baseline; subsequent S4 implementation is summarized below. Physical-device, long-session, and GUI performance remain unqualified.

- [S4 implementation](improvement/S4-IMPLEMENTATION.md) now adds track-local audio source bounds, per-block MIDI candidates with prepared base pitch, and compact published meter banks. Exact rendering/reference checks, regression, sanitizer, and matched optimized workloads cover these software boundaries. The audit baseline sparse 32×32 median fell from 0.588416 to 0.058624 ms in the recorded S4.3 scheduling run; sparse MIDI 1×1024 fell from 0.053250 to 0.006000 ms. Published meter storage saves 435,456 bytes per double-buffered capacity slot/master. [S4.4b/S4.5](improvement/S4-EDITING-DEADLINES.md) now add compact ordered mixer updates, indexed source-history transfer, whole busy-worker timing, and callback-aware 2..32-block queue targets (default 32). Config/project round trips preserve requested policy. [S4.6](improvement/S4-STREAMING.md) now adds worker-owned recording checkpoints, 65,536-frame recent previews, streamed recovery/finalization, and two-pass streamed file exports with cancellation and bounded waveform packs. The [capture-clock follow-up](improvement/S4-CAPTURE-CLOCK.md) now retains anchored samples when diagnostic reads are busy, using an independently published valid transport epoch. Nine strict live dummy captures—including three 30-second intervals—reported zero missing input/output. [S4.7a–d](improvement/S4.7-MEDIA.md) now add background metadata/content probing and decoding, four owned request slots, cooperative cancellation, prepared cache adoption, generation-safe insertion, warm reuse and periodic retired-plan collection. Library drops and interactive recording/bounce insertion use pending/completed/failed states. [S4.8 analyzer compute](improvement/S4.8-ANALYSIS.md) now reuses worker-owned window/oscillator coefficients and exposes consumer backlog/timing counters, preserving calibrated results with measured lower CPU. Its live probes retained analyzer throughput but found occasional whole-workload deadline misses and two output-gap runs; those entered [S4.9 sustained assessment](improvement/S4.9-ACCEPTANCE.md). That assessment is complete with 8/8 workflow, 7/8 continuity and 0/8 strict timing passes. A heavy 96 kHz case lost 8,320 output frames; long-session RSS and physical/interactive qualification remain open. Empty and populated GUI frames rendered successfully, with visible effects-layout crowding. Full-file pinned media and explicit synchronous restore/offline APIs remain limits. These runtime changes are included in checkpoint `fb832eb`.

## Current Shipped State
- The managed Vulkan adoption is committed locally as `5fa5d6e` (shared
  subtree refresh) and `f472ae8` (soniCs integration) against canonical
  shared commit `60084f90564105983c7c74e862a299d8b6775347`, with
  `vk_runtime 0.6.0` and `vk_renderer 1.3.1`. The existing SDL/Vulkan
  presentation path now uses the renderer's embedded runtime ownership. The
  dedicated rollout proof verifies validation-clean startup, resize, renderer
  restart, deterministic readback/capture, and 2.0x Retina drawable scaling
  (`1440x900` then `1800x1120`) on Apple M2. This is committed source truth,
  but not a version bump, release, Registry promotion, Linux proof, or
  compute-path adoption; DAW audio, transport, persistence, and UI semantics
  remain app-owned and unchanged.
- Core seam decomposition wave is landed across app/engine/input/session/ui/undo lanes.
- Workspace Authoring WAP4 is operator-accepted as the second bounded
  presentation-profile proving host after IDE: DAW uses the shared session and
  compatibility vocabulary through local adapters while retaining its fixed
  four-surface solver, runtime gate, drawing, and WAPP path policy. Pane mode
  drafts only Library/Inspector visibility, selected focus, and existing
  transport/library/mixer ratios; it supports explicit Apply/Cancel and
  fail-closed WAPP save/preview without changing audio, projects, or sessions.
- Data-path contract foundation (`P3`) is complete with explicit runtime path fields and persistence.
- Release/desktop packaging lanes are complete through the shared target-contract flow.
- MEW1 portability support is implemented in the persistent
  `codex/daw-main-edit` lane: `soniCs Main Edit.app` uses bundle identifier
  `com.cosm.sonics.main-edit`, separate `DAW-Main-Edit` runtime/log namespaces,
  the generic embedded build-identity schema, a source-mutation guard, and the
  required build/self-test/guarded-refresh targets. This remains local
  development proof, not a version, release, Registry, publication, or
  deployment claim.
- Intel `x86_64` packaging passed local gates after launcher runtime shader-lane hardening.
- Public release version is now `0.2.0`.
- MIDI regions are first-class engine/session objects with timeline creation/selection, piano-roll editing, QWERTY audition/recording, note clipboard/duplicate commands, quantize, velocity editing, bounce-to-WAV, and session round-trip coverage.
- Built-in MIDI instruments now use grouped factory presets, per-region overrides, track-level instrument defaults, and instrument parameter automation for region-local and inherited track-level lanes.
- Audio recording now has a DAW-local SDL capture wrapper and recording coordinator that arms from timeline `R`, captures only while transport is moving, previews the active waveform with role-aware status text, reports active/error recording status in the timeline, refuses MIDI-only audio targets with a clear status message, finalizes to its unique `recordings/take-*.wav`, and inserts the result as a normal undoable/session-persisted audio clip on the selected empty, audio, or mixed track.
- Latest manual packaged-app proof recorded selected-track recording,
  record-armed solo setup, live waveform preview, and play/pause-gated capture
  as functioning well enough for the current audio-recording lane.
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
  - `make -C daw package-desktop-main-edit-self-test`
- Audio capture and recording diagnostics stay outside capture callbacks,
  drain loops, per-frame paths, and audio-thread work.
- Engine graph rebuild summaries are opt-in through the existing engine logging
  path rather than always-on runtime output.

## Verification Contract
- Managed Vulkan presentation proof:
  - `make -C daw vulkan-rollout-contract`
  - `make -C daw vulkan-rollout-self-test`
  - the self-test is display-backed and proves validation, runtime/device
    identity, readback/capture, resize, 2.0x Retina scale, and restart
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
  - `make -C daw package-desktop-main-edit-self-test`:
    validates exact generic source/package identity, the isolated Main Edit
    bundle identifier, runtime/log namespaces, architecture, resources, and
    signature without installing or launching the GUI
  - `make -C daw package-desktop-main-edit-refresh`:
    host-required guarded refresh that refuses to replace a running Main Edit
    app and cannot target canonical `soniCs.app`
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
- `docs/main_edit_worktree.md` is the program-local MEW1 runbook. The
  persistent Main Edit lane is retained by default after adoption; recycling
  requires clean/reachable/process-free evidence.

## Current Boundary
- Preserve seam decomposition stability and data-path contract correctness.
- Current audio/MIDI implementation is paused at a solid current state. Future
  hard audio-vs-MIDI track typing, durable record-arm controls, input-device
  picker UI, user preset storage, and external MIDI input should begin as fresh
  planning/proof slices rather than as active S24 work.
- Keep launcher/runtime shader-copy hardening aligned with the packaged Vulkan/runtime contract.
- Treat generic pane trees, dynamic module providers, and broader runtime
  controls as future platform decisions, not as unimplemented DAW WAP4 work.
- Current security posture remains trusted local desktop use. R4 hardened the
  current local trust boundaries, but broader untrusted-project or sandboxed
  runtime safety still needs a fresh audit before being claimed.

## History and Deep Lane References
- Full lane history is in:
  - `/Users/calebsv/Desktop/CodeWork/docs/private_program_docs/daw/`
- This file is the compressed public current-state contract.

## S4 bounded closeout

The [S4 closeout](improvement/S4-CLOSEOUT.md) completes the bounded software follow-up: best-effort render-worker priority with refusal/restart diagnostics, 20 repeated ownership lifetimes, four sustained profiles, and acceptance-discovered transport/modal input corrections. All four profiles passed continuity; only two passed the strict timing gate, so universal zero-miss timing remains unqualified. Native dummy-audio save/reopen and silent default CoreAudio callback delivery passed; listening and microphone alignment remain separate. [S5 functional slices](improvement/S5-FUNCTIONAL-SLICES.md) begin with action/history integrity and truthful workflow feedback.

## S5 implementation boundary

The [S5 ledger](improvement/S5-IMPLEMENTATION.md) records partial action/history work: atomic compound transforms and previews, guarded generated-track placement history, retained neighbor content and topology for slide drops, and persistent save/load failure dialogs. Full S5.1 is not complete: remaining compound trim/split entry points, remaining history identities and native gesture acceptance remain open. These later source changes do not inherit the S4 binary performance qualification automatically.

Later S5.1 checkpoints add transactional duplicate/delete/paste, key-event undo/delete and edit-repeat suppression, atomic MIDI left trim, complete selection remapping for single-clip sorting, and pre-reserved numeric inspector history with checked frame conversion and retained rejected input. Inspector rename/drag history, visible failure explanations and native interaction acceptance remain incomplete; the ledger separates each verified subset from those open behaviors.

The current gesture audit confirms trim and ripple movement are separate modes. Audio left trim now publishes bounds/position atomically, and ripple movement includes downstream MIDI clips through stable target identities. A new ripple-trim gesture is not implied.

Timeline selection duplicate/delete now publishes complete audio/MIDI actions with one reserved history entry and explicit selection restoration. Ctrl/Cmd-D uses event-local modifiers; native shortcut and gesture rehearsal remains pending.

Clipboard paste now prepares complete audio/MIDI content and required topology before one publication and one reserved undo entry, including guarded generated-track retirement and empty-project restoration. Missing media/preparation rejection preserves the prior authored project. Native acceptance remains pending.

## 2026-10-06 bounded text/modal adoption

Main Edit now adopts the pinned kit_ui 0.18.0 text editing/presentation contract in
six existing owner families and shared Load/Cancel focus. Product publication,
validation, retry and cancel policies remain local. Fresh compile, targeted real
owner tests and native dark/light Vulkan overlay checks qualify this subset.
Remaining editor controls, pane composition and actual-loop fullscreen lifecycle
are separate rollout slices. Native OS IME and physical audio acceptance are not
claimed. See `docs/shared_ui_rollout.md`. VERSION remains 0.3.0; no release action.

### Editor controls rollout (2026-10-06)

Five discrete-control groups now share release/cancellation/focus through sibling
`editor_controls` adapters: library modes, timeline toolbar, MIDI editor,
instrument navigation/presets and effects header/slot/overlay controls. Existing
command owners and continuous gestures remain local. The common button frame
uses shared rounded tokens; product palette, status and geometry remain owned by
Sonics. See `docs/shared_ui_rollout.md` for exact coverage and retained exceptions.


### Discrete exceptions checkpoint (2026-10-06)

The Main Edit control surface now includes track-header mute/solo, snapshot
mute/solo/presets, EQ selectors/band toggles, meter modes/rack palettes and spec
boolean/enum/native-beats controls. Product painters and direct engine/undo owners
remain intact. Generic detail hit regions are excluded from specialized views;
menus suppress covered controls. The exact spec clip is shared across painting
and input. See [the current contract](shared_ui_rollout.md) for gates and limits.
Next: pane composition, actual-loop fullscreen, then program-wide rollout review.
