# soniCs Current Truth

Last source review: 2026-10-28. Main Edit remains a development source lane at VERSION 0.4.0; the public release remains 0.4.0. The dated sections below retain their original implementation and acceptance boundaries. Installed packages and historical release records do not qualify subsequent source changes.

## 2026-10-12 Main Edit timeline navigation

Vertical scrolling and a bounded right-side scrollbar now make lower tracks
reachable at large row heights. Visible Height buttons use existing shared
release ownership, and row zoom preserves a pointer anchor. Drawing, track/clip
hit geometry and discrete control clipping share the same viewport. A visible
selected row survives resizing; pane dividers retain 14-pixel grab zones.
See [navigation behavior and proof](timeline_navigation.md). Nested audio-bus
groups are implemented and verified (see [track groups](track_groups.md)); collection Load/Save is implemented through explicit folder scope and versioned checkpoints (see [collections](project_collections.md)) in the application-development
handoff. No composition was authored by this slice.

## Current creative-control state

At the 2026-10-28 source review, the official SDK adapter declares 70 local MCP tools through the opt-in same-user Unix bridge. Clients must discover current schemas rather than depend on a dated tool count. Common application actions provide guarded instruments, personal presets, isolated sound analysis, empty-project MIDI region creation/extension, note/pattern edits, expression transforms, MIDI arrangement, track/master effects and reusable effect chains. Complete project settings are embedded independently of personal library files. See [MCP usage](../tools/mcp/README.md), [effect chains/composition](effect_chains.md), [MIDI region creation](midi_regions.md), [expression](midi_expression.md), and [arrangement](arrangement.md).

Actual native App_Run acceptance covers creative MCP edits/history/receipts, processed composition capture, normal shutdown save and real restart with exact audio comparisons. These are local development results; client registration, installed app adoption and physical listening are separate.

Shared post-fader return processing and complete routing save/reopen now pass production signal, latency, rollback, ASan and actual native acceptance. Common exact-state routing actions and scoped undo/redo reserve complete history before publication. Guarded return creation and routing batches/history now have MCP adoption; native routing controls now adopt these common actions with staged percentages, explicit Refresh and guarded creation; the current effects tools edit serial track/master chains, including agent-created returns. See [routing](routing.md). Filtered voices now have per-note cutoff decay and retriggered LFO controls, with Sweep Bass, Filter Pluck and Moving Pad presets; all share native/MCP, library and project controls. Captured inspection previews now have direct finite current/reference listening through the existing output endpoint, with actual native input/painter and active dummy-endpoint qualification. The requested five-stage creative-control development sequence is implemented and verified; physical listening, installed package delivery and global MCP registration remain separate.

## 2026-10-09 Filtered synth and native MCP loop

Main Edit now contains the reconciled 0.4.0 source and the previously accepted
instrument controls/MCP slice. Filtered Keys and Filtered Pad add real per-note
low-pass state, cutoff/Q controls and automation through existing instrument
actions. Capabilities report fifteen presets and fourteen parameters. Native
MCP edits, exact history restoration and saved cutoff/Q readback pass. See
[filtered synth contracts, evidence and remaining sequence](filtered_synth.md).
Reusable personal presets now use a user library with complete sound settings embedded in projects; see [user presets](user_presets.md).

## 2026-10-08 Rounded oscillator family

Three additive presets (Rounded Keys, Rounded Pluck, Rounded Pad) share a
phase-aligned sine/triangle oscillator with brightness, detuned second-voice
blend, sub and continuous drive. Existing preset IDs/defaults/DSP are preserved.
All ten advertised parameters use existing UI actions and MCP operations;
modulation stays hidden because this family does not consume vibrato controls.
See [rounded synth design and evidence](rounded_synth.md). This is an oscillator
slice; it does not change the legacy vibrato calculation or add a filter.

## 2026-10-08 Instrument control refinement

Preset-specific supported controls, ADSR control order, log-time envelope knobs
and latched Shift fine dragging now share engine metadata. Instrument preview
plots use the actual isolated voice/ADSR kernel rather than illustrative formulas.
Original preset defaults, physical ranges, serialized values and automation
semantics are unchanged. The existing MCP tools expose supported controls and
additive control-scale metadata. The musical fixture proves actual distinct
rendered output and byte-identical undo/redo restoration. See
[instrument control contract](instrument_controls.md). Human listening, installed
package delivery and new synth models remain separate.

