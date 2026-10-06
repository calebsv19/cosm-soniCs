# S3.7 — Isolated, deterministic offline export

Status: complete at the documented software boundary (2026-09-21). Scope is the existing serial track/master renderer and WAV bounce; stop before S4. Source-bound acceptance is recorded in [the S3.7 receipt](evidence/s3-export.json).

Subsequent checkpoint: [S4.1's audit](S4-RUNTIME-AUDIT.md) has since been authorized and completed; S4-unstarted statements below record the original S3.7 closeout boundary.

## 1. Contract and implementation plan

1. Capture accepted authored sources, mixer settings, automation, EQ, and effects into an independently owned export plan on the control thread. Pin immutable media and omit live audition. Do not stop/restart playback, publish a live revision, consume transport commands, or update live meters/analyzers.
2. Reset only export-owned DSP. The default is a cold start at the requested half-open range `[start, end)`. Optional bounded preroll supplies preceding project context without including it in output. Authored targets, not transient live ramps, define export controls.
3. Keep the existing exact-range bounce default. An options API supports a fixed appended tail and explicit peak normalization. At the selection end, audio inputs stop and active MIDI gates close; optional tail rendering includes their release and downstream effects, without triggering later notes/clips. Existing earlier region boundaries remain unchanged. Freeze end-boundary instrument automation during the appended release. Account for and trim common DSP delay separately from preroll and tail.
4. Treat normalization as an explicit option; the legacy wrapper keeps its prior normalize-only-if-over-unity behavior. Publish the output buffer only after successful finite rendering. Invalid ranges, preparation/allocation failures, or invalid DSP output leave caller output and live state intact.
5. Write the requested WAV through the existing durable writer with a fixed dither seed for repeatable PCM16 bytes. Provide explicit float-WAV writing without an implicit sidecar. Route the existing app bounce through the same writer; retain separately reported optional pack/insertion behavior.
6. Verify repeatability, cold/preroll semantics, exact endpoints and DSP compensation, bounded MIDI/FX tails with no later source leakage, normalization, live/idle isolation, progress-callback snapshot stability, allocation failure cleanup, deterministic files, and write failure preservation. Run focused tests, stable/legacy regression, relevant sanitizers, and final `make`.

## 2. Reuse and boundaries

Reuse existing prepared-source ownership, immutable media pins, the common serial mixer, and durable WAV publication. Shared `core_io`/`core_pack` remain byte/container infrastructure; `core_time`, queue/scheduler/worker modules do not define DAW range/gate/tail semantics. New export policy remains DAW-local (`reuse-deferred` for a shared exporter); no shared module/version/adoption change is needed.

Export remains synchronous on the control thread. [S4.6](S4-STREAMING.md) now routes file/app exports through a bounded two-pass disk spool, cancellable progress, and chunked durable WAV writing; the explicit buffer APIs remain RAM-backed. Live playback can continue, but concurrent CPU/storage pressure is not a realtime deadline guarantee. No background-job UI, arbitrary routing graph, automatic infinite-tail detection, or device/listening certification is implied. The engine must remain alive through progress callbacks.

## 3. Implemented API and limits

`engine_bounce_range_to_buffer_with_options` captures an independent prepared source/mix plan. Live playback, its DSP histories, clock epoch, worker, meters, and analysis publications are untouched. Live audition and transient recording-arm intent are excluded. Source media remains pinned even if a progress callback deletes the original clip. The owner engine must remain alive; capture and rendering belong on its control thread.

`EngineBounceOptions` specifies preroll and appended tail in project-rate frames, each capped at 60 seconds. Preroll is clipped at frame zero. Output contains exactly `end - start + tail_frames` frames after separate DSP latency trimming. Default/legacy wrappers use zero preroll, zero tail, and normalization only when the peak exceeds unity. Explicit zero-initialized options preserve gain. The caller supplies an output with `data == NULL`; failures leave its metadata unchanged. Progress reports processing work, including preroll and latency flushing, starting at zero and ending at the reported total.

Audio inputs stop at the selection boundary. MIDI notes crossing the boundary close their gates; release and effects can continue for the requested fixed tail. Later notes and clips do not enter. Earlier region ends remain hard bounds. Instrument automation freezes at the last frame before the selection end during appended release. A fixed tail can truncate long decays; this is not an automatic tail detector or a fade guarantee. Lookahead can make earlier output depend on the allowed future tail, so exact and tail renders are not universally prefix-identical.

The common mixer retains the existing serial source/track FX/EQ/pan/delay-compensation/master order, with live observers disabled for export. Nonfinite output or output beyond the existing absolute-amplitude safety bound of 64 fails export. Existing tiny-sample sanitization remains shared with live mixing. Repeatability is qualified for the same build, inputs, configuration, and processing block size, not universal bit identity across platforms or every effect/block-size combination.

`engine_bounce_write_wav` explicitly selects PCM16 or float32. PCM16 uses fixed-seed TPDF dither; float32 preserves rendered headroom. PCM16 quantizes/clips over-unity samples if normalization was disabled. The app and legacy file helper write only the requested PCM16 WAV; the previous implicit `.f32.wav` sidecar is removed. The existing UI keeps the exact-range legacy policy; advanced preroll/tail/float options are API capabilities. Optional app pack creation and bounce insertion retain their separately reported outcomes.

The existing durable writer is reused: prepublication failures preserve the old destination, but a directory-sync failure can leave a visible publication whose durability is uncertain. No stronger all-failure rollback guarantee is claimed.

## 4. Acceptance and remaining boundaries

| Check | Result |
| --- | --- |
| Focused export/MIDI/dynamics | Passed; repeatable buffers/files, cold/preroll ranges, release/FX tails, delay compensation, normalization, authored snapshot isolation |
| Failure injection | Passed at 53 preparation-allocation boundaries; no partial caller buffer or leaked media pin |
| Live isolation | Passed with SDL dummy callback and running worker; output is independent of audition and recording-arm intent |
| Stable and legacy test lanes | Passed on final implementation |
| AddressSanitizer | Export, MIDI render, dynamics, and live mixer ownership passed |
| ThreadSanitizer | Export, live mixer ownership, and transport clock passed |
| Application build | Final `make` passed |

The [receipt](evidence/s3-export.json) records exact commands, source/log hashes, and proof limits. These are software checks, not physical listening/device acceptance or a sustained-load benchmark. S3.7 closes the planned S3 processing/export sequence at these bounded contracts. That receipt is the historical S3.7 boundary. Current S4 implementation and remaining work are tracked in [the S4 ledger](S4-IMPLEMENTATION.md). Earlier S1 editing-integrity and physical-acceptance deferrals remain open.
