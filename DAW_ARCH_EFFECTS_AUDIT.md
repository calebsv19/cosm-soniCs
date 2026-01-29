# Codex Task: DAW Architecture & Effects System Audit (Answer With Code References)

You are analyzing an existing DAW codebase. For every answer:
- Name the exact files and functions involved.
- Describe the runtime call sequence (who calls who, in what order).
- Identify thread context (audio thread vs UI thread vs worker).
- Note any real-time unsafe operations on the audio thread (allocations, locks, file IO, logging, vector growth, etc.).
- If unknown/unclear, say so and explain why.

## A) Engine loop & threading model

### A1) Main entry and init sequence
- Entry point: `src/app/main.c:203` (`main`).
- Init sequence (UI thread, main thread):
  1) Config + runtime state: `config_load_file`, `config_set_defaults`, `tempo_state_default`, `tempo_map_init`, `time_signature_map_init`, etc. in `src/app/main.c:203-235`.
  2) UI pane/layout + project manager: `ui_init_panes`, `project_manager_init` in `src/app/main.c:241-243`.
  3) Project/session load or fresh engine: `project_manager_load_last`, `session_load_from_file`, else `engine_create` in `src/app/main.c:244-263`.
  4) SDL app and font init: `App_Init`, `TTF_Init`, `ui_font_set`, timer HUD init in `src/app/main.c:287-308`.
  5) UI layout + input init: `ui_layout_panes`, `pane_manager_init`, `input_manager_init`, `inspector_input_init`, `effects_panel_input_init` in `src/app/main.c:310-327`.
  6) Engine start: `engine_start` -> post-load FX apply -> `effects_panel_sync_from_engine` in `src/app/main.c:339-347`.
  7) Transport init: `engine_transport_stop`, `engine_transport_seek(0)` in `src/app/main.c:350-352`.
  8) Main loop: `App_Run` with callbacks in `src/app/main.c:331-355`.
  9) Shutdown: session save and cleanup in `src/app/main.c:357-377`.

### A2) Audio callback function and registration
- Audio callback: `engine_audio_callback` in `src/engine/engine_audio.c:93`.
- Registered in `engine_start` via `audio_device_open` passing `engine_audio_callback` in `src/engine/engine_core.c:629-633`.
- SDL audio trampoline: `sdl_audio_trampoline` calls device callback in `src/audio/device_sdl.c:6-19`.

### A3) Threads and ownership
- UI/Main thread (SDL): `App_Run` loop calling `handle_input`, `handle_update`, `handle_render` in `src/app/main.c:331-355`.
  - Owns: `AppState` and all UI state (effects panel, layout, selection, etc.) in `include/app_state.h`.
- Audio device thread (SDL audio callback): `sdl_audio_trampoline` -> `engine_audio_callback` in `src/audio/device_sdl.c:6-19`, `src/engine/engine_audio.c:93-103`.
  - Owns: output device buffer; reads from `engine->output_queue` (ring buffer).
- Engine worker thread: created in `engine_start` with `SDL_CreateThread(engine_worker_main, ...)` in `src/engine/engine_core.c:695-701`.
  - Owns: render pipeline and transport frame updates; writes to `engine->output_queue`.
- Spectrum thread: created in `engine_start` in `src/engine/engine_core.c:703-711` running `engine_spectrum_thread_main` (`src/engine/engine_spectrum.c:235+`).
  - Owns: spectrum ring buffer consumption and spectrum history updates.
- Spectrogram thread: created in `engine_start` in `src/engine/engine_core.c:714-725` running `engine_spectrogram_thread_main` (`src/engine/engine_spectrogram.c:198+`).
  - Owns: spectrogram ring buffer consumption and spectrogram history updates.

### A4) Shared state
- Shared state between UI and worker:
  - Engine graph/track data: `engine->tracks`, `engine->graph` (`include/engine/engine_internal.h`).
  - FX manager: `engine->fxm` guarded by `engine->fxm_mutex` (`include/engine/engine_internal.h`, `src/engine/engine_fx.c:140-239`).
  - Transport state: `engine->transport_playing` (atomic) and `engine->transport_frame` (non-atomic) (`include/engine/engine_internal.h`, `src/engine/engine_transport.c:33-104`).
  - Loop state: `engine->loop_*` atomics in `include/engine/engine_internal.h`.
  - Meters: `engine->master_meter`, `engine->track_meters`, `engine->master_fx_meters`, `engine->track_fx_meters` guarded by `engine->meter_mutex` (`src/engine/engine_meter.c:389-467`).
  - Spectrum/spectrogram buffers: `engine->spectrum_queue`, `engine->spectrogram_queue` with mutexed history (`src/engine/engine_spectrum.c:197-209`, `src/engine/engine_spectrogram.c:314-349`).
- Command queue between UI and worker: ring buffer `engine->command_queue` (SPSC) with `engine_post_command` and `engine_process_commands` in `src/engine/engine_core.c:30-135`.

### A5) Per-buffer audio pipeline order
- Audio device thread (callback):
  - `sdl_audio_trampoline` -> `engine_audio_callback` -> `audio_queue_read` -> zero-fill remainder.
  - `src/audio/device_sdl.c:6-19` -> `src/engine/engine_audio.c:93-103`.
