# Directory: src/engine

Purpose: Real-time audio engine, graph, and source implementations.

The foundation implementation and remaining limits are reconciled in the [S1 closeout](../../docs/improvement/S1-CLOSEOUT.md). The original broad S1 contract is not fully complete; further implementation is stopped pending a new instruction.

- `engine_core_commands.c` transfers complete command packets from the creating control thread to the render worker; overflow is observable and stop/note-off have an atomic safety fallback.
- `engine_source_plan.c` prepares independent clip sources and pins decoded media, publishes complete source revisions, and reclaims retired revisions on the control thread. Queued external graphs use the same retirement mechanism; their borrowed source userdata must outlive the engine.
- The active revision also owns scalar track metadata, EQ histories, effect handles, and meter accumulation. Track identity maps surviving histories through insertion/removal. Meter frames are copied under a nonblocking publication lock; UI getters lock and reject mismatched track identities.
- `engine_mix_ownership_test` exercises concurrent track/FX/EQ edits through the mixer. The separate source-lifetime test still isolates source ownership. The analyzer/lifecycle test adds real worker and analyzer threads with an SDL dummy output device, five startup failure stages, and eight restarts. Individual edit rejection and playback status are covered by dedicated tests; compound callers and physical-device acceptance remain deferred.
- Analyzer streams carry complete contiguous windows tagged with target generations and stable track/effect identities. Queue overflow drops a whole new window; only the consumer advances the read index. Analysis history belongs to the consumer, and obsolete generations cannot publish results. Frequency calibration and transform performance remain S3/S4 work.
- Startup prepares buffers before starting workers and has one rollback path. Repeated start is idempotent; stop pauses output, joins every worker, then resets quiescent queues and reclaims storage. Project rate, DSP block size, tempo, and effect identities survive endpoint restarts.
- Gain/pan/mute/solo/EQ edits restore the prior editable state when revision preparation fails. FX parameter edits roll back under the control mutex; structural FX edits commit an independent candidate only after render preparation succeeds. The parameter-transaction test covers rejection, retry, and preservation of an earlier pending revision. Individual clip/track mutations also have failure transactions; outer compound actions and remaining UI/undo feedback are deferred.
- Explicit zero track/clip gain means silence. Legacy missing serialized gains default to unity in the parser.

## Files
- `buffer_pool.c`
  - `engine_buffer_pool_init/free`: Allocate contiguous scratch storage for multi-channel blocks.
  - `engine_buffer_pool_acquire/release`: Hand out per-frame channel views and mark them as in-use.
- `engine.c`
  - Transport/device control (`engine_create/start/stop/destroy`, `engine_is_running`, `engine_transport_play/stop/is_playing`).
  - Clip + track management (`engine_add_track`, `engine_remove_track`, `engine_track_set_name`, `engine_track_set_muted`, `engine_track_set_solo`, `engine_track_set_gain`, `engine_add_clip[_to_track]`, `engine_remove_clip`, `engine_duplicate_clip`, setters for timeline/gain/name/region).
  - Clip instances now retain their source `media_path` for session persistence.
  - Decoded audio cache (`AudioMediaCache`) shares buffers between clips referencing the same file+sample-rate.
  - Transport helpers (`engine_transport_play/stop/is_playing/seek/set_loop`) and command queue plumbing for worker-thread safe updates.
  - Segment utilities (`engine_add_clip_segment`) to clone portions of an existing clip when splitting around drops.
  - MIDI clip creation and note mutation APIs; persisted MIDI notes rebuild preset/parameter-aware instrument sources, and live audition commands feed temporary QWERTY notes through the selected region preset and sanitized parameter overrides without mutating clips. Stopped-transport audition uses an engine-local idle clock plus an audition-only mix path so Test mode can produce sound without advancing the playhead or playing saved region notes.
  - Graph wiring (`engine_queue_graph_swap`, `engine_rebuild_sources`) and command processing (`engine_post_command`, worker thread loop), including MIDI instrument source registration.
  - Utility helpers for sampler refresh, clip sorting, and transport frame tracking.
- `engine_meter.c`
  - Master/track peak-RMS metering snapshots plus FX metering taps (correlation, vectorscope, LUFS).
  - FX meter tap callback captures per-instance snapshots for the effects panel detail views.
