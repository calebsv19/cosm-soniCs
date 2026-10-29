# Directory: src/session

Session persistence now uses staged, validated atomic replacement with one previous valid `.bak` recovery copy. See [S2.1 saving contract and evidence](../../docs/improvement/S2-SAVING.md). This protects session-file publication; coherent capture/application and media durability remain separate work.


Purpose: Session persistence helpers that translate between the live `AppState`/engine runtime and the on-disk JSON document described below.

## Session Document (`SessionDocument`)
- `version`: File format version (`SESSION_DOCUMENT_VERSION`).
- `engine`: Snapshot of `EngineRuntimeConfig` (sample rate, block size).
- `tempo`: Base tempo/time signature values for backward compatibility.
- `tempo_map`: Ordered tempo change list in beats (`beat`, `bpm`).
- `time_signature_map`: Ordered time signature change list in beats (`beat`, `ts_num`, `ts_den`).
- `transport_playing` / `transport_frame`: Whether playback was active and the current frame.
- `loop`: Loop enabled flag plus start/end frames.
- `timeline`: Visible seconds, window start offset, vertical scale, grid visibility, playhead frame, and the snap/automation/tempo-overlay UI toggles used by the main timeline.
- `midi_editor`: Selected MIDI-region lower-pane state, including note-editor vs instrument-panel mode, the active instrument parameter group tab, quantize/default-velocity controls, and stored time/pitch viewport positions.
- `layout`: Split ratios for transport/library/mixer panes.
- `library`: Root directory for the asset browser, current panel mode, and currently selected index.
- `active_track_index` / `selected_track_index` / `selected_clip_index` / `selection[]`: Active track focus plus multi-clip selection state.
- `tracks[]`: Ordered list of tracks with name, gain, mute/solo flags, optional MIDI instrument defaults (`midi_instrument_enabled`, `midi_instrument_preset`, named-key `midi_instrument_params`), and optional track-level MIDI instrument automation (`midi_instrument_automation`) for inherited MIDI regions.
  - `clips[]`: Per-track clips with `kind`, asset path when audio-backed, clip name, gain, start/duration/offset frames, and selection flag.
    - Each clip also persists `fade_in_frames` / `fade_out_frames` (in samples) plus `fade_in_curve` / `fade_out_curve`.
    - Each clip can include `automation` lanes, each with a `target` and a list of `{frame, value}` points.
    - MIDI clips persist `instrument_preset`, `instrument_inherits_track`, named-key `instrument_params`, and `midi_notes[]` with `start_frame`, `duration_frames`, `note`, and normalized `velocity`; missing `kind` is treated as audio for old sessions, missing `instrument_inherits_track` defaults to explicit per-region sound, missing `instrument_preset` defaults to Pure Sine, missing params default to the selected preset, and unknown future param keys are ignored.

## Implementation
- `session_document.c`, `session_io_write.c`, `session_io_read.c`, and `session_apply.c`: Capture, persist, and restore `AppState`/engine state. Saving is handled by `session_save_to_file` (`session_document_capture` → `session_document_write_file`), while loading goes through `session_load_from_file` (`session_document_read_recoverable` → primary or validated backup → `session_apply_document`). Individual helpers can be reused for tooling/tests.

### JSON Sketch
```json
{
  "version": 23,
  "engine": {"sample_rate": 48000, "block_size": 128},
  "tempo": {"bpm": 120, "ts_num": 4, "ts_den": 4},
  "tempo_map": [{"beat": 0, "bpm": 120}, {"beat": 32, "bpm": 140}],
  "time_signature_map": [{"beat": 0, "ts_num": 4, "ts_den": 4}, {"beat": 64, "ts_num": 3, "ts_den": 4}],
  "transport_playing": false,
  "transport_frame": 0,
  "loop": {"enabled": true, "start_frame": 96000, "end_frame": 144000},
  "timeline": {
    "visible_seconds": 8.0,
    "window_start_seconds": 0.0,
    "vertical_scale": 1.0,
    "show_all_grid_lines": false,
    "playhead_frame": 96000
  },
  "selected_track_index": 0,
  "selected_clip_index": 1,
  "midi_editor": {
    "panel_mode": 1,
    "instrument_active_group": 4
  },
  "layout": {"transport_ratio": 0.3, "library_ratio": 0.25, "mixer_ratio": 0.45},
  "library": {"directory": "assets/audio", "selected_index": 2},
  "tracks": [
    {
      "name": "Vocal",
      "gain": 0.9,
      "muted": false,
      "solo": false,
      "clips": [
        {
          "name": "Vocal Take",
          "kind": "audio",
          "media_path": "assets/audio/vocal.wav",
          "gain": 0.85,
          "start_frame": 96000,
          "duration_frames": 192000,
          "offset_frames": 0,
          "fade_in_frames": 0,
          "fade_out_frames": 0,
          "fade_in_curve": 0,
          "fade_out_curve": 0,
          "automation": [
            {
              "target": 0,
              "points": [
                {"frame": 24000, "value": 0.5}
              ]
            }
          ],
          "midi_notes": [],
          "selected": true
        },
        {
          "name": "Keys",
          "kind": "midi",
          "media_id": "",
          "media_path": "",
          "gain": 0.9,
          "start_frame": 192000,
          "duration_frames": 96000,
          "offset_frames": 0,
          "fade_in_frames": 0,
          "fade_out_frames": 0,
          "fade_in_curve": 0,
          "fade_out_curve": 0,
          "automation": [],
          "instrument_preset": "saw_lead",
          "instrument_inherits_track": false,
          "instrument_params": {
            "level": 0.82,
            "tone": 0.73,
            "attack_ms": 12,
            "decay_ms": 180,
            "sustain": 0.64,
            "release_ms": 120,
            "osc_mix": 0.55,
            "osc2_detune": 9,
            "sub_mix": 0.2,
            "drive": 0.33,
            "vibrato_rate": 5.5,
            "vibrato_depth": 11
          },
          "midi_notes": [
            {"start_frame": 0, "duration_frames": 24000, "note": 60, "velocity": 0.9}
          ],
          "selected": false
        }
      ]
    }
  ]
}
```


