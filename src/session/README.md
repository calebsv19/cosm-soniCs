# Directory: src/session

Purpose: Session persistence helpers that translate between the live `AppState`/engine runtime and the on-disk JSON document described below.

## Session Document (`SessionDocument`)
- `version`: File format version (`SESSION_DOCUMENT_VERSION`).
- `engine`: Snapshot of `EngineRuntimeConfig` (sample rate, block size).
- `tempo`: Base tempo/time signature values for backward compatibility.
- `tempo_map`: Ordered tempo change list in beats (`beat`, `bpm`).
- `time_signature_map`: Ordered time signature change list in beats (`beat`, `ts_num`, `ts_den`).
- `transport_playing` / `transport_frame`: Whether playback was active and the current frame.
- `loop`: Loop enabled flag plus start/end frames.
- `timeline`: Visible seconds, window start offset, vertical scale, grid visibility, and playhead frame.
- `layout`: Split ratios for transport/library/mixer panes.
- `library`: Root directory for the asset browser and currently selected index.
- `tracks[]`: Ordered list of tracks with name, gain, mute/solo flags.
  - `clips[]`: Per-track clips with `kind`, asset path when audio-backed, clip name, gain, start/duration/offset frames, and selection flag.
    - Each clip also persists `fade_in_frames` / `fade_out_frames` (in samples) plus `fade_in_curve` / `fade_out_curve`.
    - Each clip can include `automation` lanes, each with a `target` and a list of `{frame, value}` points.
    - MIDI clips persist `instrument_preset`, `instrument_params`, and `midi_notes[]` with `start_frame`, `duration_frames`, `note`, and normalized `velocity`; missing `kind` is treated as audio for old sessions, missing `instrument_preset` defaults to Pure Sine, and missing params default to the selected preset.

## Implementation
- `session_serialization.c`: Captures and restores `AppState`/engine state. Saving is handled by `session_save_to_file` (`session_document_capture` → `session_document_write_file`), while loading goes through `session_load_from_file` (`session_document_read_file` → validation → `session_apply_document`). Individual helpers can be reused for tooling/tests.

### JSON Sketch
```json
{
  "version": 20,
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
          "instrument_params": {
            "level": 0.82,
            "tone": 0.73,
            "attack_ms": 12,
            "release_ms": 120
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
