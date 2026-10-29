# Gain-matched A/B listening

Main Edit provides an ephemeral comparison of two immutable production captures. It changes neither authored gains nor project history. Comparison captures are not saved in music projects.

## Native workflow

1. Stop transport and recording, open **A/B** in the effects toolbar, choose Project or Track, and set exact project-frame Start, End and Tail fields. The initial range is five seconds at the playhead plus one second tail; **At head** resets it explicitly.
2. Capture A, close the panel to edit processing, reopen and capture B with the same scope, target and range. Capturing A replaces the pair only after successful rendering. Capturing B retains A and requires identical project generation, stream geometry and range.
3. Listen A or B. **Matched** compares at equal all-channel RMS; **Original** retains their level difference with common safety attenuation. Switching side or mode during audition uses one shared cursor and a five-millisecond convex crossfade. An explicit restart begins at frame zero. Stop ends this audition; Clear releases captures.

Closing the panel or losing focus stops its audition and retains captures and range fields. A project replacement or app teardown releases them. Authoring while auditioning is refused. Listening requires an available output device and stopped transport. Ordinary transport or another preview can replace the audition; A/B Stop does not stop an unrelated preview.

## Signal and bounded-resource contract

Matching uses unweighted combined-channel RMS over the entire captured range, including its explicit tail. It is not LUFS matching and it does not collapse opposite-polarity stereo. Only the louder capture is attenuated; no matching gain boosts either side. A shared sample-peak safety attenuation keeps the selected outputs at or below 0.95 full scale. Original mode applies the same safety gain to both sides. Raw captured samples remain unchanged. Silent captures report matching unavailable and can still be auditioned in Original mode.

Material is limited to ten seconds, explicit tail to five seconds, and the pair to 64 MiB of cached PCM. Rendering prepares up to sixty seconds of preceding DSP context. Engine serial latency compensation aligns processing paths; edits that change source content can intentionally change the signal. Dry bypasses inserts and dedicated EQ without resetting authored source/mixer gains and avoids duplicated return processing.

Each side owns an independent immutable mix-provenance snapshot. Fresh comparison revision and production mix guards reject stale captures before replacement. Failed admission or rendering retains the previous pair. Callback playback allocates no memory, uses an atomic side/mode request and preserves cursor continuity even when a switch is retargeted during a crossfade.

## MCP workflow

The adapter now discovers 65 tools, including `get_ab_comparison`, `capture_ab`, `listen_ab` and `stop_ab`. Use discovered schemas rather than a hardcoded tool count.

1. Read `get_ab_comparison` and `get_mix_snapshot`; pass the current comparison token as `expected_comparison` and the production guard as `expected_mix` to `capture_ab(side="A", start_frame="0", end_frame="240000", tail_frames="48000")` at 48 kHz. Omit target for Project or supply a discovered target handle for Track. `dry` selects dry/processed capture.
2. Make the desired guarded processing edit, reread both guards and capture B using exactly the same target/range. Captured metrics and settings fingerprints identify each side; raw PCM is not exposed in JSON.
3. `listen_ab(side="A", matched=True)` starts listening. A subsequent B or mode selection switches in place; `restart=True` starts at zero. `get_ab_comparison` reports side, mode, active state and cursor. `stop_ab(clear=False)` stops; `clear=True` also releases the cache.

Capture/listen/stop require a write-enabled endpoint, current instance, unique immutable UUID request identity and exact comparison token. Capture additionally needs the exact mix guard. Readback is read-only. Replaying a receipt does not reapply an old capture or restart playback. These are ephemeral audition operations and report no authored music change.

## Verification boundaries

Native and ordinary MCP SDK fixtures verify anti-phase stereo RMS matching, immutable provenance, stale/range refusals, latency alignment with a five-sample delayed processing path, safety attenuation, silence/nonfinite rejection, session replacement, cursor continuity, crossfade retargeting and allocation failure. Tests use a paused dummy audio endpoint; headless/package proof does not establish physical listening or GUI acceptance.