- `engine_analysis_queue.c`
  - Generation-checked target selection, contiguous window assembly, and whole-packet SPSC publication.
- `engine_spectrum.c`
  - Background spectrum analyzer for EQ detail views (track or master).
- `engine_spectrogram.c`
  - STFT-style spectrogram history capture for the spectrogram meter detail view.
- `graph.c`
  - `engine_graph_create/destroy/configure`: Own the mixing graph and its buffer pool.
  - `engine_graph_add_source/clear_sources`: Register sampler/tone sources with optional reset callbacks.
  - `engine_graph_render/reset`: Mix active sources into an interleaved block each engine tick.
  - `engine_graph_get_*`: Expose graph configuration (channels/sample-rate/max block/pool).
- `midi.c`
  - `engine_midi_note_list_*`: Maintain bounded, sorted MIDI note arrays for model-only MIDI clips.
  - `engine_midi_note_is_valid`: Validate note duration, pitch range, and velocity range before clip-level duration checks.
- `engine_midi_audition.c`
  - `engine_midi_audition_note_on/off/all_notes_off`: Queue or apply temporary live MIDI notes for editor audition without writing clip note arrays, including stopped-transport audition cleanup.
- `engine_audio.c`
  - `engine_mix_midi_audition_only`: Renders only the temporary MIDI audition source for stopped Test mode while preserving track/master processing.
- `instrument_osc.c`
  - `engine_instrument_param_*`: Describe and sanitize the first bounded per-region instrument parameters: Level, Tone, Attack, and Release.
  - `engine_instrument_source_create/destroy`: Own per-MIDI-clip instrument state and copied note snapshots.
  - `engine_instrument_source_set_midi_clip`: Capture transport placement, clip duration, preset id, sanitized params, and bounded MIDI notes for graph rendering.
  - `engine_instrument_source_render/reset`: Render built-in Pure Sine, Soft Square, Saw Lead, and Simple Bass presets with MIDI pitch-to-frequency conversion, tone shaping, level scaling, and attack/release ramps.
- `sampler.c`
  - `engine_sampler_source_create/destroy`: Manage per-clip sampler instances.
  - `engine_sampler_source_set_clip`: Map a media clip segment onto the global transport timeline, capturing fade-in/out spans for ramping.
  - `engine_sampler_source_render/reset`: Produce interleaved audio frames from the scheduled segment.
  - Accessors for start frame, frame counts, offsets, and underlying media.
- `timeline_contract.c`
  - `daw_timeline_frame_range*`: Define half-open transport-frame ranges and overlap predicates.
  - `daw_timeline_analyze_overlap`: Produce trim/split/remove/shift plans used by clip overlap resolution.
  - `daw_timeline_frames_from_*`: Convert seconds or tempo-map beats into canonical transport frames for audio and MIDI region placement.
- `track_role.c`
  - `engine_track_role_*`: Derive non-persistent empty/audio/MIDI/mixed track roles from existing clip kinds for recording-target and timeline UX policy.
- `source_tone.c`
  - `engine_tone_source_create/destroy`: Allocate a diagnostic tone generator.
  - `engine_tone_source_render/reset`: Fill buffers with a simple sine tone, keeping phase continuity.
  - `engine_tone_source_ops`: Populate an `EngineGraphSourceOps` struct for registration.

Tempo, seek, loop, and recording-arm mutations require the creating control thread. Tempo values are validated and use the immutable project rate; queued tempo changes apply as whole FIFO messages. Recording-arm edits restore the prior isolation target when source revision preparation fails.

Track append/insertion/removal and clip deletion are transactional at render publication. Detached resources stay owned until the replacement revision succeeds; rejection restores the editable model and leaves the accepted render revision intact. Track edits remap engine recording isolation, preserve surviving runtime/FX identities, and clear stale UI caches after commit. Clip creation/move/trim and implicit track growth still need their separate failure-path audit.

`audio_source.c` owns stable source metadata objects behind a growable pointer table. A clip's metadata pointer survives registry growth; registry clear detaches editable clip references before freeing the objects. Render revisions retain their own immutable media pins and do not borrow registry metadata. The lifetime test covers 128-entry registry growth and clear while a clip is still present.