## 2026-10-06 Main Edit UI adoption

The installed runtime baseline was checkpointed at `fb832eb` and fast-forwarded
into persistent Main Edit before importing shared `09ff89a` in separate subtree
commit `9dc47a0`. Main Edit now adopts shared transport/menu button interaction,
focus and rounded measured presentation while preserving the existing direct
engine/project commands and product theme. Focused and stable headless gates
pass; actual dark/light frames and native Vulkan startup/resize/restart proof
are retained. Initial macOS adoption includes bounded text/modal hosts, five editor control
groups and their discrete track/snapshot/EQ/meter/spec exceptions are now adopted.
Fixed-pane composition, actual-loop window lifecycle and populated surface presentation are qualified. Thin slider visuals remain independent of generous hit targets; effect controls use single rounded painters. Human Main Edit comparison and separate release adoption remain next. See
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
- Public release version is `0.4.0`; later Main Edit engineering remains unreleased.
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

## Pane composition adoption (2026-10-06)

The sibling `pane_composition.c` adapter adopts immutable shared `09ff89a` /
kit_pane 0.5.0 shell, header and content geometry for the existing four fixed panes.
Product topology, ratios, minimum sizes, colors, fonts, specialized painters and
continuous gestures stay with Sonics. Ordinary divider resizing retains generous
hit regions and thin visuals without activating workspace authoring.

The native Vulkan clip helpers now operate on renderer state rather than being
no-ops. Pane painters receive shared region clips; nested library/effect/preview
and Load-list clips intersect the enclosing clip and restore its exact state.
One divider pass runs before modal overlays. Focus loss, hiding/minimizing, size
changes and modal/authoring takeover cancel active divider dragging; a held pointer
must release before another drag. Shared controls and active undo gestures retain
priority over sampled divider input. Last accepted pane geometry is retained.

Qualification used a fresh task build, geometry/hit/hidden-pane/clip/resize tests,
existing layout, editor, text, input and authoring tests, and stable headless smoke:

```sh
make BUILD_DIR=build/ui-panes-20261006 -j4
make BUILD_DIR=build/ui-panes-20261006 test-shared-pane-composition test-layout-sweep test-shared-editor-exceptions test-shared-editor-controls test-shared-text-focus test-input-delivery test-workspace-authoring-host test-workspace-authoring-profile build-native-editor-controls-proof run-headless-smoke
make BUILD_DIR=build/ui-panes-20261006 package-desktop-main-edit-refresh
```

Thirteen actual Vulkan frames per dark/light theme pass native validation. An exact
pixel containment check at 2x drawable scale proves all pixels outside an enclosing
clip remain unchanged while real pane content paints inside it. Evidence:
`_private_workspace_artifacts/ui_unification/sonics_panes_20261006`.
This qualifies fixed-pane composition, not generic pane trees or shared splitter
migration. Next: actual-loop fullscreen/window lifecycle, then a program-wide audit
and human comparison. Physical audio, OS IME and other-platform acceptance remain
separate. VERSION stays 0.3.0; Main Edit delivery does not promote canonical/release.

## Window lifecycle adoption (2026-10-06)

The shared kit_ui 0.18.0 window observer, logical coordinate contract, F11 desktop-fullscreen helper and opt-in lifecycle probe now operate in the actual application loop. Geometry, drawable, display, mode and visibility observations request swapchain updates; hidden/minimized windows suspend presentation while background work continues. Focused/presentable windows receive sampled input. Native transitions cancel discrete captures/preedit, finish accepted continuous edits through existing release/undo owners, and require pointer release. Text/meter caches wait for submitted GPU work before recovery or teardown.