### Atomic save and recovery

`save_file.c` owns a same-directory candidate through flush, file sync, close, validation, rename, and directory sync. Existing destinations are preserved on prepublication failure. A directory-sync failure after rename is reported distinctly in logs because the new file is already published. New files are private; existing regular-file permission bits are retained; symlink destinations are rejected.

The session writer validates candidate bytes and retains the previous valid primary as `<path>.bak` before replacement. Loading tries the validated backup only if primary reading/validation fails, logs recovery, and leaves files untouched. Temporary crash leftovers are ignored. Last-project markers publish after the session; registry dirty state clears only after its own successful publication. `make test-session-atomic-save` covers file-operation failures, process interruption, recovery, and project/marker ordering.

### Coherent project transactions

Capture is a control-thread operation that prepares a deep-owned document and replaces the previous snapshot only on success. Accepted EQ settings come from the engine rather than mutable UI drafts. Restore prepares a private offline engine, registry, maps, clips, automation, EQ, and FX before transferring project ownership; required failures, including missing media, preserve the old project and history. Successful commit clears obsolete history and editing state and remaps clip references after sorting. Input documents remain immutable. Inspector/library refresh follows commit; device activation remains the caller's separate step. See [the S2.2 contract](../../docs/improvement/S2-CAPTURE-RESTORE.md) and `make test-session-transaction`.


S4.5 adds the optional engine property `output_queue_blocks` (integer 2..32). Serialization preserves the requested value, old documents default to 32, and parser/validation reject invalid values. Engine restoration uses the saved runtime configuration when constructing the replacement engine. The effective render-ahead target is computed at device startup and may be raised for callback headroom; it is not serialized as a physical latency promise.


S4.7 invalidates asynchronous import generations only at successful project replacement, before retiring the old engine. Failed candidate preparation preserves pending work. Restore stays atomic and synchronous; pending imports are not serialized as published clips. See [S4.7](../../docs/improvement/S4.7-MEDIA.md).

### Shared return routing

Track documents optionally contain `is_return` and exactly four `sends` entries. Each send stores `destination_track` (one-based position in the complete saved track array, zero for no binding) and a finite linear `gain` from zero to one. Missing legacy routing fields default to ordinary source lanes and disconnected sends. Runtime identities are not saved; capture resolves their current destinations into the owned document, and restoration binds them to fresh runtime identities only after every lane exists.

Validation refuses occupied/instrument-enabled returns, more than four returns, missing/non-return destinations, duplicate bindings, return-to-return sends and invalid levels. Routed projects are bounded to 128 tracks. The parser also refuses duplicate routing keys, incomplete/extra slots, unknown send fields and fractional destinations. Routing is restored inside the private candidate project; allocation or publication failure preserves the current live project. Complete return effect settings use existing embedded track effect storage.

`test-session-transaction` now verifies exact send gains, fresh destination rebinding, return effects and bit-identical production audio after disk serialization/reopening, seven routed restore allocation failures and explicit routing-publication refusal. Normal/ASan and stable regression checks pass. Actual native App_Run additionally verifies loaded routing, return-chain MCP parameter edits/undo, independent dry/wet ratios, normal save and real restart with exact metrics/plots/WAV. Route creation in that acceptance is fixture-authored; native/MCP route authoring remains pending.

Piano expression extension: [piano expression](../../docs/piano_expression.md) describes the three piano-only controls, binary region/live sustain, 88 string modes, recorded key/pedal mechanics, 32-field v6 presets and session v25. Older preset versions 1–5 and session v24 remain readable; existing non-piano voices keep their behavior.

[Collection-scoped Load/Save](../../docs/project_collections.md) now isolates folder lists, previews monotonically versioned checkpoints and preserves exact current-project save paths. Native collection and publication tests cover the shared modal geometry and atomic no-replace writer.

`session_json_string.c` decodes raw UTF-8 and JSON Unicode escapes equivalently, including surrogate pairs, and refuses malformed/overflow strings instead of silently substituting or truncating label bytes. Session document ownership and atomic apply boundaries remain unchanged.
