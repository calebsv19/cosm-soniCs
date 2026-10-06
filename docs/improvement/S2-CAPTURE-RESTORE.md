# S2.2 — Coherent project capture and restore

## 1. Bounded contract

Capture a complete, independently owned authored project on the control thread. Prepare a replacement project privately and retain the current project if any required preparation fails. Commit only after its engine, tracks, audio/MIDI clips, maps, automation, EQ, and effect chains are ready. This is the second focused S2 slice; stop and report after its software acceptance.

## 2. Implemented behavior

1. Capture builds and validates a private document before replacing the caller's previous document. Allocation or required FX snapshot failure frees only the candidate. Tempo/signature maps, notes, and automation cannot silently disappear on an allocation failure. Capture does not mutate the UI's EQ draft.
2. EQ configuration is retained with the engine's accepted control state and copied into render configurations. Saves obtain processor settings from that state, while retaining presentation-only EQ metadata from the UI. This avoids saving a stale or rejected UI draft as accepted sound processing.
3. Restore validates the input, clones registry storage, and builds a separate offline engine and project-owned arrays. Required track/clip/map/automation/EQ/FX failures reject the entire candidate. Missing or undecodable media and out-of-source audio regions reject restoration instead of silently skipping or shortening clips. A readable registry path is preferred, with the document path as fallback. Media-ID resolution never rewrites the input document.
4. MIDI restoration uses the complete clip transform, preserving notes, gain, fades, placement, and instrument settings. Track settings are reapplied after clip creation so implicit MIDI instrument activation cannot override an explicitly disabled saved instrument.
5. Only successful preparation retires the old engine, clears incompatible undo history and transient editing state, and transfers ownership. Failed preparation retains the old engine, playback, maps, registry, and history. Active capture, an uncommitted recorded take, and active bounce reject restore before preparation.
6. Inspector restoration and library scanning happen after commit. Clip selections, inspector references, and MIDI viewport identities map document order into the engine's chronological order. Existing callers can repeat pending-FX application safely because successful preparation clears the dirty flags.

## 3. Acceptance coverage

`tests/session_transaction_test.c` links the real engine and includes the production capture/restore implementation with test-local failure injection. It verifies:

1. Eleven capture-owned allocation failures preserve the previous complete snapshot, including maps and notes.
2. Five restore-owned allocation boundaries, engine creation rejection, track creation rejection, and FX rejection preserve the old project and its history; successful retry restores the project.
3. Missing media rejects the whole restore; the source document remains unchanged.
4. Thirty captures while an SDL dummy audio engine runs; rejected restore keeps that engine running. Non-control-thread capture and restore reject before mutation.
5. Restored maps, MIDI notes, audio gain, accepted EQ, track/master effects, history reset, and offline engine ownership; exact source-graph audio samples match before and after restoration.
6. An unsorted document restores selection and inspector identity after engine sorting.

The fault sweep covers allocations directly owned by these two translation units. It is not an exhaustive injection into every allocator inside media decoding, maps, effects, or the engine. Engine/track/FX rejection exercises propagated nested failures. Source-graph sample equality does not establish complete DSP/export parity.

## 4. Explicit limits

- The atomic boundary is authored project preparation and ownership transfer. Device opening/activation occurs afterward in the existing application caller. A device activation failure does not restore the retired engine. No physical device, listening, or visual acceptance is claimed.
- Worker play state and transport frame remain runtime observations, not a coherent rendered/consumed/presentation clock. Loading remains stopped at the existing initial position. Timeline timing and resume semantics belong to S2.3/S2.4.
- Missing media currently rejects load and logs the failure. Placeholder media, a relink workflow, and user-facing recovery/error feedback are still product work. Backup fallback remains a parse/validation recovery policy, not automatic fallback after media/application failure.
- Postcommit library scanning/persistence is a convenience refresh, outside the required project transaction. The transaction does not roll back filesystem reads, cache preparation, or newly encountered media metadata inside the discarded engine.
- Referenced WAV durability, write/close error propagation, recoverable recording streams, and session/audio cross-file ordering remain open. S2.1 atomic JSON does not close these gaps.
- Large project preparation temporarily holds two engines and decoded media and can block the control thread. Asynchronous loading/progress/cancellation and performance budgets are future work.
- S1 compound-edit, identity/feedback, and physical acceptance deferrals remain in [S1-CLOSEOUT.md](S1-CLOSEOUT.md).

## 5. Next bounded slices

1. **S2.3 clocks + S2.4 transport:** subsequently implemented at the software callback boundary in [S2-CLOCKS-TRANSPORT.md](S2-CLOCKS-TRANSPORT.md). Physical output timing remains unmeasured.
2. **S2.5 recording alignment and media durability:** timestamp captured frames, account for buffering and latency, preserve/recover takes, propagate WAV write/close failures, and verify media-before-project publication. Physical loopback acceptance is separate from software tests.
3. **S2.6 diagnostics:** make deadline misses, underruns, queue depth, and command age observable with bounded overhead and useful user-facing failure reporting.

S2 remains open. S3 addresses truthful processing and analysis: consistent fade math, limiter lookahead/dynamics latency, smoothing/discontinuities, contiguous FFT windows and calibrated/channel-labelled taps, supported media/resampling, instrument release/aliasing, and deterministic export/reset/tail behavior. Timing foundations should precede its timing-dependent work; UI cohesion remains a later phase.

## 6. Verification

The machine-readable [receipt](evidence/s2-capture-restore.json) records completed commands, exit status, log hashes, and implementation fingerprints. Sanitizer and normal builds run sequentially because shared dependency build directories are reused. No commit, installation, or release is part of this slice.

| Check | Result |
| --- | --- |
| `make test-stable test-legacy` | Passed, including session transactions and prior engine/session regressions. |
| AddressSanitizer: transaction, atomic save, recording | Passed on final fixture. |
| ThreadSanitizer: transaction | Passed, including dummy-device live capture and rejected restore. |
| Final `make` | Passed. |
| `git diff --check` | Passed. |

An intermediate sanitizer run caught shared automation ownership in the test's duplicated-clip fixture. The fixture now owns independent descriptors and omits automation on its extra selection-only clip; all final runs above include the correction.

Subsequent status: S2.5 and S2.6 are implemented at the software boundary documented in [recording/durability](S2-RECORDING-DURABILITY.md) and [diagnostics](S2-DIAGNOSTICS.md). Earlier future-work statements above describe this document's original handoff boundary.