Capacity growth prepares replacement track/meter arrays and scope banks before committing pointers. Snapshot double-buffer rows retain their contents at the new stride. Failure frees newly prepared storage without changing existing capacities or track resources; fault-injection tests cover all capacity arrays and each new scope ring.

Scalar clip edits (timeline start, region, gain, and fade lengths) restore their previous metadata and sampler timing if source revision preparation fails. Timeline moves also restore sorted order and leave the caller's output index untouched. These setters edit existing clips on the control thread; unchanged automation storage is not rebuilt for scalar timing updates.

Clip automation edits prepare independent lane storage and replacement control sources, then publish one complete render revision. Failure retains original automation and sources; successful edits invalidate borrowed lane/source pointers. Copy/snapshot helpers propagate point-allocation failure, and sampler automation replacement returns a boolean without discarding prior lanes on failure. Track-instrument automation uses independent lane candidates as well. MIDI note edits retain the original list until publication succeeds; instrument scalar settings restore prior metadata on rejection. These APIs require control-thread access and existing tracks/clips, and reject non-finite velocities or parameters.

Clip additions use a separate descriptor array during preparation. Existing sources are borrowed until commit; only the new clip owns newly prepared resources. Publication failure discards that clip and restores implicit track growth and effects. Successful publication replaces the descriptor array, invalidating borrowed clip descriptors. Source registry metadata and spare capacity can remain cached after rejection.

Overlap resolution prepares all removed/trimmed/shifted/split regions before a single publication attempt. Rejection preserves the original track; acceptance frees replaced control sources while render revisions retain independent ownership. The anchor and unchanged sources remain stable, and newer clips retain priority. Compound caller actions that surround overlap resolution remain deferred under D1 in the closeout.

Cross-track moves transfer the complete clip descriptor between prepared arrays and publish one revision. Failure restores both tracks and sampler timing; success retains clip/source identity while invalidating borrowed descriptor arrays. Implicit destination tracks roll back with rejected moves. Same-track movement delegates to the timeline-position setter.

Live play/stop return command acceptance and leave playback-state changes to FIFO consumption by the worker. `engine_transport_is_playing` reports applied state. Offline play/stop synchronously consume commands so flush/reset and playback state agree. Requested/pending transport presentation uses the snapshot API described below; manual visual acceptance remains deferred.

`EnginePlaybackSnapshot` exposes accepted play/stop intent and applied playback with serial acknowledgements and a pending flag. Safety-stop fallback acknowledges the latest accepted stop serial. Input toggles use accepted intent; audio and metering continue to use applied state. Snapshot timing is command state, not audio-device presentation timing.

`engine_track_set_settings` restores mixer and MIDI instrument scalar settings with one validated publication. Failed preparation restores every field. It is the engine transaction used by track-setting history restores.

`engine_transform_clip` applies placement, region bounds, gain/fades, and MIDI notes/instrument settings in one validated publication, including transfers between tracks. Rejection restores prior ownership and sampler timing; successful edits retain creation identity and invalidate borrowed descriptor arrays. It is used by complete clip-transform history. A multi-clip caller still needs a separate outer transaction to guarantee group-wide rollback.

`EngineEqState` retains the accepted `EngineEqCurve` alongside coefficients so control-thread session capture can read processor configuration through `engine_get_eq_curve`. Render configuration clones copy this metadata without sharing processing histories. `engine_is_control_thread` exposes the existing ownership check to project capture/restore. A restored project is fully prepared offline before replacing the previous engine; audio-device activation remains separate.

### Software clocks and transport

`engine_clock.c` maps a monotonic stream-frame count within each discontinuity epoch to project/loop positions. `EngineClockSnapshot` separates prepared, callback-delivered, and estimated presentation positions; hardware position is explicitly unknown. Callback underrun silence does not advance timeline position. UI/editing uses `engine_get_presentation_frame`; `engine_get_transport_frame` remains the render cursor.

