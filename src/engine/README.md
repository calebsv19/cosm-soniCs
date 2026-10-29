# Directory: src/engine

- `hat_choke.c`: control-thread track-local onset preparation and immutable per-note choke fences shared across enabled hat regions, including bounded natural tail spill and export context.
- `instrument_articulation.c`: bounded preparation of mono 808 legato/glide trajectories and region-local hat choke ordering, with seekable analytic phase and no render-time allocation.

- `instrument_percussion.c`: deterministic one-shot kick/snare/hat and gate-aware pitched 808 rendering, independent body/noise envelopes and integrated pitch sweep; see [percussion design](../../docs/percussion_synth.md).

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


`instrument_controls.c` defines preset-supported controls and reversible log-time
knob mapping. `instrument_osc.c` shares its allocation-free isolated note/ADSR
kernel with diagnostic plots while retaining the original polyphonic arithmetic.
Existing saved values, presets and automation semantics are unchanged. The
additive rounded family reuses the bounded triangle/sine helpers and existing
ADSR, detune, sub and drive controls; see [rounded synth](../../docs/rounded_synth.md). See
[instrument controls](../../docs/instrument_controls.md) and
`make test-instrument-controls`.

`instrument_filter.c` owns private per-note low-pass integrators and smoothed cutoff/Q coefficients for Filtered Keys/Pad. Existing voices retain their oscillator renderer. Prepared sources reset this state on note preparation and transport discontinuity; the bounded isolated waveform API streams a separate copy from note onset. See `docs/filtered_synth.md`.

### Guarded MIDI region authoring

Structural MIDI redo restores stable track/clip identities through staged additions; `engine_midi_regions.c` removes an existing-lane region and restores its prior instrument metadata in one publication.

### Effects authoring development

`engine_fx_chain.c` prepares complete chain candidates and readback before publication. Filtered export plans isolate tracks/regions and bypass cloned FX/EQ for dry previews without changing live state or legacy defaults.

## Shared-return engine foundation

`engine/routing.h` captures ordered stable track identities and complete send/return metadata. `engine_routing_set` compares the exact capture, validates the complete graph and publishes one prepared source revision. A preparation failure restores the authored fields. The initial graph supports four return lanes and four post-fader send slots per source. Return lanes contain no regions or enabled instruments; missing/duplicate bindings, feedback, return-to-return sends and nonfinite/out-of-range levels refuse publication. Incoming bindings protect return deletion, even at zero level.

Live and private export plans own return sums and destination maps. Sources feed their sends after lane processing, fader, pan and source-stage compensation; each return processes its aggregated input once, applies its own effects/EQ/fader/pan and aligns with the delayed dry sum before master processing. Source solo retains its returns; return solo suppresses the dry sum while retaining source inputs. Project/source dry exports omit return duplication, while an isolated dry return exposes its source input sum. Stable render revision adoption preserves compatible DSP and smooths same-binding send-level changes during playback.

`test-engine-routing` verifies production samples for independent parallel returns, sparse send slots, source/return isolation, dry rendering, solo/mute/fader behavior and exact serial source/return/master latency. Instrumentation covers source-plan allocation rollback and no new render heap calls. Routing now survives complete project capture/reopening; guarded common/native/MCP authoring remains pending; this engine API does not establish a usable bus workflow by itself. Stopped instrument audition still follows its existing two-second effect-tail policy.

`engine_routing_snapshot_equal` and `engine_routing_validate` expose non-mutating complete-state comparison/validation for application actions. Common routing history uses these guards and the same prepared publication path; valid edits on existing lanes produce one shared undo command. Return-lane creation and native/MCP routing adoption remain separate work.

`engine_append_return` shares ordinary track insertion's prepared topology/FX publication, setting the role before the candidate is visible. Fresh creation or retained-identity redo appends one independent empty return, refuses capacity/identity conflicts, and restores ownership on publication failure. Common creation history protects later lane edits before removing its return. See [routing](../../docs/routing.md) for agent adoption and remaining native controls.

`instrument_catalog.c` owns stable instrument/preset identities and physical metadata separately from oscillator rendering. The filtered voice kernel adds signed octave decay and retriggered filter LFO movement; zero depths retain static filtering. Sweep Bass, Filter Pluck and Moving Pad use these shared controls, including diagnostic rendering and automation.

`instrument_samples.c` owns the immutable bundled solo violin bank, nearby-root playback, sustain overlap and recorded dynamics. Instrument rendering supplies gate/release and smoothed per-note expression; see `docs/sampled_violin.md`.

Violin stabilization uses longer phase-matched loops with correlation-aware overlap energy, per-pitch calibrated soft/strong layers, equal-power expression and S-curve note gates. Live audition retirement honors the minimum protected release. Original assets and the original preparation remain retained.