- Worker thread render loop:
  - `engine_worker_main` -> `engine_process_commands` -> `engine_spectrum_begin_block` -> `engine_spectrogram_begin_block` -> `engine_mix_tracks` -> loop/seek handling -> `fxm_render_master` -> `engine_sanitize_block` -> `engine_spectrum_update` -> `audio_queue_write`.
  - `src/engine/engine_core.c:158-275`.
- Track render path inside `engine_mix_tracks`:
  - `engine_graph_render_track` -> `fxm_render_track` -> `engine_eq_process` (track EQ) -> `engine_spectrum_update_track` -> `apply_track_pan` -> track meter -> sum into master.
  - Then `engine_eq_process` (master EQ) -> `fxm_render_master` -> `engine_sanitize_block` -> master meter.
  - `src/engine/engine_audio.c:105-172`.

### A6) Sample rate / buffer size / block size representation
- Representation: `EngineRuntimeConfig` has `sample_rate` and `block_size` in `include/config.h`.
- Default load and config: `config_set_defaults` / `config_load_file` in `src/config/config.c:58-157` and `src/app/main.c:223-230`.
- Propagation:
  - `engine_create` seeds `engine->config` in `src/engine/engine_core.c:282-291`.
  - `engine_start` negotiates device spec and overwrites `engine->config.sample_rate` and `engine->config.block_size` in `src/engine/engine_core.c:636-638`.
  - `engine_graph_configure` uses negotiated spec in `src/engine/engine_core.c:651-655`.
- Mutability mid-run: no explicit dynamic change at runtime; only updated at `engine_start` after device open. No UI path found to change sample rate/block size while running.

### A7) Transport model
- Play/stop/seek: `engine_transport_play`, `engine_transport_stop`, `engine_transport_seek` in `src/engine/engine_transport.c:5-61`.
- Pause: no separate pause API; stop toggles transport off.
- Transport state storage: `engine->transport_playing` (atomic) and `engine->transport_frame` (non-atomic) in `include/engine/engine_internal.h`.
- Seek effect on render state:
  - If engine running: command queued, worker handles `ENGINE_CMD_SEEK` in `engine_process_commands`, which sets `engine->transport_frame`, clears output queue, and resets graph in `src/engine/engine_core.c:102-105`.
  - If not running: `engine_transport_seek` directly sets `transport_frame`, clears queue, resets graph in `src/engine/engine_transport.c:44-48`.
  - FX state is not reset on seek (no call to FX reset), only graph sources reset.

### A8) Latency handling
- FX latency field exists in `FxDesc.latency_samples` in `include/effects/effects_api.h:18-27`.
- No latency compensation or reported latency logic found in engine; `FxDesc.latency_samples` is not used in `effects_manager` or engine render.

### A9) UI <-> DSP direct calls
- UI calls into engine for DSP state:
  - FX parameter updates: `effects_panel_input.c:215-266` -> `engine_fx_*_set_param*` in `src/engine/engine_fx.c:179-207`.
  - EQ curve updates: `engine_set_master_eq_curve` / `engine_set_track_eq_curve` from EQ panel input in `src/input/effects_panel_eq_detail_input.c` (calls in `src/engine/engine_tracks.c:469-478`).
- DSP/worker pushes to UI via shared state (meter/spectrum histories) with mutexes and ring buffers; no direct UI callback from audio thread.
- RT-unsafe interactions: FX and meter updates use `SDL_LockMutex` in worker thread (see G).

## B) Audio graph structure (tracks, routing, buses)

### B1) Core audio graph objects
- `EngineGraph` (`src/engine/graph.c:7-17`, `include/engine/graph.h`) holds sources and a mix buffer.
- Track/clip structures: `EngineTrack` and `EngineClip` in `include/engine/engine.h:58-109`.
- Source ops for samplers and tone: `EngineGraphSourceOps` in `include/engine/graph.h:10-16`, sampler ops in `src/engine/sampler.c:174-176`.
- FX chains: `EffectsManager`, `FxChain`, `FxInstance` in `src/effects/effects_manager.c:15-63`.

### B2) Pull vs push
- Pull-based: worker thread calls `engine_graph_render_track` which pulls from sources via `EngineGraphSourceOps.render` in `src/engine/engine_audio.c:122-127` and `src/engine/graph.c:116-145`.

### B3) Routing (sends/returns, buses, sidechains)
- No explicit buses or sends/returns found. Sources are clips; track routing is via track index filtering in `engine_graph_render_track` (`src/engine/graph.c:116-145`).
- Sidechain API exists in `FxVTable.process_sc` in `include/effects/effects_api.h:24-42`, but no routing or call sites found in engine; currently unused.

### B4) Routing config and execution
- Configured in `engine_rebuild_sources` by iterating tracks and clips, adding sources via `engine_graph_add_source` in `src/engine/engine_core.c:37-77`.
- Execution in render loop: `engine_graph_render_track` inside `engine_mix_tracks` in `src/engine/engine_audio.c:122-127`.

### B5) Master mix
- Master mix = sum of per-track buffers into `out` with no normalization/clipping (beyond sanitize) in `engine_mix_tracks` (`src/engine/engine_audio.c:148-150`).
- After summing: master EQ + master FX + sanitize + meter in `src/engine/engine_audio.c:153-172`.
- Worker thread applies master FX again on whole block in `src/engine/engine_core.c:264-268` (double-application noted; see G).

