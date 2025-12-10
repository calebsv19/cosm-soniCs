# Directory: src/session

Purpose: Session persistence helpers that translate between the live `AppState`/engine runtime and the on-disk JSON document described below.

## Session Document (`SessionDocument`)
- `version`: File format version (`SESSION_DOCUMENT_VERSION`).
- `engine`: Snapshot of `EngineRuntimeConfig` (sample rate, block size).
- `transport_playing` / `transport_frame`: Whether playback was active and the current frame.
- `loop`: Loop enabled flag plus start/end frames.
- `timeline`: Visible seconds, window start offset, vertical scale, grid visibility, and playhead frame.
- `layout`: Split ratios for transport/library/mixer panes.
- `library`: Root directory for the asset browser and currently selected index.
- `tracks[]`: Ordered list of tracks with name, gain, mute/solo flags.
  - `clips[]`: Per-track clips with asset path, clip name, gain, start/duration/offset frames, and selection flag.
    - Each clip also persists `fade_in_frames` / `fade_out_frames` (in samples) for upcoming Phase 8 ramps.

## Implementation
- `session_serialization.c`: Captures and restores `AppState`/engine state. Saving is handled by `session_save_to_file` (`session_document_capture` → `session_document_write_file`), while loading goes through `session_load_from_file` (`session_document_read_file` → validation → `session_apply_document`). Individual helpers can be reused for tooling/tests.

### JSON Sketch
```json
{
  "version": 4,
  "engine": {"sample_rate": 48000, "block_size": 128},
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
          "media_path": "assets/audio/vocal.wav",
          "gain": 0.85,
          "start_frame": 96000,
          "duration_frames": 192000,
          "offset_frames": 0,
          "fade_in_frames": 0,
          "fade_out_frames": 0,
          "selected": true
        }
      ]
    }
  ]
}
```