Fresh build, window/pane/control/text/input checks and stable headless smoke pass. Final native dark/light runs qualify eight stages per theme (initial, resize, fullscreen, windowed, hidden, shown, minimized, restored), with twelve captures and continued rendering after controlled renderer rebuild, without Vulkan validation messages. This does not certify physical GPU loss, other display hardware/platforms, physical audio or OS IME. Evidence: `_private_workspace_artifacts/ui_unification/sonics_window_20261006`. Run `make BUILD_DIR=build/ui-window-20261006 test-shared-window-lifecycle`; use `CODEWORK_WINDOW_LIFECYCLE_PROOF=<isolated-output-dir>` only for explicit native qualification. VERSION remains 0.3.0. Next: populated surface audit and presentation refinement, then Main Edit comparison delivery.

## 2026-10-09 reusable user presets

Persistent Main Edit implements the five-step [user preset sequence](user_presets.md): versioned personal library, My Presets search/save/update, complete project-owned settings, finite note/chord auditions, and five guarded/read-only MCP additions with production standardized WAV previews. Library updates require current revisions and preserve a previous file; project settings remain independent. No new shared dependency, VERSION change, commit, release, Desktop installation or global client configuration is implied. Custom preset names/provenance are editor metadata; the full sound values are persisted in projects. General spectral analysis, project render and MIDI authoring remain later slices.

## 2026-10-09 sound inspection and comparison

Persistent Main Edit implements the five-step [sound inspection](sound_inspection.md) sequence: shared isolated production rendering/WAVs, original-level calibrated waveform/RMS/tonal plots, transient reference comparisons and finite auditions, plus two read-only MCP analysis tools (fourteen total). Calibration, exact render parity, SDK state preservation, native renderer/owner and three listening pairs are qualified separately. No project schema/shared dependency/VERSION change, commit, release, Desktop install or global MCP registration is implied. General project analysis and transactional MIDI authoring remain separate slices.

## 2026-10-09 transactional MIDI patterns

Persistent Main Edit delivers [copied MIDI snapshots and one-step append/replace actions](midi_authoring.md), a bounded tempo-aware major-key generator and a C-major Patterns popup. Common-action history is bound to generation/track/region identity and refuses stale replay. Atomic failure, musical timing, native renderer, production undo/redo and session-reopen checks qualify the local workflow. MCP remains fourteen tools; MIDI protocol adoption is the next boundary. No shared dependency, schema, VERSION, commit, release, installation or client-registration change is implied.


## 2026-10-09 MIDI MCP adoption

Main Edit adopts common MIDI actions through `get_midi_region`, `set_midi_notes`, `create_midi_pattern`, `undo_midi` and `redo_midi` (nineteen tools total). Exact copied snapshots back eight bounded endpoint-lifetime tokens; note/tempo pages fit existing transport limits. The immutable request ledger remains shared with instrument/preset edits. Official SDK, maximum pagination, receipt/state/history refusals, sanitizer, production audio and actual App_Run save/restart qualify the source workflow. See [MIDI MCP](midi_mcp.md). No VERSION, shared dependency/schema, commit, release, installation or global registration changes.

### Guarded MIDI region authoring

Main Edit adds five guarded structural MIDI tools, bringing the local MCP surface to 24. Agents can create a first region with an optional new trailing track, extend its right edge and replay structural history. See [region authoring](midi_regions.md); selected qualification is retained under `build/receipts/midi-regions-20261009/`. No installed app, VERSION, commit, release or client-registration change is implied.

### Effects authoring development

Main Edit implements five guarded effect tools, bringing MCP to 31 tools with composition analysis and captured preview adoption. Native composition inspection and reusable-chain storage now have focused qualification; library MCP/editor adoption, arrangement expansion and advanced routing/synthesis remain active work. See [effect chains](effect_chains.md).

Processed composition analysis and captured PCM previews now have MCP adapters, bringing the surface to 31 tools. Native Region inspection supports dry/processed capture, reference comparison and exact WAV saving. Reusable-chain storage and portable application pass all 54 registered effect types; library MCP/editor adoption, arrangement/expression and advanced routing/synthesis remain active work.

## 2026-10-10 Effects-library adoption and MIDI expression