### B6) Intermediate buffers
- `engine_worker_main` allocates `block_buffer` and `track_buffer` once per worker thread in `src/engine/engine_core.c:146-149`.
- `EngineGraph` uses `mix_buffer` and grows via `realloc` in `src/engine/graph.c:103-113`.
- `EffectsManager` uses a scratch buffer allocated once in `fxm_create` in `src/effects/effects_manager.c:212-230`.

### B7) Buffer reuse vs per-block allocation
- Reused per block: `block_buffer`, `track_buffer`, `fxm->scratch`, `EngineGraph.mix_buffer`.
- Potential allocation during render: `EngineGraph.mix_buffer` via `realloc` in render when capacity grows (`src/engine/graph.c:103-113`).

### B8) Channel configuration
- Global channel count stored in `EngineGraph` and audio device spec: `engine_graph_get_channels` / `AudioDeviceSpec.channels` (`src/engine/graph.c:175-177`, `src/audio/device_sdl.c:55-57`).
- Engine currently requests 2 channels in `engine_start` (`src/engine/engine_core.c:621-625`).
- Clip channel handling: `sample_from_clip` uses clip channels and clamps to last channel for extra output channels (`src/engine/sampler.c:178-192`).
- No per-track channel count; all track buffers use engine graph channels.

### B9) Graph rebuild
- Rebuild triggered by track/clip edits (`engine_rebuild_sources` called across `src/engine/engine_tracks.c` and `src/engine/engine_clips.c`, e.g. `src/engine/engine_tracks.c:259+`, `src/engine/engine_clips.c:780+`).
- Rebuild runs in the caller thread (usually UI thread). No explicit synchronization with worker thread for `engine->graph` mutations.

## C) Effects / plugins system (current reality)

### C1) What is an effect
- Base API: `FxDesc`, `FxVTable`, `FxHandle` in `include/effects/effects_api.h:12-42`.
- Manager: `EffectsManager` handles chain instances and rendering in `src/effects/effects_manager.c`.
- Typical effect implementation: `*_get_desc` and `*_create` in each `src/effects/**/fx_*.c` file.

### C2) Storage and order
- Per-track and master chains are dynamic arrays `FxChain` inside `EffectsManager` (`src/effects/effects_manager.c:28-63`).
- Order is array order; render loops from 0..count-1 in `fxm_render_track` / `fxm_render_master` (`src/effects/effects_manager.c:603-621`, `629-657`).

### C3) Bypass
- `FxInstance.enabled` controls bypass (true bypass; skip processing) in `fxm_render_*` (`src/effects/effects_manager.c:605-606`, `639-640`).

### C4) Parameter representation
- `FxDesc` provides param names and defaults (`include/effects/effects_api.h:14-27`).
- Runtime per-instance storage in `FxInstance.param_values`, `param_mode`, `param_beats` (`src/effects/effects_manager.c:15-26`).
- Session persistence: `SessionFxInstance` in `include/session.h:125-139`.

### C5) Min/max/default handling
- Defaults: `FxDesc.param_defaults` per effect.
- Min/max: heuristically derived in UI by `derive_param_range` based on param name in `src/ui/effects_panel/panel.c:310-371`.

### C6) UI -> DSP parameter update path
- UI slider change: `effects_panel_input.c:215-266` -> `engine_fx_*_set_param(_with_mode)` -> `fxm_*_set_param` -> `FxVTable.set_param`.
- Call sequence (UI thread): `apply_slider_value` (`src/input/effects_panel_input.c:215-266`) -> `engine_fx_master_set_param`/`engine_fx_track_set_param` (`src/engine/engine_fx.c:179-207`, `252-270`) -> `fxm_master_set_param`/`fxm_track_set_param` (`src/effects/effects_manager.c:394-546`) -> effect `set_param`.

### C7) Thread safety of params
- Parameter updates are protected by `engine->fxm_mutex` in `engine_fx_*` (`src/engine/engine_fx.c:179-219`, `223-270`).
- Render thread also locks `engine->fxm_mutex` around `fxm_render_*` in `engine_mix_tracks` and worker loop (`src/engine/engine_audio.c:128-131`, `154-157`; `src/engine/engine_core.c:264-268`).
- This is thread-safe but RT-unsafe (blocking mutex in render thread).

### C8) Parameter smoothing
- No general parameter smoothing system found. Individual effects have internal envelope smoothing (compressor, auto_trim, etc.), but no manager-level smoothing of param changes.

### C9) Effect reset behavior
- Effects are reset only at creation (`instantiate_fx` calls `vt.reset` in `src/effects/effects_manager.c:248-326`).
- No global reset on play/stop/seek; `engine_transport_stop` resets graph but not FX (`src/engine/engine_core.c:87-90`, `src/engine/engine_audio.c:153-158`).
- FX state resets when engine is restarted (engine recreates `EffectsManager` in `engine_start`, `src/engine/engine_core.c:667-688`).

