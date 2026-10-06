# soniCs

`soniCs` is a C-based desktop digital audio workstation built with SDL2 and a shared Vulkan renderer.

The repository and source-level program key remain `daw`.

The [initial audit](docs/audits/2026-09-19/AUDIT.md) and [S1–S6 improvement plan](docs/improvement/PLAN.md) describe the runtime-first improvement chain. The [S1 closeout ledger](docs/improvement/S1-CLOSEOUT.md) reconciles implemented command delivery, render ownership, individual edit transactions, and lifecycle behavior with their software acceptance evidence. Application-wide compound edits, remaining identity/feedback integration, and physical-device acceptance are explicitly deferred; the original broad S1 contract is not fully complete. [S2.1 atomic saves and previous-save recovery](docs/improvement/S2-SAVING.md) are now implemented and software-verified; [S2.2 coherent capture and restore](docs/improvement/S2-CAPTURE-RESTORE.md) now prepares a complete replacement before retiring the current project. [S2.3/S2.4 clocks and transport](docs/improvement/S2-CLOCKS-TRANSPORT.md) now separate render-ahead, callback-delivered, and estimated presentation positions, with defined pause/stop/seek/loop behavior. [S2.5 recording/durability](docs/improvement/S2-RECORDING-DURABILITY.md) and [S2.6 diagnostics](docs/improvement/S2-DIAGNOSTICS.md) now add timestamped take checkpoints, checked media publication, and software runtime telemetry. Device activation, physical timing acceptance, and recovery-browser UI remain separate work; [S4.6](docs/improvement/S4-STREAMING.md) now bounds recording/export pipeline memory.

## Current State

- Stage: alpha, actively developed.
- Build output: `build/targets/macOS-arm64/toolchains/clang/bin/daw_app`.
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
- Capture callbacks remain log-free. Main-thread recording status reports checkpointed
  duration, capture gaps, and observed software alignment; output health appears in
  the timeline status and existing bounded optional worker timing logs.

## Repository Layout

- `src/` and `include/`: DAW implementation and headers.
- `config/`: runtime config and public fallback template.
- `assets/`: runtime assets (no bundled public audio samples).
- `tests/`: unit/smoke/stress test targets.
- `docs/`: focused technical and release documentation (start at `docs/README.md`).
- `third_party/codework_shared/`: vendored shared core/kit/runtime modules.

Recording now preserves capture gaps and timestamped recovery checkpoints, publishes synced WAV media before clip insertion, and retains failed takes for retry. Recover journals with `build/targets/macOS-arm64/toolchains/clang/bin/daw_app --recover-take JOURNAL OUTPUT.wav`. Runtime diagnostics expose software output gaps, render-budget overruns, queue depth, and command age through the timeline status and optional timing logs. See [S2.5 recording/recovery](docs/improvement/S2-RECORDING-DURABILITY.md) and [S2.6 diagnostics](docs/improvement/S2-DIAGNOSTICS.md) for exact guarantees; [S4.6 streaming](docs/improvement/S4-STREAMING.md) adds bounded pipelines while hardware acceptance and full-file media import/cache remain separate.

S3.1 now makes the existing audio fade shapes agree across editor preview, playback, and bounce, with transactional curve publication. Linear timing/endpoints and serialized curve IDs are preserved; existing nonlinear selections now produce their displayed shape. See [fade parity](docs/improvement/S3-FADE-PARITY.md).

S3.2 calibrates the existing spectrum/spectrogram as flat tonal-amplitude dBFS of the mid signal, labels their actual tap locations, and resets history across missing windows/transport changes. See [analysis calibration](docs/improvement/S3-ANALYSIS-CALIBRATION.md). The pre-repair S3.3 findings are retained in the [entry audit and limiter impulse probe](docs/improvement/S3-DYNAMICS-AUDIT.md).