Main Edit now exposes 40 MCP tools. `list_effect_presets`, `get_effect_preset`, `save_effect_preset` and `apply_effect_preset` adopt the complete portable chain library through common guarded actions. The native Chains browser uses shared name editing, explicit Save As/Update/Refresh, stale-state refusal and one-step recall. Native Vulkan browser/save frames and enclosing clip checks pass separately from SDK/library/project independence tests. See [effect chains](effect_chains.md).

`transform_midi` and native Patterns shortcuts add tempo-aware swung quantize, velocity scaling and seeded bounded variation with unchanged note durations and complete state/history guards. See [expression](midi_expression.md). Full arrangement operations and advanced routing/synthesis remain active requirements. This is development source, not an installed delivery or full five-stage completion.

The actual native expression/library loop now qualifies normal save and real restart with exact note/settings/audio readback after private library removal. A directly blocking session precision defect was repaired: authored floats now round-trip with `FLT_DECIMAL_DIG` significant digits instead of six decimal places and near-zero truncation. Focused session serialization, stable regression and sanitizer-backed MCP reopen checks pass.

Guarded MIDI arrangement now supports common/MCP duplicate, move, right-edge trim and delete batches with full captured content guards and one undo step. Native MIDI duplicate/delete shares this action; dedicated native Arrange fields and actual-window painter/App_Run acceptance now pass. See [arrangement](arrangement.md). Advanced routing and synthesis remain active goal work.

## Sampled violin foundation — 2026-10-11

Main Edit source adds the CC0 VSCO 2 solo bowed-vibrato instrument: 15 recorded roots, soft/strong layers, G3–C7, held-note crossfaded sustain, note-off release and smoothed Dynamics automation. Source/audio/preset/session checks are separate from guarded Desktop installation and subjective listening acceptance. See `sampled_violin.md` for exact controls, asset identity and acceptance checks.

### Violin stabilization — 2026-10-11

Main Edit refines bowed sustain with 1.88–6.58-second recorded-root loops, correlation-aware overlap compensation, matched dynamic layer levels and click-protected S-curve onset/release. Original recording bytes and foundation metadata remain intact. The same editable demo supplies unnormalized before/after WAVs; source/test, package/install and subjective listening acceptance remain separate.

Main Edit adds sampled trumpet (MIDI 53–84, two layers) and clarinet (50–89, three layers) through fixed shared acoustic-bank playback. Stabilized violin demo bytes remain identical. Source/test, packaged/installed and listening acceptance remain separate; see sampled_winds.md.


Wind stabilization now favors steadier sustain loops and eases adjacent-layer timbre transitions, including the clarinet middle layer. Original source samples and violin playback remain unchanged; see `sampled_winds.md`.


Main Edit adds sampled tenor saxophone (MIDI 44–88, 23 roots, two layers) using the fixed sampler at native 48 kHz. Existing voices retain their assets, defaults and DSP behavior; see `sampled_saxophone.md`.


Sax stabilization coordinates layer loops, aligns sustained phases and compensates shared blend energy. Shorter upper-register loops are documented in `sampled_saxophone.md`; original assets and other voices remain intact.

Main Edit includes sampled flute (MIDI 60–93, nine roots) and cello section (MIDI 36–77, 13 roots), each with two recorded layers. These banks use optional pitch-center correction and coordinated loops; existing voices retain their playback paths. See `sampled_flute_cello.md`.

Flute and cello stabilization adds bounded time-varying layer interference correction with unchanged recorded onsets and note envelopes. Three cello loop boundaries are refined; original recordings and the four older voices are preserved. See `sampled_flute_cello.md`.

Sampled Oboe and French Horn extend the existing acoustic sampler with paired recorded dynamics and held-note expression. Complete audition chords follow each bank's playable range. See `sampled_oboe_horn.md` for ranges, provenance, preparation and verification.

Oboe/French horn stabilization refines five qualified common loops and regenerates their bounded layer-energy curves; tighter all-pitch blend and note-transition checks cover the existing sampler. See `sampled_oboe_horn.md`.

Sampled Bassoon and Tenor Trombone add two paired-layer recorded sustain banks through the existing sampler, supported controls and guarded preset/automation tools. See `sampled_bassoon_trombone.md` for ranges, source provenance and verification.