### C10) Existing effects list (file, key params, state, memory, resources)
- Basics/utility:
  - Gain (`src/effects/basics/fx_gain.c`): `gain_dB`. Stateless; no extra buffers beyond instance; no external resources.
  - DCBlock (`src/effects/basics/fx_dc_block.c`): `cutoff_hz`, `mix`. Stateful (filter history); no external resources.
  - Pan (`src/effects/basics/fx_pan.c`): `pan`. Stateless; no external resources.
  - Mute (`src/effects/basics/fx_mute.c`): `mute`, `ramp_ms`. Stateful (ramp); no external resources.
  - MonoMakerLow (`src/effects/basics/fx_monomaker.c`): `crossover_hz`, `slope`. Stateful (filter history); no external resources.
  - StereoBlend (`src/effects/basics/fx_stereo_blend.c`): `balance`, `keep_stereo`. Likely stateless; no external resources.
  - AutoTrim (`src/effects/basics/fx_auto_trim.c`): `target_dB`, `speed_ms`, `max_gain_dB`, `gate_thresh_dB`. Stateful (RMS/env); no external resources.
- Dynamics:
  - Compressor (`src/effects/dynamics/fx_compressor.c`): `threshold_dB`, `ratio`, `attack_ms`, `release_ms`, `makeup_dB`, `knee_dB`, `detector`. Stateful (envelopes), per-channel buffers allocated; no external resources.
  - Limiter (`src/effects/dynamics/fx_limiter.c`): `ceiling_dB`, `lookahead_ms`, `release_ms`. Stateful (lookahead buffer); no external resources.
  - Gate (`src/effects/dynamics/fx_gate.c`): `threshold_dB`, `ratio`, `attack_ms`, `release_ms`, `hold_ms`. Stateful; no external resources.
  - DeEsser (`src/effects/dynamics/fx_deesser.c`): `center_hz`, `Q`, `threshold_dB`, `ratio`, `attack_ms`, `release_ms`, `makeup_dB`, `band_only`. Stateful (filters/envelopes); no external resources.
  - SCCompressor (`src/effects/dynamics/fx_sidechain_compressor.c`): same params as compressor. Stateful; sidechain API present but routing not wired; no external resources.
  - UpwardComp (`src/effects/dynamics/fx_upward_comp.c`): `threshold_dB`, `ratio`, `attack_ms`, `release_ms`, `mix`, `makeup_dB`. Stateful; no external resources.
  - Expander (`src/effects/dynamics/fx_expander.c`): `threshold_dB`, `ratio`, `hysteresis_dB`, `attack_ms`, `release_ms`. Stateful; no external resources.
  - TransientShaper (`src/effects/dynamics/fx_transient_shaper.c`): `attack_amt`, `sustain_amt`, `fast_ms`, `slow_ms`, `mix`. Stateful; no external resources.
  - SoftClip (`src/effects/dynamics/fx_softclip.c`): `drive`, `makeup_dB`. Likely stateless; no external resources.
- EQ:
  - BiquadEQ (`src/effects/eqs/fx_biquad_eq.c`): `type`, `freq_hz`, `Q`, `gain_dB`. Stateful (biquad history); no external resources.
  - EQ_Fixed3 (`src/effects/eqs/fx_eq_fixed3.c`): `low_gain_dB`, `mid_gain_dB`, `high_gain_dB`, `mid_Q`. Stateful (filters); no external resources.
- Filter & tone:
  - SVF (`src/effects/filter&tone/fx_svf_filter.c`): `mode`, `cutoff_hz`, `q`, `gain_dB`. Stateful; no external resources.
  - AutoWah (`src/effects/filter&tone/fx_autowah.c`): `min_hz`, `max_hz`, `q`, `attack_ms`, `release_ms`, `mix`. Stateful; no external resources.
  - StereoWidth (`src/effects/filter&tone/fx_stereo_width.c`): `width`, `balance`. Likely stateless; no external resources.
  - TiltEQ (`src/effects/filter&tone/fx_tilt_eq.c`): `pivot_hz`, `tilt_dB`, `mix`. Stateful; no external resources.
  - Phaser (`src/effects/filter&tone/fx_phaser.c`): `rate_hz`, `depth`, `center_Hz`, `stages`, `feedback`, `mix`. Stateful (filters/LFO); no external resources.
  - FormantFilter (`src/effects/filter&tone/fx_formant_filter.c`): `vowel_idx`, `mod_rate_hz`, `mod_depth`, `Q1`, `Q2`, `mix`. Stateful (filters); no external resources.
  - CombFF (`src/effects/filter&tone/fx_comb.c`): `delay_ms`, `gain`, `mix`. Stateful (delay line); no external resources.
- Delay:
  - Delay (`src/effects/delay/fx_delay_simple.c`): `time_ms`, `feedback`, `mix`. Stateful (delay buffer); no external resources.
  - PingPongDelay (`src/effects/delay/fx_pingpong_delay.c`): `time_ms`, `feedback`, `mix`. Stateful (delay buffers); no external resources.
  - MultiTapDelay (`src/effects/delay/fx_multitap_delay.c`): `base_time_ms`, `feedback`, `mix`, `tap*_mul`, `tap*_gain`. Stateful (delay buffers); no external resources.
  - TapeEcho (`src/effects/delay/fx_tape_echo.c`): `time_ms`, `feedback`, `mix`, `wobble_hz`, `wobble_depth_ms`, `highcut_Hz`, `sat`. Stateful (delay + modulation); no external resources.
  - DiffusionDelay (`src/effects/delay/fx_diffusion_delay.c`): `delay_ms`, `feedback`, `diffusion`, `stages`, `mix`, `highcut_Hz`. Stateful; no external resources.
