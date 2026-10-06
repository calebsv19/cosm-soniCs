# S3.3 entry audit — Limiter and processing latency

This is the retained pre-repair audit. The authorized implementation and acceptance contract are in [S3-DYNAMICS.md](S3-DYNAMICS.md); the original S3.2 receipt preserves the pre-repair source hashes.

## 1. Current observed behavior

A new standalone probe builds the current production limiter and measures an impulse below its ceiling at 48 kHz:

| Lookahead control | Corresponding requested frames | Observed delay | Descriptor latency |
| --- | ---: | ---: | ---: |
| 0 ms | 0 | 0 | 0 |
| 1 ms | 48 | 1 | 0 |
| 3 ms | 144 | 1 | 0 |

The supported UI and processor range is already 0–3 ms. The current read index selects the preceding buffer position, not the sample delayed by the selected duration. The zero setting's one-slot storage happens to emit the current sample. The probe's zero exit means successful measurement, not limiter acceptance.

Further current-source findings:

1. Lookahead changes allocate/reset delay storage. The effect manager applies smoothing through `set_param` while rendering, so a moving lookahead value can repeatedly enter that allocation path.
2. The descriptor exposes fixed latency only and always reports zero for this effect. The vtable has no per-instance dynamic-latency query. No engine processing-delay compensation consumes these descriptors.
3. Gain-reduction scope values are estimated from input/output RMS in the manager; a delayed transient can make that estimate differ from actual detector reduction.

## 2. Next bounded implementation contract

1. Define variable lookahead in integer project-rate samples, including true zero, and expose actual per-instance latency without pretending the static descriptor fully represents it.
2. Prepare maximum supported storage before rendering. Define a bounded, explicit history/reset policy when lookahead changes; do not allocate from render-time parameter smoothing.
3. Correct delay and detector timing together, with linked-channel ceiling/release behavior. Verify silence, impulse delay, ceiling, release, stereo linking, block partitioning, reset, and parameter transitions. This remains a sample-peak limiter unless true-peak processing is separately implemented and proven.
4. Establish the engine's track/master alignment policy before calling corrected variable-delay playback complete. A local delay-index fix alone changes relative track timing. Broader routing/latency compensation must have its own reviewed scope rather than being implied by a processor test.
5. Audit the existing compressor and gain-reduction measurement contract as the next dynamics increment; retain honest distinctions between measured processor reduction and an RMS-ratio visualization.

At this entry-audit checkpoint, S3.3 had advanced through the audit only. No production limiter, dynamics API, routing, or latency-compensation code had changed at that checkpoint. The coordinated timing contract was the next implementation boundary.

## 3. Reproduction

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -Iinclude \
  docs/improvement/probes/limiter_latency_probe.c \
  src/effects/dynamics/fx_limiter.c -lm -o /tmp/daw-s33-limiter-probe
/tmp/daw-s33-limiter-probe
```

[Probe source](probes/limiter_latency_probe.c); the original output/hash are retained in the S3.2 evidence receipt. The current probe also prints the dynamic query and its repaired output is in the S3.3 receipt. Source references: `src/effects/dynamics/fx_limiter.c`, `include/effects/effects_api.h`, `include/effects/param_specs/dynamics_param_specs.h`, and `src/effects/effects_manager.c`.
