# Directory: src/app

Purpose: Application bootstrap and SDL event loop integration.

## Files
- `bounce_region.c`
  - Allocates bounce filenames under the active library root and inserts successful bounces as normal audio clips on a newly appended `Bounce` track when track creation succeeds.
- `main_bounce.c`
  - Orchestrates offline bounce range selection, progress reporting, WAV/pack writes, library refresh, and bounce-region insertion. Uses the streamed engine export path for a single deterministic PCM16 file; no implicit float sidecar. Existing UI uses cold exact-range export with normalize-if-clipping, two-pass progress, and Escape cancellation; bounded optional pack/insertion outcomes remain separately reported. See [S3.7](../../docs/improvement/S3-EXPORT.md).
- `main.c`
  - `handle_input`: Feeds the current SDL event into the input manager when available.
  - `handle_update`: Ensures layout sizing is up to date and advances input state when work is due.
  - `handle_render`: Clears the renderer, draws panes/controls/overlays, and presents the frame.
  - Wake-loop callbacks: Defines urgent-work checks, render-cadence timeout selection, wake-event filtering, background tick, render-gate, and diagnostics hooks consumed by `SDLApp` wake-blocked loop execution.
  - Input invalidation routing: Pointer events now invalidate targeted panes via pane hit-testing; global/layout events still invalidate all panes for correctness.
  - Async producer bridge: Worker-thread producers post typed main-thread messages (`daw_mainthread_message_post`) that coalesce wake signaling and drive targeted UI invalidation in the background tick.
  - Gate diagnostics mode: `DAW_LOOP_GATE_EVAL=1` with `DAW_SCENARIO=idle|playback|interaction` emits pass/fail threshold checks from loop diagnostics windows.
  - Loop diagnostics JSON mode: `DAW_LOOP_DIAG_FORMAT=json` (or `DAW_LOOP_DIAG_JSON=1`) emits schema-1 `LoopDiag` lines for cross-program sleep/wake calibration parity.
  - Gate harness: run `daw/tools/run_loop_gates.sh` to execute all gate scenarios and emit a summarized `pass/fail/inconclusive` report with scenario logs.
    - headless validation: set `HEADLESS=1` for no-display/no-swapchain loop gate checks.
  - Visual artifact proof: `DAW_VISUAL_ARTIFACT_ONCE=1` requests a first-frame
    capture and exits after the artifact is written or fails.
  - Quick commands:
    - `make -C daw loop-gates` (default profile, inconclusive => exit 2)
    - `make -C daw loop-gates-strict` (strict profile, inconclusive => failure exit)
  - `main`: Loads config, restores the last session from `config/last_session.json` (or seeds defaults), initialises UI subsystems, configures wake-loop policy, runs the SDL framework loop, and auto-saves the session on shutdown.
- `visual_artifact_proof.c`: Env-gated one-shot first-frame capture used by the
  R6 `visual-artifact` target.
- Shutdown stops engine producers before destroying UI wake/message/job infrastructure.
- Recording failures retain captured audio for Finish retry or explicit cancellation/recovery. Cancel quiesces the capture callback before releasing its queue; failed capture startup clears armed state while preserving its error. SDL dummy tests cover both raw and clock-aligned concurrent capture.
- `audio_recording.c`: DAW-local audio recording coordinator. It owns the capture queue, worker-owned timeline journal drain, bounded recent preview, streamed WAV finalization path, media registry registration, and normal audio-clip insertion while leaving shortcut/UI policy to input modules.

Recording setup checks engine arming acceptance before advertising an active take. Rejection releases the prepared queue and reports a retryable error; device-backed setup also closes its newly opened endpoint. The recording test injects arming rejection and proves cleanup/retry.

Audio takes retain a stable target-track identity. Finalization resolves its current index, or reports a removed target while retaining the captured take without insertion. Reusing the former array index does not redirect an existing take.

Space now pauses/resumes at the callback-delivered timeline cursor; the Stop button returns to frame zero. Timeline and transport presentation use the bounded callback-based clock estimate rather than the worker's render-ahead cursor. Physical output latency and capture alignment are not inferred from that estimate.

`audio_recording.c` now transfers timestamped capture packets, preserves overflow intervals as silence, checkpoints a recovery journal on drain, and retains failed takes. Timeline takes stop accepting audio across transport discontinuities and require a non-looping project-rate capture endpoint. `main.c --recover-take JOURNAL OUTPUT.wav` recovers a verified prefix without opening the desktop. See [S2.5](../../docs/improvement/S2-RECORDING-DURABILITY.md).

S4.6 moves actual timeline checkpoint I/O off the UI loop, joins the storage worker before finalization/cancel, and publishes fixed preview snapshots. Raw/synthetic callers retain explicit synchronous draining. The recent waveform represents only the newest 65,536 frames while clip extent follows the complete take. File bounce uses an anonymous spool and bounded pack overview; completed-WAV insertion still uses the full-file media cache. Separate queue/clock observations now include `clock_continuity_frames`: an anchored take retains samples through busy diagnostic reads only when its independently published epoch is still current. Transport changes halt the take. See [the clock correction](../../docs/improvement/S4-CAPTURE-CLOCK.md). See [S4.6](../../docs/improvement/S4-STREAMING.md).


S4.7 `media_import.c` owns application-lifetime requests across project candidates. Library drops, interactive recording finish and bounce insertion publish asynchronously; saved WAVs survive failed/canceled insertion. Ctrl/Cmd+Escape cancels pending requests. Raw/headless recording without this owner and explicit offline APIs retain synchronous behavior. See [S4.7](../../docs/improvement/S4.7-MEDIA.md).