- Distortion/lo-fi:
  - HardClip (`src/effects/distortion/fx_hardclip.c`): `threshold_dB`, `output_dB`, `mix`. Likely stateless; no external resources.
  - SoftSaturation (`src/effects/distortion/fx_softsat.c`): `drive_dB`, `output_dB`, `tone_lp_hz`, `mix`. Stateful if filter; no external resources.
  - BitCrusher (`src/effects/distortion/fx_bitcrusher.c`): `bits`, `srrate`, `mix`. Stateful (sample hold); no external resources.
  - Overdrive (`src/effects/distortion/fx_overdrive.c`): `drive_dB`, `asymmetry`, `pre_hp_hz`, `post_lp_hz`, `output_dB`, `mix`. Stateful (filters); no external resources.
  - Waveshaper (`src/effects/distortion/fx_waveshaper.c`): `curve`, `drive_dB`, `bias`, `pre_tilt_dB`, `post_tilt_dB`, `pivot_Hz`, `mix`, `out_gain_dB`. Stateful (filters); no external resources.
  - Decimator (`src/effects/distortion/fx_decimator.c`): `hold_N`, `bit_depth`, `jitter`, `post_lowpass_Hz`, `mix`. Stateful; no external resources.
- Modulation:
  - TremoloPan (`src/effects/modulation/fx_tremolo_autopan.c`): `rate_hz`, `depth`, `shape`, `autopan`, `mix`. Stateful (LFO); no external resources.
  - Chorus (`src/effects/modulation/fx_chorus.c`): `rate_hz`, `depth_ms`, `base_ms`, `mix`. Stateful (delay buffer); no external resources.
  - Flanger (`src/effects/modulation/fx_flanger.c`): `rate_hz`, `depth_ms`, `base_ms`, `feedback`, `mix`. Stateful (delay buffer); no external resources.
  - Vibrato (`src/effects/modulation/fx_vibrato.c`): `rate_hz`, `depth_ms`, `base_ms`, `mix`. Stateful (delay buffer); no external resources.
  - RingMod (`src/effects/modulation/fx_ringmod.c`): `freq_hz`, `mix`. Stateful (oscillator phase); no external resources.
  - AutoPan (`src/effects/modulation/fx_autopan.c`): `rate_hz`, `depth`, `phase_deg`, `mix`. Stateful (LFO); no external resources.
  - BarberpolePhaser (`src/effects/modulation/fx_barberpole_phaser.c`): `rate_hz`, `depth`, `min_Hz`, `max_Hz`, `stages`, `feedback`, `mix`, `direction`. Stateful (filters/LFO); no external resources.
- Reverb/spatial:
  - Reverb (`src/effects/reverb/fx_reverb.c`): `size`, `decay_rt60`, `damping`, `predelay_ms`, `mix`. Stateful (delay lines); no external resources.
  - EarlyReflections (`src/effects/reverb/fx_early_reflections.c`): `predelay_ms`, `width`, `mix`. Stateful (delay lines); no external resources.
  - PlateLite (`src/effects/reverb/fx_plate_lite.c`): `size`, `decay_rt60`, `highcut_Hz`, `mix`. Stateful (delay lines); no external resources.
  - GatedReverb (`src/effects/reverb/fx_gated_reverb.c`): `size`, `decay_rt60`, `thresh_dB`, `hold_ms`, `release_ms`, `mix`. Stateful (delay lines + envelope); no external resources.
- Metering (analysis-only, no params):
  - CorrelationMeter (`src/effects/metering/fx_correlation_meter.c`), MidSideMeter (`src/effects/metering/fx_mid_side_meter.c`), VectorScope (`src/effects/metering/fx_vectorscope_meter.c`), PeakRmsMeter (`src/effects/metering/fx_peak_rms_meter.c`), LufsMeter (`src/effects/metering/fx_lufs_meter.c`), SpectrogramMeter (`src/effects/metering/fx_spectrogram_meter.c`).
  - These are treated as taps: `fxm_render_*` skips processing and calls meter callback when type id 100-109 (`src/effects/effects_manager.c:606-611`, `642-646`).
- External resources: none of the above effects load files or use external IRs (no file IO in `src/effects/**/fx_*.c`).

### C11) Bypass and wet/dry
- Bypass: `FxInstance.enabled` skip (true bypass) in `fxm_render_*` (`src/effects/effects_manager.c:605-606`, `639-640`).
- Wet/dry: no global wet/dry system; some effects have `mix` param defined in their `get_desc` and implemented per effect file.

### C12) Process signature
- Standard process: `FxVTable.process(FxHandle*, const float*, float*, int, int)` in `include/effects/effects_api.h:21-33`.
- Manager uses in-place by default, or scratch buffer if `FX_FLAG_INPLACE_OK` not set (`src/effects/effects_manager.c:613-621`, `649-657`).

### C13) Block size / latency requests
- Effects have `FxDesc.latency_samples` but no engine-level handling or dynamic block size requests; render always uses engine block size.

## D) Parameter system gaps (what will fight us)

