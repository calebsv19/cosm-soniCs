# Directory: tests

Purpose: Lightweight smoke/integration checks for new subsystems. The current suite focuses on Phase 7 session persistence.

## Files
- `session_serialization_test.c`: Builds a minimal `SessionDocument`, runs validation, writes it to `build/tests/sample_session.json`, reloads it via the JSON parser, and verifies the round-trip result. Run via `make test-session`.