S3.3 repairs limiter lookahead, aligns the existing parallel track paths, corrects both compressor knee curves, and exposes applied reduction separately from RMS delta. Exact-range bounce removes reported common DSP delay. See [the dynamics contract and evidence](docs/improvement/S3-DYNAMICS.md) for transition/reset policies and proof limits.

S3.4 adds sample-counted Gain and mixer transitions plus zero-latency bypass blending; explicit transport and latency-change resets retain their documented boundaries. S3.5 validates supported WAV structures and replaces linear import conversion with an anti-aliased offline converter shared by native/fallback decode paths. S3.6 gives existing notes a gate/release lifecycle, bounds audition voice retirement, applies existing volume/pan lanes to instruments, and reduces aliasing in supported oscillator components. See [controls](docs/improvement/S3-CONTROL-TRANSITIONS.md), [media conversion](docs/improvement/S3-MEDIA-CONVERSION.md), and [instruments](docs/improvement/S3-INSTRUMENT-LIFECYCLE.md) for acceptance and limits. S3.7 now captures an independent export plan, preserves live playback state, supports bounded preroll/tails and explicit normalization, and writes deterministic requested WAVs without an implicit sidecar. See [export policy and acceptance](docs/improvement/S3-EXPORT.md). S4.1 now provides an [initial runtime workload audit](docs/improvement/S4-RUNTIME-AUDIT.md) and [opt-in optimized measurements](tests/performance/README.md). S4.2/S4.3 now reduce inactive audio/MIDI work, and S4.4a separates published meter values from large DSP histories. See [the implementation ledger and acceptance evidence](docs/improvement/S4-IMPLEMENTATION.md). S4.4b now avoids complete source/DSP preparation for gain, pan, mute, and solo edits. S4.5 adds complete busy-worker timing and explicit queue targets; compatibility buffering remains the default. See [edit/deadline contracts](docs/improvement/S4-EDITING-DEADLINES.md). S4.6 adds a recording journal worker, bounded recent waveform previews, streamed finalization/recovery, and two-pass streamed exports with Escape cancellation and bounded pack overviews. See [streaming contracts and measurements](docs/improvement/S4-STREAMING.md). [S4.7a–d media preparation/cache](docs/improvement/S4.7-MEDIA.md) now move metadata/content probing and interactive import decoding to bounded owned background work; default app build policy remains unchanged.

The [capture-clock follow-up](docs/improvement/S4-CAPTURE-CLOCK.md) corrects the input gaps found during S4.6 qualification: anchored capture now checks a separately published transport epoch when detailed clock observations are busy. Nine strict software captures passed with zero missing input/output; physical qualification remains separate. The [S4.7 implementation](docs/improvement/S4.7-MEDIA.md) covers metadata probing, prepared-cache adoption, bounded background jobs, project-generation rejection, and library/recording/bounce insertion. Ctrl/Cmd+Escape cancels pending imports while saved files remain intact.

[S4.8 analyzer efficiency](docs/improvement/S4.8-ANALYSIS.md) now prepares worker-owned Hann/oscillator coefficients, preserving the logarithmic tonal calibration with approximately 4× lower optimized kernel CPU and about 3× lower unoptimized kernel CPU in matched local measurements. Consumer backlog and transform timing are observable. [S4.9 sustained assessment](docs/improvement/S4.9-ACCEPTANCE.md) now records 8/8 workflow passes, 7/8 continuity passes and retained strict timing failures. Real empty/populated GUI frames render, but physical-device, long-session memory and interactive qualification remain open. See [current functionality and next steps](docs/improvement/DAW-STATUS-AND-NEXT-STEPS.md) for S5 and the unimplemented MCP adapter.

## Runtime improvement checkpoint

The local runtime work now has a [bounded S4 closeout](docs/improvement/S4-CLOSEOUT.md) and [proposed S5 functional slices](docs/improvement/S5-FUNCTIONAL-SLICES.md). Read the closeout for actual continuity, timing and hardware acceptance limits; this is not a release or universal real-time certification.