- Range definitions are heuristic and UI-driven, not part of `FxDesc`: `derive_param_range` uses name heuristics in `src/ui/effects_panel/panel.c:310-371`.
- Many params are effectively magic numbers without units beyond naming; display formatting relies on substring matching (`ms`, `Hz`, `dB`) in `src/ui/effects_panel/slot_view.c:110-142`.
- Mode-dependent params (tempo sync) are handled in UI and persistence, not in DSP:
  - `FxParamMode` stored in `FxInstance` and `SessionFxInstance`, applied in UI to convert to native values before calling engine `set_param` (`src/input/effects_panel_input.c:215-266`, `include/session.h:125-139`).
- Enum/mode params exist but are plain float parameters (e.g., `detector` in compressor, `mode` in SVF, `type` in Biquad EQ) with no explicit enum metadata; representation is float in params (`FxDesc.param_names` in each effect file).
- No read-only meter params in effects; metering is handled via separate meter effects and `EngineFxMeterSnapshot` (`src/engine/engine_meter.c:389-467`).
- Automation for FX params: not found; only clip automation for volume/pan via `EngineAutomationLane` (`include/engine/automation.h`, `src/engine/sampler.c:159-254`).
- Parameter changes are undoable in UI: `UndoCommand` created for FX edits in `src/input/effects_panel_input.c:286-323` and committed on mouse up; no versioning beyond session file version (`include/session.h:13`).

## E) UI system for effects (current state)

- UI rendering entry: `effects_panel_render` called from layout in `src/ui/layout.c:324-332` and `src/ui/effects_panel/panel.c:1322+`.
- UI structure: generic slot view with sliders for params in `src/ui/effects_panel/slot_view.c`, plus list view, EQ detail, and meter detail views in `src/ui/effects_panel/*.c`.
- Controls are generic sliders; no per-effect UI components beyond EQ and meter detail views.
- How UI knows what to display:
  - FX registry -> `FxDesc` -> `FxTypeUIInfo` with param names/ranges/kinds in `effects_panel_refresh_catalog` (`src/ui/effects_panel/panel.c:783-833`).
  - Slot renders based on `FxSlotUIState.param_count` and param names in `src/ui/effects_panel/slot_view.c`.
- Value formatting: centralized in `format_value_label` in `src/ui/effects_panel/slot_view.c:110-143` and beat labels in `format_beat_label` (`src/ui/effects_panel/slot_view.c:72-107`).
- Resizing/layout: manual pixel layout; no layout engine. `effects_panel_compute_layout` and `effects_slot_compute_layout` compute rectangles (`src/ui/effects_panel/panel.c:954-980`, `src/ui/effects_panel/slot_view.c:201+`).
- UI state location: `EffectsPanelState` in `include/app_state.h:330-386`.
- Persistence of UI state: `SessionEffectsPanelState` in `include/session.h:70-99`, saved in `src/session/session_io_write.c:239-289` and restored in `src/session/session_apply.c:160-233`.

## F) Visualization / metering / analysis (what exists already)

- Track/master meters (peak/RMS + clip hold): computed in `engine_mix_tracks` via `compute_peak_rms` and stored under `engine->meter_mutex` (`src/engine/engine_audio.c:136-171`).
- FX meters: computed from meter tap callback `engine_fx_meter_tap_callback` in `src/engine/engine_meter.c:389-449`.
- Spectrum display: ring buffer + spectrum thread -> `engine_get_spectrum_snapshot` and `engine_get_track_spectrum_snapshot` in `src/engine/engine_spectrum.c:197-209`, `370-383` and UI usage in `src/ui/effects_panel/eq_detail_view.c:222-224`.
- Spectrogram display: ring buffer + spectrogram thread -> `engine_get_fx_spectrogram_snapshot` in `src/engine/engine_spectrogram.c:314-349`, UI uses in `src/ui/effects_panel/meter_detail_view.c:547-560`.
- Data transport from audio to UI:
  - Spectrum/spectrogram use lock-free ring buffers (`RingBuffer` in `src/audio/ringbuf.c:23-133`) + background threads.
  - Meters use shared structs guarded by `engine->meter_mutex` (`src/engine/engine_meter.c:389-449`, `483-547`).
- Time-series visualization approach:
  - `EffectsMeterHistory` is updated by `meter_history_update` in `src/ui/effects_panel/meter_detail_view.c:447` and used by meter views.
- Reusable for effect visualization streams:
  - Ring buffer infrastructure (`src/audio/ringbuf.c`), spectrum/spectrogram threads (`src/engine/engine_spectrum.c`, `src/engine/engine_spectrogram.c`), FX meter tap callback (`src/engine/engine_meter.c:389-449`), meter history UI (`src/ui/effects_panel/meter_detail_view.c`), and timer HUD framework (`src/render/timer_hud_adapter.c`, `timer_hud/time_scope.h` not analyzed in depth).

## G) Real-time safety audit (audio callback + render path)

### G1) Audio callback thread (SDL audio)
- `engine_audio_callback` performs `audio_queue_read` and `memset` only (`src/engine/engine_audio.c:93-103`). No locks or allocations in callback itself.
- `audio_queue_read` uses ring buffer with atomics; no locks (`src/audio/audio_queue.c:39-47`, `src/audio/ringbuf.c:119-133`).

