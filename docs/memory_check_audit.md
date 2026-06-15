# soniCs Memory-Check Audit

`daw` provides a default-off fisiCs memory-check lane for the session
serialization allocation path:

```sh
make -C daw memory-check-audit
```

The audit rebuilds the existing session serialization test with the
`physics-units,memory-check` overlay, links the fisiCs memory-check runtime,
and runs the generated focused test binary. This keeps the diagnostic pass
separate from the normal Clang build, desktop packaging, and release flow.

Report files:

- `daw/build/memory_check/daw.stdout`
- `daw/build/memory_check/daw.stderr`

## Current Baseline

Last audited: 2026-06-07

```text
[fisics:memory-check] summary: active=0 leaked_bytes=0 allocs=73 frees=73 double_free=0 unknown_free=0 tracker_failures=0
```
