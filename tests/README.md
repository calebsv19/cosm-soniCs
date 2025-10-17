# Directory: tests

Purpose: Lightweight smoke/integration checks for new subsystems. Coverage now spans session persistence and the Phase 8 media cache.

## Files
- `session_serialization_test.c`: Builds a minimal `SessionDocument`, runs validation, writes it to `build/tests/sample_session.json`, reloads it via the JSON parser, and verifies the round-trip result. Run via `make test-session`.
- `media_cache_stress_test.c`: Repeatedly acquires/releases cached clips across duplicate and mixed sample-rate scenarios to shake out refcount bugs. Run via `make test-cache`.
- `clip_overlap_priority_test.c`: Ensures clips with identical start frames retain deterministic ordering after timeline edits. Run via `make test-overlap`.
- `engine_smoke_test.c`: Headless engine exercise that loads a clip, applies fades, toggles loop/playback, and verifies transport state transitions. Run via `make test-smoke`.