### G2) Worker render thread (real-time sensitive)
RT-unsafe operations in render path:
- Locks:
  - `engine->fxm_mutex` locked around FX processing: `engine_mix_tracks` and `engine_worker_main` (`src/engine/engine_audio.c:128-131`, `154-157`; `src/engine/engine_core.c:264-268`).
  - `engine->meter_mutex` locked for track/master meters and FX meters (`src/engine/engine_audio.c:140-145`, `165-170`; `src/engine/engine_meter.c:409-448`).
  - `EngineSamplerSource.automation_mutex` locked in sampler render (`src/engine/sampler.c:200-202`, `271-272`).
- Allocations / reallocations:
  - `engine_worker_main` reallocates `block_buffer` when channel count changes (`src/engine/engine_core.c:176-186`).
  - `EngineGraph` may `realloc` `mix_buffer` during render (`src/engine/graph.c:103-113`).
  - `engine_graph_add_source` uses `realloc` when rebuilding sources; may run concurrently (see G3) (`src/engine/graph.c:82-90`).
- Logging:
  - `engine_timing_trace` logs in worker thread if timing logs enabled (`src/engine/engine_core.c:238-247`).
- Other:
  - `SDL_Delay` inside worker loop for pacing (`src/engine/engine_core.c:160-167`).

### G3) Unsafe cross-thread access / races
- `engine_rebuild_sources` mutates `engine->graph` (sources + reset) without explicit synchronization with worker render (`src/engine/engine_core.c:37-77`). Worker render reads from `engine->graph` in `engine_graph_render_track` (`src/engine/engine_audio.c:122-127`). Potential data race if UI edits tracks/clips during playback.
- `engine->transport_frame` is non-atomic but read from UI via `engine_get_transport_frame` (`src/engine/engine_transport.c:100-104`) while worker updates it (`src/engine/engine_core.c:234-235`).

### G4) Denormals and clipping protection
- Denormals handled by `engine_sanitize_block`/`sanitize_sample` zeroing sub-1e-12 values and NaN/Inf (`src/engine/engine_audio.c:8-15`, `84-90`).
- Clipping protection: only sanitize (zero when abs > 64) and metering; no limiter on output. Bounce path normalizes to avoid clipping in `engine_bounce_range` (`src/engine/engine_io.c:74-99`).

## H) Serialization & project persistence

- File format: JSON written manually in `session_document_write_file` (`src/session/session_io_write.c:1-620`) and parsed in `src/session/session_io_read_parse.c` (not fully enumerated here).
- Serialization entry: `session_save_to_file` -> `session_document_capture` -> `session_document_write_file` (`src/session/session_io_write.c:618-650`).
- Effect settings persistence:
  - Stored per instance in `SessionFxInstance` with `type`, `enabled`, `param_count`, `params`, `param_modes`, `param_beats` (`include/session.h:125-139`).
  - Written in `src/session/session_io_write.c:326-366` (master FX) and `src/session/session_io_write.c:518-560` (track FX).
  - Loaded in `src/session/session_io_read_parse.c` (see `session_document_append_master_fx` and track fx parsing around `src/session/session_io_read_parse.c:254-875`).
- Versioning: `SESSION_DOCUMENT_VERSION` in `include/session.h:13`.
- Load behavior when FX list changes:
  - Session load collects `PendingMasterFx`/`PendingTrackFxEntry`, then applies in `session_apply_pending_*`.
  - `engine_fx_*_add` returns 0 if type not found; those effects are skipped (no explicit error) in `src/session/session_apply.c:523-621`.
- Preset files per effect: none found.

## I) Applied "panel framework" feasibility checks

### I1) Lowest-friction location for EffectParamSpec metadata
- Best candidate: extend `FxDesc` (in `include/effects/effects_api.h`) or add parallel metadata in registry entries (`FxRegistryEntry` in `include/effects/effects_manager.h`).
- Current UI already builds `FxTypeUIInfo` from `FxDesc` in `effects_panel_refresh_catalog` (`src/ui/effects_panel/panel.c:783-833`), so adding explicit range/unit/curve flags to `FxDesc` would remove heuristic `derive_param_range` and `format_value_label` reliance.

### I2) What needs to change for knobs/sliders/toggles/dropdowns
- Add control type metadata per param (e.g., enum) in `FxDesc` or new `EffectParamSpec` array.
- Update UI layout in `effects_slot_compute_layout` / `effects_slot_render` to render different control widgets based on control type (`src/ui/effects_panel/slot_view.c`).
- Update input handling in `effects_panel_input.c` to handle toggles and dropdown selections with discrete values.

### I3) Units + formatting
- Add explicit unit/format metadata per param (Hz, ms, dB, percent, enum) to replace substring matching in `format_value_label` (`src/ui/effects_panel/slot_view.c:110-143`).
- Centralize formatting in a new utility rather than heuristics.

### I4) Mapping curves (log/linear)
- Introduce mapping metadata (linear/log/exponential) in param spec; update `slider_value_from_mouse` and slider rendering to use mapping (`src/input/effects_panel_input.c:165-213`).
- Optionally add per-param normalization helpers in `effects/param_utils.c`.

### I5) Visualization buses (gain reduction, pre/post waveform, spectrum)
- Extend FX manager to offer tap points (pre/post) or provide side buffers; reuse meter callback or add dedicated ring buffers per active effect.
- For gain reduction, consider adding a per-effect metrics callback similar to `fxm_set_meter_tap_callback` (`src/engine/engine_meter.c:565-573`).
- For waveform/spectrum pre/post, add ring buffers and background processing similar to `engine_spectrum_*` and `engine_spectrogram_*` (`src/engine/engine_spectrum.c`, `src/engine/engine_spectrogram.c`).

