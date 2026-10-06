# Authorized S3.4–S3.6 implementation

Historical scope: this checklist records the S3.4–S3.6 authorization and closeout. The later S3.7 authorization is complete separately; see [S3.7](S3-EXPORT.md). Its original stop statements below describe that earlier checkpoint.

Status: S3.4–S3.6 complete at the documented software contracts. All three reports and source-bound receipts are present; the completion map below retains every original requirement. Stop before S3.7. Manual listening/device acceptance remains separate.

## 1. S3.4 controls and transitions

1. Sample-counted existing Gain control transitions, including retargeting, reset, zero/silence semantics, channel linking, and partition independence.
2. Audit and refine live mixer gain/pan and mute/solo transitions while preserving stopped authored values, render revision ownership, and automation meaning.
3. Establish bypass transitions for existing zero-latency effects; retain explicit S3.3 history-reset policy for latency-changing edits and avoid misaligned dry/wet blending.
4. Audit seek/stop/pause/loop discontinuities. Preserve the established transport clock and safety-stop contract, and document which boundaries intentionally reset rather than promise uninterrupted audio.
5. Prove integration, parameter rejection, source/revision ownership, and callback/worker behavior. No broad FX automation feature or seamless lookahead modulation is implied.

## 2. S3.5 import and conversion

1. Reconcile the actual native WAV/platform/fallback decode paths and supported integer/float/channel formats; reject malformed/unsupported input clearly.
2. Replace linear conversion with a bounded, anti-aliased offline resampling path; preserve explicit output duration, channel identity, amplitude, and project rate.
3. Verify rate pairs, passband/stopband, DC/impulse/short boundaries, allocation/size failures, and unsupported format handling.
4. Keep decode/conversion off the callback. Streaming and large-library cache architecture remain S4.

## 3. S3.6 instruments

1. Give existing note duration a clear gate/release contract; preserve the amplitude at early note-off instead of dropping the voice.
2. Retain and retire audition release voices while stopped or playing; handle retrigger, all-notes-off, target changes, and bounded idle processing.
3. Verify existing volume/pan automation behavior for instruments alongside instrument-parameter automation.
4. Measure existing oscillator aliasing and improve supported discontinuous waveforms with a tested rate-aware approach. Do not claim complete nonlinear alias elimination without evidence.
5. Verify partition/reset determinism, polyphony, finite output, lifecycle safety, and engine integration.

## 4. Acceptance and stop

Each slice receives a current implementation report and source-bound receipt. Run focused tests, stable/legacy integration, relevant sanitizers, and final `make`. Manual listening/device/visual acceptance remains separately labeled. Preserve unrelated local work and do not commit. S3.7 export architecture is not authorized in this sequence.

## 5. Shared reuse

Retain the existing shared runtime ownership/queue integration. `core_math` offers generic primitives and `core_time` monotonic timing; neither provides DAW sample ramps, media-rate conversion, or instrument voice policy. These domain contracts remain local pending a demonstrated shared consumer; no shared library version/adoption change is currently required.

## 6. Completion audit map

The original acceptance items above remain the scope; the following maps each to inspected implementation and executable evidence.

| Requirement | Implementation | Evidence |
| --- | --- | --- |
| S3.4.1 Gain ramps, retarget/reset, endpoints, channels, partitions | `fx_gain.c`, `sample_ramp.h` | `control_transitions_test`: 44.1/48/96 kHz and 17/1024-frame partitions |
| S3.4.2 Mixer gain/pan/mute/solo and revision ownership | `graph.c`, `engine_source_plan.c`, `engine_audio.c` | Control transitions, stopped-audition sample comparison, parameter transactions, live mixer ownership |
| S3.4.3 Supported bypass transitions | `effects_manager.c` | Both bypass directions, mid-transition revision transfer, dynamics delay/alignment regressions |
| S3.4.4 Transport discontinuity policy | `engine_core_commands.c`, `engine_core.c`, graph resets | Loop source-reset versus explicit control-reset samples; transport-clock live sequences |
| S3.4.5 Integration/rejection/worker behavior | Prepared sources and manager handoff | Stable/legacy, guarded dynamics/manager processing, selected ASan/TSan |
| S3.5.1 Native/platform/fallback paths | `media_clip.c` | PCM/float/extensible fixtures, native Apple MP3 and forced FFmpeg fallback |
| S3.5.2 Anti-alias conversion, channels, amplitude, duration | `resample.c` | Rate-pair/DC/channel tests, independent passband/alias/image measurements |
| S3.5.3 Boundaries/failures | Bounded parser, coefficient/output allocation | Short input, impulse, malformed chunks, unsupported input, invalid ratio/size, injected allocations |
| S3.5.4 Offline ownership | Import/cache callers and conversion API | Source inspection: no decoder/resampler invocation in output callback; cache integration suite |
| S3.6.1 Gate/release and early note-off | `instrument_osc.c` | Independent envelope expectations through attack/decay/sustain at three rates; audible release and exact post-release silence |
| S3.6.2 Audition lifecycle | `engine_midi_audition.c`, `engine_core.c` | Retrigger/pool bound, stopped/playing retirement, target/preset reset, panic, real dummy-worker idle completion |
| S3.6.3 Existing automation | Instrument signal and parameter lane evaluation | Combined clip/track volume/pan sample comparisons; existing instrument parameter automation tests |
| S3.6.4 Measured alias improvement | `instrument_waveform.h`, preset calls | Coherent folded-component comparison and representable triangle harmonics; limits explicitly documented |
| S3.6.5 Determinism/polyphony/lifecycle/integration | Prepared instrument and audition storage | Partition/reset exactness, preset render suite, bounded retrigger stress, guarded heap calls, ASan/TSan |
| Closeout and stop | Slice reports, README/plan reconciliation, three receipts | Current source/log fingerprints, final `make`, local links and whitespace checks; no S3.7 implementation |

Reports: [S3.4](S3-CONTROL-TRANSITIONS.md), [S3.5](S3-MEDIA-CONVERSION.md), [S3.6](S3-INSTRUMENT-LIFECYCLE.md). Physical listening/device acceptance and sustained maximum-load qualification are separate evidence, not inferred from these tests.
