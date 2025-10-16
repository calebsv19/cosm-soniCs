# Directory: src/config

Purpose: Load on-disk engine configuration into runtime structures.

## Files
- `config.c`
  - `config_set_defaults`: Populate an `EngineRuntimeConfig` with safe fallback values.
  - `config_load_file`: Parse key/value pairs from `engine.cfg`, override defaults, and ignore comments/whitespace.