### I6) ScopeHost location
- UI side: likely near `effects_panel_meter_detail_view` or new `ui/effects_panel/scope_host.c` to own visualization data; it can call engine snapshot APIs and draw.
- Engine side: add new capture targets in `engine_meter.c` or a dedicated `engine_scope.c` to manage ring buffers.

### I7) Riskiest refactor area
- RT safety and thread synchronization: current FX render and metering use locks on worker thread; adding more data flow (param specs, visualization taps) may increase contention and glitches. Also `engine_rebuild_sources` mutates graph without synchronization (possible races).

### I8) Minimal pilot effect to migrate
- Suggested pilot: Compressor (already rich param set, clear units). Files to touch:
  - `include/effects/effects_api.h` (extend metadata for params).
  - `include/effects/effects_manager.h` (if registry changes needed).
  - `src/effects/dynamics/fx_compressor.c` (fill new metadata per param).
  - `src/ui/effects_panel/panel.c` (use explicit metadata instead of `derive_param_range`).
  - `src/ui/effects_panel/slot_view.c` (formatting based on metadata).
  - `src/input/effects_panel_input.c` (mapping curves based on metadata).

## J) Output format requests

### J1) One-page current architecture summary
- Threads: UI/main thread (`src/app/main.c:331-355`), SDL audio callback (`src/audio/device_sdl.c:6-19`, `src/engine/engine_audio.c:93-103`), engine worker (`src/engine/engine_core.c:137-279`), spectrum thread (`src/engine/engine_spectrum.c:235+`), spectrogram thread (`src/engine/engine_spectrogram.c:198+`).
- Audio render order (per block): Worker thread -> `engine_mix_tracks` (track graph -> track FX -> track EQ -> pan -> meters -> sum) -> master EQ -> master FX -> sanitize -> meters; then worker applies master FX again -> sanitize -> queue (`src/engine/engine_audio.c:105-172`, `src/engine/engine_core.c:264-275`).
- Effect parameter flow: UI slider -> `apply_slider_value` -> `engine_fx_*_set_param` -> `fxm_*_set_param` -> `FxVTable.set_param` (`src/input/effects_panel_input.c:215-266`, `src/engine/engine_fx.c:179-219`, `src/effects/effects_manager.c:394-546`).
- UI flow: `effects_panel_render` from layout (`src/ui/layout.c:324-332`) -> list/slot views -> input events in `effects_panel_input.c`.
- Persistence: JSON session with FX params in `session_io_write.c` and load in `session_io_read_parse.c` / `session_apply.c`.

### J2) Top 10 blockers for new effect panel framework (with code references)
1) Param ranges/units are heuristic and UI-only (`src/ui/effects_panel/panel.c:310-371`, `src/ui/effects_panel/slot_view.c:110-143`).
2) FX params are float-only; no enum/boolean metadata (`include/effects/effects_api.h:12-27`, per-effect `*_get_desc`).
3) No central param spec; UI pulls names from `FxDesc` and invents behavior (`src/ui/effects_panel/panel.c:783-833`).
4) FX parameter updates block audio render via `fxm_mutex` (RT unsafe) (`src/engine/engine_audio.c:128-131`, `src/engine/engine_core.c:264-268`).
5) Meter taps and spectrogram updates lock `meter_mutex` in render thread (`src/engine/engine_meter.c:409-448`).
6) Graph rebuild uses `realloc` and mutates sources without synchronization (`src/engine/engine_core.c:37-77`, `src/engine/graph.c:82-90`).
7) No FX param automation system (only clip volume/pan automation) (`include/engine/automation.h`, `src/engine/sampler.c:159-254`).
8) No latency compensation; `FxDesc.latency_samples` unused (`include/effects/effects_api.h:18-27`, `src/effects/effects_manager.c`).
9) Double master FX processing path (unclear if intentional) (`src/engine/engine_audio.c:153-158` plus `src/engine/engine_core.c:264-268`).
10) UI layout is manual and tightly coupled to slot slider UI (`src/ui/effects_panel/panel.c:954-980`, `src/ui/effects_panel/slot_view.c:201+`).

### J3) Top 10 reusable components for new framework
1) FX registry and descriptors (`include/effects/effects_api.h`, `src/effects/effects_builtin.c`).
2) EffectsManager chains and snapshot APIs (`src/effects/effects_manager.c`, `src/engine/engine_fx.c`).
3) Param tempo-sync utilities (`src/effects/param_utils.c`).
4) Undo system for FX edits (`src/input/effects_panel_input.c:286-323`, `src/undo/undo_manager.c`).
5) Ring buffer infrastructure (`src/audio/ringbuf.c`).
6) Spectrum thread + snapshot APIs (`src/engine/engine_spectrum.c`).
7) Spectrogram thread + snapshot APIs (`src/engine/engine_spectrogram.c`).
8) FX meter tap callback and snapshots (`src/engine/engine_meter.c:389-563`).
9) Effects panel slot rendering/layout (generic control UI) (`src/ui/effects_panel/slot_view.c`, `src/ui/effects_panel/panel.c`).
10) Session persistence for FX state (`src/session/session_io_write.c:326-366`, `src/session/session_apply.c:523-621`).
