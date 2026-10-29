# Mixing and mastering functionality assessment

Current source assessment: 2026-10-13, registered Main Edit. Headless proof and
installed local package identity are separate from human listening and GUI
acceptance. This assessment does not change the music or prescribe a loudness
target. The current system is a capable editable arrangement/mixer and now has
substantial offline diagnostics; it is not yet a complete mastering workflow.

| Area | Implemented | Remaining functional gap |
| --- | --- | --- |
| Arrangement and mixer | Nested child/group/return routing, fold/selection, compact gain/pan controls, mute/solo and undo/redo; collection-scoped UI Load/Save | A unified mixer workflow and clear persistent processing/meter views need user-level evaluation |
| EQ and processing | Dedicated EQ and ordered bus/track/master inserts; qualified Compressor/Limiter controls, serial latency and non-consuming live GR feedback in native/MCP reads | Qualification remains processor-specific; sample-peak Limiter is not a true-peak ceiling; external sidechain selection remains absent |
| Levels and headroom | Per-channel original peak/RMS, passage I/M/S and true peak, whole-song I/LRA/TP | Qualified live 400 ms sample peak/RMS and stereo/mono windows now supplement offline capture metrics; live LUFS remains unqualified and its approximate view is hidden |
| Frequency and transitions | Independent-channel time-varying frequency windows, source output tails and prepared note-ending fences/controls | Audible per-note tails remain aggregate; raw audio-file region tails and a role-aware comparison workflow are incomplete |
| Automation and routing | Authored region volume/pan and supported instrument curves; group-volume/send-level curves and qualified Gain/Pan insert curves with native point editing, persistence and MCP/history parity | Broader effect-parameter curves, group pan, return/master automation, timeline drag handles and live curve authoring remain incomplete |
| Stereo and dynamics | Stereo balance; ordinary Compressor uses independent channel envelopes, Sidechain Compressor has a linked detector; Limiter links channels | Explicit external sidechain-source routing is absent from the exposed engine/MCP lane; qualified correlation/mono-loss and output-only mono monitoring are now implemented; frequency-dependent phase and external reference sets remain separate |
| Mastering delivery | Native and guarded MCP asynchronous whole-song WAV export with format/tail choices, progress/cancel and verified result identity | Rate conversion, compressed delivery formats, surround and persistent job catalogue remain separate work |
| Comparison | Native and guarded MCP immutable A/B captures, RMS matching, Original mode and aligned crossfaded switching | External reference import and persistent repeatable reference sets remain separate work |

## Recommended order after this slice

1. **Processing qualification and control:** qualify additional selected filters,
   dynamics and ambience parameters through real routes, with timed native/MCP
   automation and history parity. Qualified live stereo/mono feedback is now implemented.
2. **Expressive processing:** qualify selected filters/dynamics/ambience curves
   beyond Gain/Pan, with exact DSP timing and native/MCP/history parity. Group pan,
   return/master faders and timeline curve handles remain separate work.
3. **Reference workflow:** external reference import and persistent comparison
   sets beyond the implemented immutable passage A/B controls. Keep listening
   gains separate from authored processing and delivery levels.
4. **Processor qualification:** validate the most-used EQ, compressor, limiter,
   de-esser, room/reverb and delay behaviors through their real routes. Qualify
   stereo linking and external sidechain routing explicitly. True-peak analysis
   does not turn the existing sample-peak limiter into a true-peak limiter.

Full official EBU programme/transient vectors and surround/LFE mapping remain
measurement qualification work, but should not displace the workflow priorities.
Musical balance, articulation, ambience and section contrast require deliberate
listening alongside these controls and measurements. No mastering target or
musical-quality acceptance is inferred from LUFS, LRA, spectrum or headroom.

Source pointers: `include/engine/automation.h`, `src/app/control_automation.c`,
`src/app/control_routing.c`, `src/app/mix_capture.c`, `src/app/loudness_range.c`,
`src/effects/dynamics/fx_compressor.c`, `fx_sidechain_compressor.c`,
`fx_limiter.c`, and `src/effects/metering/`. See [measurement contracts](mix_diagnostics.md)
and [agent capability boundaries](agent_music_capabilities.md).

See [group and send automation](mixer_automation.md) for the implemented editing,
physical gain, latency alignment, persistence and proof contract.

Gain/Pan insert automation is now implemented; see [contracts](effect_parameter_automation.md).

See [gain-matched A/B contracts](ab_listening.md) for native controls, ephemeral cache ownership and MCP guards.

See [asynchronous song export](song_export.md) for native/MCP usage, worker ownership and delivery boundaries.

See [qualified live stereo/mono metering](live_stereo_metering.md) for native/MCP controls, freshness and output-only monitoring boundaries.
