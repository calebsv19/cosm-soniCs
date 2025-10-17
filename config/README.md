# Directory: config

Purpose: Runtime configuration and fallback assets loaded during startup.

## Files
- `engine.cfg`: INI-style settings parsed by `config_load_file` to seed engine defaults. Supported keys:
  - `sample_rate`: Target device sample rate (Hz).
  - `block_size`: Audio callback block size (frames).
  - `fade_default_in_ms` / `fade_default_out_ms`: Automatic fade durations applied to newly added clips (milliseconds).
  - `fade_presets_ms`: Comma-separated preset list (milliseconds) surfaced in the clip inspector.
  - `enable_engine_logs`: Enables extra SDL logging for engine operations (values: `on/off`, `true/false`, `1/0`).
  - `enable_cache_logs`: Enables verbose logging for the media cache acquire/release path.
  - `enable_timing_logs`: Reserved for detailed timing output during profiling (default disabled).
- `test.wav`: Fallback clip the engine loads when the user presses `L` with nothing selected in the library.