Pause holds the delivered cursor; Stop returns to zero; seek preserves mode; loop changes preserve the delivered cursor then apply the new interval. Transport commands carry requested/applied serials. The worker briefly excludes the SDL callback to flush a generation, then resets source/EQ/FX histories outside that window. Ordinary loop wraps retain FX tails. Space uses pause/resume. See [the S2.3/S2.4 contract](../../docs/improvement/S2-CLOCKS-TRANSPORT.md) and `make test-engine-transport-clock` for proof and physical-timing limits.

`EngineDiagnostics` exposes lifetime callback gaps, whole-block render-budget overruns/max duration, FIFO command age, output occupancy/high-water, and command admission counters. Callback instrumentation uses fixed atomic operations; formatting belongs to status/log consumers. These are software observations, not physical DAC/ADC or hardware xrun measurements. See [S2.6](../../docs/improvement/S2-DIAGNOSTICS.md).

Audio fade shapes now use `engine/fade_curve.h`, the same existing formulas used by UI previews. Samplers and render clones carry both shapes; scalar shape edits publish transactionally and roll back on rejection. Zero fades and linear endpoint timing are unchanged. See [S3.1](../../docs/improvement/S3-FADE-PARITY.md) and `make test-fade-processing`.

Spectrum/spectrogram now share `analysis_math.h` for Hann tonal-amplitude normalization and a Nyquist-bounded log-frequency grid. Spectrum averages powers from two windows. Packets carry epoch/sequence/sample offsets; gaps reset aggregation, and snapshots reject old epochs. Spectrogram metadata identifies rate, window size, selected insert, and capture position. See [S3.2](../../docs/improvement/S3-ANALYSIS-CALIBRATION.md).

S3.3 compensates the existing parallel track chains to their longest active processing delay before the master sum. Diagnostics report common track-plus-master DSP latency independently of queue/device timing. Offline range bounce resets its independently captured FX/EQ history, renders the extra reported delay, and trims that leading delay. Timing edits reset histories at a block boundary; this is not seamless delay modulation. See [the dynamics contract](../../docs/improvement/S3-DYNAMICS.md).

S3.4 transfers sample-counted source gain and track balance ramps across compatible prepared revisions; live changes use 5 ms while stopped edits and explicit transport resets snap to authored targets. Ordinary source resets at loop wraps preserve these ramps. Stopped audition has prepared gain state of its own. See [the transition contract](../../docs/improvement/S3-CONTROL-TRANSITIONS.md).

S3.6 interprets MIDI duration as a gate followed by release from the gate-end envelope level, bounded by the authored region. Audition prepares 256 descriptors, retains releases, retires them at worker boundaries, and caps stopped effect-tail processing at two seconds with a final 5 ms fade. Transport discontinuities and target/preset/parameter changes clear the live voice set. Existing volume/pan lanes now affect instruments; sample-rate-aware saw/triangle components reduce aliasing without claiming alias-free nonlinear presets. See [instrument lifecycle](../../docs/improvement/S3-INSTRUMENT-LIFECYCLE.md).


S3.7 captures authored sources, media pins, effects, EQ, and mixer targets into a private export plan. The common prepared mixer runs without live telemetry or audition taps; playback and its DSP histories continue independently. `engine_bounce_range_to_buffer_with_options` adds up to 60 seconds each of preroll and fixed tail, with explicit normalization. Exact/cold normalized-if-clipping defaults remain in the legacy helpers. Range-end input gating excludes later material while allowing selected MIDI release and effect decay within the requested tail. `engine_bounce_write_wav` provides fixed-dither PCM16 or explicit float32 output. See [export contracts, failure semantics, and evidence](../../docs/improvement/S3-EXPORT.md). Rendering remains synchronous; explicit buffer APIs are RAM-backed, while file exports now use the S4.6 disk spool. Live deadline and physical listening acceptance are separate.