The fixed sampler now shares bank descriptors across violin, trumpet and clarinet, including adjacent two/three-layer dynamics. Violin arithmetic and its original demo WAV are preserved exactly; see docs/sampled_winds.md.


### Wind stabilization

Wind playback uses `sample_map_stabilized.inc`, prepared with
`tools/prepare_wind_samples.py assets/instruments --stabilize`. Loop selection
measures 100 ms sustain-energy variation alongside seam correlation and boundary
level matching. Original recordings and original preparation remain intact.
Wind dynamics use a quintic easing curve within each adjacent-layer blend, giving
the clarinet middle layer a smooth timbre slope. Existing protected attack and
release gates remain active. Violin mapping, blend arithmetic and defaults are
unchanged. Matching before/after WAVs and installed checks are retained in the
canonical `artifacts/wind-stabilization/20261011/` evidence lane.


The fixed acoustic sampler also supports native 48 kHz tenor saxophone; existing 44.1 kHz banks retain their original behavior. See `docs/sampled_saxophone.md`.


Sax-only coordinated loop metadata and measured blend-energy attenuation stabilize layer interference. Existing instruments remain unchanged.

Flute and cello section use optional per-recording pitch-center ratios with common reference-coordinate dynamics loops; older banks retain their original arithmetic. See `docs/sampled_flute_cello.md`.

Flute/cello runtime uses immutable 100 ms layer-energy measurements with continuous onset/cycle lookup and bounded interference correction. The onset branch, attack/release envelope and older banks retain their existing behavior.

Sampled Oboe and French Horn extend the existing acoustic sampler with paired recorded dynamics and held-note expression. Complete audition chords follow each bank's playable range. See `docs/sampled_oboe_horn.md` for ranges, provenance, preparation and verification.

Oboe/French horn stabilization refines five qualified common loops and regenerates their bounded layer-energy curves; tighter all-pitch blend and note-transition checks cover the existing sampler. See `docs/sampled_oboe_horn.md`.

Sampled Bassoon and Tenor Trombone add two paired-layer recorded sustain banks through the existing sampler, supported controls and guarded preset/automation tools. See `docs/sampled_bassoon_trombone.md` for ranges, source provenance and verification.

Sampled Acoustic Piano, Viola Section, Double Bass and Violin Section extend the acoustic palette. Piano has finite velocity-selected strikes and voice-specific automation; bowed strings reuse sustain/dynamics playback. Audition notes follow each recorded range. See `docs/sampled_piano_strings.md`.

Piano expression extension: [piano expression](../../docs/piano_expression.md) describes the three piano-only controls, binary region/live sustain, 88 string modes, recorded key/pedal mechanics, 32-field v6 presets and session v25. Older preset versions 1–5 and session v24 remain readable; existing non-piano voices keep their behavior.

Nested/collapsible audio bus groups, common history and MCP support are described in [track groups](../../docs/track_groups.md). Engine group projection/mixing and the native group panel have focused signal, persistence, rollback and SDL/Vulkan tests.

Private diagnostic observers attach only to detached export plans. `engine_mix_probe.c` derives tap-specific latency from the production graph; fixed tap hooks observe existing source/bus/master buffers without changing live processing. Full preceding and lookahead/block context are measured by the app-layer capture. See [mix diagnostics](../../docs/mix_diagnostics.md).

Mix diagnostic insert pages now expose per-insert input/output levels and actual
per-channel Compressor/Limiter attenuation with output-latency alignment; see
`docs/mix_diagnostics.md` for paging, calibration and remaining measurement limits.

Immutable mix capture schema v3 adds per-channel spectral windows for processed
child/group/return/master outputs. `get_mix_capture` independently pages
`frequency_window` and `frequency_channel`; see `docs/mix_diagnostics.md` for
calibration, window coverage and remaining loudness/true-peak limits.

Immutable mix captures now include prepared note-ending controls/fences and
threshold-qualified aggregate MIDI source tails. `get_mix_capture` independently
pages eight note rows with `note_offset`; see `docs/mix_diagnostics.md` section 7.

Asynchronous whole-song WAV export now uses a captured production graph and shared worker, with native/MCP format/tail choices, progress, cancellation and verified no-overwrite output. See [export contracts](../../docs/song_export.md).

Qualified live 400 ms per-channel/stereo/mono compatibility windows now cover child/group/return/master output, with native scope controls and guarded output-only stereo/mono monitoring. The approximate live LUFS view is hidden behind an explicit qualification notice; offline validated LUFS remains available. See [live metering contracts](../../docs/live_stereo_metering.md).

Compressor/Limiter controls now have qualified non-consuming live gain-reduction feedback in native headers and `get_effect_chain.dynamics_feedback`, separate from authored edit tokens. Compressor detection remains independent by channel; Limiter is sample-peak, not true-peak. See [dynamics controls and feedback](../../docs/dynamics_feedback.md).
