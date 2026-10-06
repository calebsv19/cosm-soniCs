# S5 — Functional interaction and cohesive UI

Status: implementation started; see [S5 implementation ledger](S5-IMPLEMENTATION.md) for verified subsets and remaining work. Historical S1 editing-integrity debts are prerequisites within this phase, not silently completed work. The S4 closeout report keeps any unqualified physical or stress boundaries explicit.

## 1. S5.1 — Trustworthy application actions and editing history

1. Audit remaining frame-polled gestures and shortcuts after the S4 Play/Stop/Space correction, including text-focus isolation. Inventory each existing edit path: pointer drag, shortcut, inspector entry, context menu and undo/redo. Record the engine action, target identity, undo entry, rejection path and visible result.
2. Make multi-clip move/trim/duplicate and their history application all-or-nothing. Validate and prepare the whole action before publication; a failure leaves every target and the history cursor unchanged.
3. Replace remaining index/pointer-sensitive history targets with stable project-scoped identities. Invalidate stale actions across project replacement and distinguish removed targets from valid reordered targets.
4. Show pending/applied/rejected state for operations where publication is asynchronous; reflect engine readback rather than assuming a requested setter succeeded. Preserve the user's prior selection/state on rejection.
5. Use the same application action for mouse, keyboard and later automation entry points; avoid multiple implementations of one edit.

Acceptance: reorder tracks, remove targets, inject failure at each multi-object preparation boundary, undo/redo mixed audio/MIDI/automation/name edits, replace the project, then compare actual project contents with visible state. Multi-clip transform history now uses one atomic engine publication; live compound gestures and other history families remain under audit. No new creative feature is needed to complete this slice.

## 2. S5.2 — Transport and recording as an explicit workflow

1. Make play, pause, stop, seek and loop controls display requested versus applied state consistently; identify which playhead represents presentation, not render-ahead.
2. Show the recording destination, selected/armed track, source device, sample rate and capture status before recording begins.
3. Separate capturing, finalizing, importing, ready and failed-take states. Make cancel/recovery discoverable when an empty or failed take intentionally retains its capture state. A finalized file whose insertion fails must remain recoverable and visible.
4. Make stop/discontinuity/restart handling discoverable; surface underruns and device errors with actionable status instead of only diagnostic counters.
5. Verify recorded placement and listening on the chosen physical device; software clock tests cannot supply that acceptance.

Acceptance: start/stop while busy, repeated pause/seek/loop, record during edits, finalize/cancel/recover a take, reject an unavailable input, then reopen the project. Preserve files and prevent ambiguous duplicate takes.

## 3. S5.3 — Arrangement and inspector agree on one selection

1. Establish predictable selection, multiselection, focus and keyboard ownership across the timeline, track list and inspector.
2. Make move/trim/split/fade/duplicate gestures expose a clear preview, snapping rule and commit/cancel boundary.
3. Keep waveform/MIDI bounds, duration, gain, fades and inspector values synchronized after edits, zoom, undo and project restore.
4. Define usable minimum dimensions, scroll behavior and hit targets at small/large windows and Retina scale; text and handles must remain readable. Use consistent track rows, aligned clip edges, visible panel boundaries and predictable inspector placement to make the existing layout feel structurally connected.
5. Preserve the viewport and selection deliberately during pending import and track insertion/removal.

Acceptance: execute the same supported edit through a gesture, shortcut and inspector; verify equivalent project data and undo. Capture representative empty, sparse and dense arrangements at multiple sizes. Do not infer waveform faults from a single first frame or a low-amplitude fixture.

## 4. S5.4 — Mixer and effect-chain control integrity

1. Give track/master identity, mute/solo/gain/pan, selected chain and active effect distinct visible states.
2. Fix the observed compressor/limiter card crowding with consistent label/value/control bounds, minimum widths and deliberate overflow/scroll behavior.
3. Make add/remove/reorder/bypass and parameter reset/edit expose a clear target and reliable undo/rejection semantics.
4. Keep parameter values, units, ranges and DSP delay information consistent with supported engine behavior; avoid controls that imply unimplemented routing.
5. Preserve effect identity and analyzer selection through chain edits and project restoration.

Acceptance: reorder/remove active effects while playing, undo, resize, switch tracks, reject an invalid edit, and verify audio continuity plus parameter/chain readback. Screenshots establish layout; interaction and audio checks establish behavior.

## 5. S5.5 — Analysis that explains the sound accurately

1. Show the selected source and tap position, sample rate, scale, units and whether the displayed data is fresh, paused, missing or stale.
2. Label the existing spectrum/spectrogram as Hann-windowed tonal peak-amplitude dBFS using the Mid signal. Do not imply true peak, loudness, selectable stereo channels or integrated band energy.
3. Keep axes, cursors, legends and numeric readings stable and readable while resizing and switching source/effect selections.
4. Separate useful visual history controls from changes to DSP analysis semantics. Preserve worker-owned analysis and bounded publication.
5. Make meter clipping/hold/recovery and analyzer unavailability visually distinguishable from silence.

Acceptance: known tones, silence, stereo cancellation, clipping, source removal and project replacement; correlate displayed values with existing calibrated tests. New analysis modes require their own later DSP contract.

## 6. S5.6 — Project and background-operation visibility

1. Distinguish current project, unsaved changes, last successful save and failed/uncertain save outcomes. Keep load failures visible without dismissing the only recovery UI, and remove duplicate project entries observed in the isolated rehearsal.
2. Present import/export progress, cancellation and publication as separate states, with filenames/destinations and supported resource limits. The current 64 MiB per decoded/converted buffer admits about 175 seconds of stereo 48 kHz float audio; longer-file playback policy requires a separate media design, not a UI promise.
3. Make missing/changed media, decode admission failure and recording recovery actionable without losing the prior usable project.
4. Keep common file operations responsive; define which existing synchronous operations need a bounded application job before exposing them through automation.
5. Qualify one complete journey: import → edit → undo/redo → effects → record → export → save/reopen, including rejection/cancellation.

Acceptance: isolated fixture project, deterministic exported audio/project checks, UI state captures and user interaction. Successful file creation alone is not acceptance of the visible workflow.

## 7. Optional S5 automation adapter — after common actions exist

MCP is currently absent. Build it as a separately accepted adapter, not a second audio engine or a substitute for GUI/hardware testing.

1. Read-only project/revision/track/clip/effect/transport/diagnostic snapshots, including data age and stable target identity.
2. Owner-thread dispatch for transport and file operations; report operation IDs and pending/applied/failed/cancelled outcomes.
3. A small supported editing set sharing S5.1 validation, undo and transaction boundaries; reject stale project generations and removed targets.
4. One agent-driven isolated import/edit/analyze/export/reopen scenario with artifact verification, cancellation and stale-target tests before expanding the tool catalog.

## 8. Slice boundary rule

For each slice: (1) audit the actual current behavior, (2) agree the focused behavior changes, (3) implement the bounded subset, (4) run source/runtime/UI acceptance appropriate to the claim, and (5) return a concrete completion/remaining report. S5.1 should precede multiplying editing entry points; later surface order can follow real-use priorities. Physical acceptance and an installed/released package retain separate evidence identities.