S4.1 audits source/note traversal, revision preparation/adoption, metering memory, queue policy, and disk paths using opt-in optimized workloads. See [the measured baseline and proposed slices](../../docs/improvement/S4-RUNTIME-AUDIT.md). The historical audit is followed by [S4.2/S4.3 scheduling and S4.4a compact meter publication](../../docs/improvement/S4-IMPLEMENTATION.md): track-local sampler interval rejection, per-block MIDI candidates with cached base pitch, and published meter banks without DSP histories. Exact sample/order/ramp/envelope behavior and published meter payloads are covered by focused checks. S4.4b adds compact complete scalar snapshots through the existing pending slot and sorted prepared identity lookup for source ramp transfer. S4.5 measures the full busy worker cycle and bounds render-ahead with `output_queue_blocks`, rounded to at least two callback buffers. See [contracts, queue policy, and evidence](../../docs/improvement/S4-EDITING-DEADLINES.md).

S4.6 adds `engine_bounce_range_to_wav`: one private captured render, bounded block buffers, an anonymous float spool, explicit second-pass normalization, cancellable progress, and durable PCM16/float WAV publication. `engine_bounce_range` delegates to it; normalized chunk observers support bounded optional pack overviews. The owner engine must outlive callbacks, and observations are provisional until the writer reports publication. See [S4.6 memory/I/O limits and capture-quality finding](../../docs/improvement/S4-STREAMING.md).

`engine_capture_epoch_is_current` reads a dedicated capture epoch published only after a completed playing, non-looping transport transition. Discontinuities invalidate it before touching clock fields; ordinary output-clock publication leaves it readable. Anchored recording can preserve source-clock samples without a fresh diagnostic snapshot, while actual epoch changes still halt capture. See [the capture continuity proof](../../docs/improvement/S4-CAPTURE-CLOCK.md).


S4.7 adds decoder-free prepared/cached clip publication through the existing transaction, current-version cache lookup and control-thread retired-media collection. Only already retired render plans are collected; active source pins remain intact. See [S4.7](../../docs/improvement/S4.7-MEDIA.md).


The dedicated render producer requests SDL high thread priority once per start as a best-effort scheduling policy. Failure leaves ordinary rendering operational. `EngineDiagnostics.worker_priority_status` is 0 before the request/after stop, 1 when accepted, or -1 when refused; restart retries. This does not elevate analyzer/import/UI workers or imply hard real-time delivery. Queue buffering and DSP behavior are unchanged. See `docs/improvement/S4-CLOSEOUT.md` for matched observations and acceptance limits.

S5 adds `engine_transform_clips` for complete transforms including required appended tracks. It prepares replacement arrays and MIDI ownership before one publication, restoring all editable state and sampler timing on rejection. Track growth and effects ownership participate in the same publication; compound duplication remains separate work; complete slide-drop overlap uses the retained-content operation below.

`engine_transform_clips_trim_tracks` supports compound undo by publishing placement restoration and removal of trailing tracks together. It rejects tracks that remain nonempty after the transforms; the application history layer additionally guards authored metadata and FX.

`engine_clip_history.c` owns retained affected-track content snapshots for S5 overlap/history work. They clone authored metadata, retain decoded media, restore all captured rows in one publication, reject missing/moved identities, and invalidate safely during engine destruction. GUI drop integration remains in progress.

`engine_clip_content_drop` prepares initial/final placement, complete affected content and overlap results privately, then publishes once. Its retained snapshots restore split/deleted neighbors without decoding again. Drops that append tracks now retain topology alongside content, clone effects topology before publication, and restore or retire the generated rows in the same undo/redo transaction. Generated-track setting/FX guards remain in the application history layer.

`engine_clip_content_edit` prepares a complete stable-ID duplicate/delete selection, retaining before/after content and publishing once. It preserves MIDI notes/automation and media versions, rejects missing/repeated targets and placement overflow, and returns new identities only after acceptance. Duplication retains the existing per-clip end-plus-gap placement rule.

`engine_clip_content_insert` prepares borrowed authored descriptors into owned audio/MIDI sources and publishes the entire insertion plus required track/effects topology once. Retained history restores content after source removal and can restore an empty (zero-track) project. Introduced identities guard undo against clips moved outside captured rows. Synchronous decode and cache/identity reservation on rejected preparation remain explicit limits.


Whole-track history captures retained media, MIDI/automation, EQ/instrument state and an independent effects chain. Insert prepares the complete row and chain before one publication; rejection rolls back content, ordering and arming. Effect instance restoration likewise configures a private candidate before publication, preserving the original instance ID and disabled state.