Sampled Acoustic Piano, Viola Section, Double Bass and Violin Section extend the acoustic palette. Piano has finite velocity-selected strikes and voice-specific automation; bowed strings reuse sustain/dynamics playback. Audition notes follow each recorded range. See `sampled_piano_strings.md`.

Piano expression extension: [piano_expression.md](piano_expression.md) describes the three piano-only controls, binary region/live sustain, 88 string modes, recorded key/pedal mechanics, 32-field v6 presets and session v25. Older preset versions 1–5 and session v24 remain readable; existing non-piano voices keep their behavior.

## Compact track UI — 2026-10-12

Aligned compact headers, stable FX access/live meters and derived combined MIDI/audio group overviews are implemented in Main Edit. Fold-only selection/history now permits playback while topology editing retains its stopped guard. See [behavior and proof](compact_track_ui.md).

## Header mixer and lane density — 2026-10-12

Child/group volume and pan now have compact native header fields with scalar-only undo/redo and live playback publication. Two-pixel gutters and subtle lane tints reduce padding while preserving region/selection geometry. See [header mixer](header_mixer.md).

## 2026-10-12 Track/group MCP controls

Main Edit adds four guarded header tools (54 total): copied label/gain/pan/mute/solo reads and one-field edits/scoped undo/redo for source children and group/return buses. Native naming shares stable identity, exact-name guards and reserved history. Playback is permitted while recording/conflicting owners retain priority. See [header controls](header_mixer.md) and the refreshed [music capability table](agent_music_capabilities.md). Native collection Load/Save remains the project-file workflow. Version stays 0.4.0; source is uncommitted Main Edit development.

## 2026-10-12 Dedicated EQ and captured channel measurements

Main Edit adds four dedicated native EQ tools (58 total), common guarded native/MCP EQ history and per-channel calibrated captured measurements. The session reader preserves raw/escaped Unicode equivalently instead of substituting question marks. See [dedicated EQ and captured metering](dedicated_eq_mcp.md). Source remains dirty/uncommitted at 0.4.0. This slice prepares a verified candidate only; the open normal Main Edit app and user music/runtime stay untouched.

## 2026-10-13 Dense text rendering repair

Main Edit's local clipped-label cache now retains textures until recorded GPU draws complete, includes renderer/raster density in its identity, and invalidates labels when fonts are replaced. Its retained working set is 512 labels to reduce dense arrangement churn. Source remains uncommitted at 0.4.0; the corrected development package is prepared separately for review, without replacing or closing the user's active Main Edit app.

## 2026-10-13 Immutable mix diagnostics first slice

Main Edit source adds read-only exact mix guards, full-context private production graph captures and immutable MCP pages for calibrated per-channel sample peak/RMS, time buckets, stable routing/processing identities and base instrument/gate provenance. Pre-child-fader audio, per-insert audio/GR history, per-note effective automation, asset content fingerprints, loudness/true peak and spectral/band analysis remain explicitly unavailable. See [schema, limits and verification](mix_diagnostics.md). Version remains 0.4.0; source and candidate proof do not install/restart the normal app or edit the music.

## 2026-10-13 Serial insert diagnostics

Mix capture schema v2 adds one effect page per node/position with calibrated input/output boundaries and actual sample-wise, per-channel pre-makeup Compressor/Limiter reduction, accumulated into immutable 32-bin histories. Each tap is aligned by graph arrival and cumulative serial latency, including lookahead. Disabled native dynamics return zero reduction; unsupported processors remain explicit. The existing three read-only MCP tools now accept strict independent effect paging. Headless proof is independent of Desktop GUI acceptance; Desktop refresh leaves the app closed. Pre-child-fader audio, per-note effective automation/tails, spectra and standards-qualified loudness/true peak remain future work. Version remains 0.4.0 and existing music is untouched.

## 2026-10-13 Time-sampled frequency analysis

Mix capture schema v3 retains 32 latency-aligned Hann/FFT observations for each processed child/group/return/master output. Per-channel powers, Nyquist-bounded bands and exact window bounds are read through independent MCP frequency cursors. These sparse short-window observations preserve original levels, DC and channel independence; they are not a continuous spectrogram, perceptual masking verdict, LUFS or true peak. Existing music, source controls and live DSP remain unchanged. Qualification and package verification are headless; the Desktop refresh leaves Main Edit closed. See [frequency contract](mix_diagnostics.md).

