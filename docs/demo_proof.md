# soniCs Demo Proof Contract

Last updated: 2026-06-19

This page defines the public R6 demo-proof surface for `soniCs`.
Repository/source-level commands still use the `daw` key.

## Canonical Automated Proof

Run:

```sh
make -C daw run-headless-smoke
```

Expected success line:

```text
daw headless smoke passed (non-interactive)
```

Expected proof summary:

```text
demo-proof: build=all tests=test-stable ui=not-launched package=separate manual=separate
demo-proof: package lane -> make -C daw package-desktop-self-test
demo-proof: manual microphone proof remains outside automated gates
```

What this proves:
- the DAW builds through the normal `all` target
- the current deterministic `test-stable` lane passes
- MIDI model/editor, instrument render, timeline contract, audio
  capture/recording, package-contract, layout, trace, config diagnostics,
  library path, and shared-adapter coverage remain in the non-interactive proof
  lane according to the current test target wiring

This is the first command to run for a fresh checkout proof-of-life. It does
not launch the interactive app, require a microphone, open the packaged app, or
depend on machine-local audio libraries. The `demo-proof:` summary lines point
to the separate package and manual proof lanes without making either one part
of the automated proof.

## Focused Follow-Up Proofs

Use these when the first proof fails or when checking a narrower lane:

```sh
make -C daw test-midi-editor-shell
make -C daw test-audio-capture-device
make -C daw test-audio-recording
```

`visual-harness` is build-only readiness for the interactive shell binary:

```sh
make -C daw visual-harness
```

It does not execute the UI and is not an unattended runtime proof.

`visual-artifact` is the source-render first-frame proof:

```sh
make -C daw visual-artifact
```

Expected final line:

```text
visual-artifact ready: visual_artifacts/daw_first_frame.bmp
```

This launches the development binary in `DAW_VISUAL_ARTIFACT_ONCE=1` mode,
writes one rendered frame, verifies the artifact is nonempty, and exits. The
generated `daw/visual_artifacts/` directory is ignored by Git. This route needs
a live display/render session; display, Vulkan, shader, or permission failures
mean the visual proof could not be produced.

## Package Proof

Run:

```sh
make -C daw package-desktop-self-test
```

Expected success lines:

```text
self-test: ok
package-desktop-self-test passed.
```

What this proves:
- the packaged launcher and app binary exist and are executable
- `Info.plist`, bundled Vulkan/MoltenVK libraries, required shaders, fonts, and
  public/default config resources are present
- bundled resource hygiene keeps generated runtime/session/project state, local
  media-library metadata, local user audio, and private planning docs out of
  the app bundle
- the launcher can seed and report its runtime shader/config lanes

Runtime writes:
- the packaged launcher resolves writable runtime state under
  `~/Library/Application Support/DAW/runtime` with a tmp fallback
- launcher logs resolve under `~/Library/Logs/DAW/launcher.log` with a tmp
  fallback
- in sandboxed agent runs, this command may require approval for packaged
  launcher runtime writes

Useful package diagnostics:

```sh
build/targets/macOS-arm64/dist/soniCs.app/Contents/MacOS/daw-launcher --print-config
```

This prints the resolved app resources, runtime root, log file, shader root,
Timer HUD settings, Vulkan ICD files, and MoltenVK dylib path.

## Default-Off Memory Check

Run only when checking the fisiCs memory-check lane:

```sh
make -C daw memory-check-audit
```

This is not part of the one-command demo proof. It depends on the local fisiCs
toolchain path and writes reports under `daw/build/memory_check/`.

## Manual Packaged-App Proof

Manual microphone/app validation remains separate from automated gates. The
latest live `soniCs.app` proof recorded selected-track recording,
record-armed solo setup, live waveform preview, and play/pause-gated capture as
functioning well enough for the current audio-recording lane.

Checklist for the manual packaged-app proof:
- build or refresh the package with `make -C daw package-desktop-refresh`
- launch the packaged `soniCs.app`, not the development binary
- select an audio destination track and arm recording from the timeline
- validate the record-armed solo setup routes capture to the intended track
- record with transport running, pause/stop, then confirm capture does not
  append while transport is stopped
- confirm the live waveform preview appears during capture
- confirm the finished take inserts as an audio clip on the selected/armed
  track and remains undoable/session-visible
- treat the result as human evidence only; do not mark automated R6 gates
  failed solely because live microphone proof has not been rerun

Current evidence from the 2026-06-19 live packaged-app proof satisfies this
checklist for the present R6 boundary. Follow-up feature work such as hard
track typing, durable record-arm controls, input-device picker UI, or external
MIDI input should start as fresh planning/proof slices, not as part of the R6
demo proof pass.