## 2026-10-13 Note endings and source tails

Mix diagnostics now copy actual prepared production key/damper/choke/mono/legato fences and evaluated ending controls. Original note indices survive private export compaction. MIDI source activity is measured before child/region scalar gain and inserts; processed node activity remains latency aligned. Threshold/range-qualified aggregate tails are not attributed to overlapping individual notes, and muted/zero-gain sources can be explicitly unobserved. Independent MCP note paging is read-only and immutable. See [ending/tail contract](mix_diagnostics.md). Music is unchanged; headless verification and closed Desktop refresh remain the delivery workflow.

## 2026-10-13 Passage LUFS and interpolated true peak

Main Edit adds immutable processed child/group/return/master M/S/I loudness and per-channel true peak to the existing three guarded read-only MCP mix tools. Mono/stereo 44.1/48/96 kHz is explicitly mapped; other configurations report unavailable. Measurements integrate only the selected range (30 s maximum), with cold loudness filters at its start, complete ungated M/S windows, BS.1770 gates and a true-peak-only terminal flush. Validation uses reconstructed EBU definitions and independent FFmpeg, not the inaccessible official ZIP or a full EBU Mode certification. Existing live LUFS inserts remain placeholders. Music, VERSION and prior dirty work are preserved. See [limits and qualification](mix_diagnostics.md#8-selected-passage-lufs-and-true-peak).

## 2026-10-13 Whole-song LUFS and loudness range

Main Edit adds `capture_mix(whole_song=True)` with duration derived from exact authored regions, explicit tail, ten-minute arrangement/thirty-second tail bounds and lightweight immutable child/group/return/master pages. Programme I/M/S/true peak and EBU 3342 2023 LRA (complete 3 s windows at 10 Hz, absolute/relative gates and 10th/95th percentiles) are available. Under-60-second results are provisional; silence/insufficient windows return null. The detailed 30-second capture contract, authored music, VERSION and existing dirty work are preserved. This is synchronous offline analysis, not a live meter or full-song export job. See [current boundaries](mix_diagnostics.md#9-whole-song-integrated-loudness-and-lra) and [functional priorities](mixing_mastering_status.md).

## Group and send automation — 2026-10-13

Main Edit adds project-frame group-volume and post-fader send curves, native Routing → Auto point editing, exact persistence and shared guarded routing undo/redo/MCP. Linear, hold and smooth curves use absolute linear gain with explicit baseline/hold boundaries. Editing is stopped; playback/rendering applies latency-aligned curves without allocations. See [contracts and controls](mixer_automation.md).

## Selected effect-parameter automation — 2026-10-13

Main Edit qualifies Gain dB and constant-power Pan position curves on child/group/return/master inserts. Native FX Auto point editing, native project persistence, shared effect-chain history and existing guarded MCP tools use the same complete physical curves. Inserts follow incoming and preceding-processor latency; static Gain ramps remain intact. See [contracts and proof](effect_parameter_automation.md).

Gain-matched A/B listening is available through the native effects toolbar and four guarded MCP tools. Captures are ephemeral, preserve authored gains/history and use equal all-channel RMS with attenuation-only sample-peak protection. See [A/B listening](ab_listening.md).

Asynchronous whole-song WAV export now uses a captured production graph and shared worker, with native/MCP format/tail choices, progress, cancellation and verified no-overwrite output. See [export contracts](song_export.md).

Qualified live 400 ms per-channel/stereo/mono compatibility windows now cover child/group/return/master output, with native scope controls and guarded output-only stereo/mono monitoring. The approximate live LUFS view is hidden behind an explicit qualification notice; offline validated LUFS remains available. See [live metering contracts](live_stereo_metering.md).

Compressor/Limiter controls now have qualified non-consuming live gain-reduction feedback in native headers and `get_effect_chain.dynamics_feedback`, separate from authored edit tokens. Compressor detection remains independent by channel; Limiter is sample-peak, not true-peak. See [dynamics controls and feedback](dynamics_feedback.md).
