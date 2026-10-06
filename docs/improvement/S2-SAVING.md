# S2.1 — Atomic saves and previous-save recovery

## 1. Authorized boundary

Resume the next phase after the bounded S1 closeout. This increment addresses session-file replacement, one previous valid session, and atomic metadata writes. Report after acceptance; do not fold coherent project restore, presentation clocks, capture alignment, streaming, or deferred S1 editing work into this increment.

## 2. Behavior contract

1. A session save writes a unique same-directory candidate, checks serialization errors, flushes, syncs, closes, and parses/validates that candidate before publishing it. The primary is never opened for truncation by the session writer.
2. Before replacing an existing valid primary, copy it into a separately prepared and synced `<project>.bak`. A backup failure prevents primary replacement. A corrupt primary cannot overwrite an existing good backup. The first save has no previous version to retain.
3. Publish the candidate with one rename. Sync the parent directory afterward. Pre-rename failure returns false and preserves the primary. Post-rename directory-sync failure is a distinct `DAW_SAVE_PUBLISHED` result: bytes are visible, durability is uncertain, and a warning is logged. The session/metadata boolean APIs report successful publication rather than falsely claiming rollback.
4. Ordinary failures remove only owned temporary files. Process termination can leave `.tmp.*` candidates; loading ignores them and never promotes an incomplete candidate. No automatic orphan deletion is introduced.
5. Normal loading first reads/validates the primary; if that fails, it tries `.bak`. It logs recovery and applies the recovered document under the original project path. Neither source is rewritten by the recovery reader. If application of a valid document fails, it does not attempt another partially applied restore; that is the separate S2.2 contract.
6. Last-project markers use the same checked atomic writer. Project identity and marker updates occur only after primary publication. If the marker fails afterward, the save remains successful, the previous marker remains readable, and the log distinguishes that outcome. Preferred and legacy markers are separate transactions.
7. Media-registry persistence uses the same writer and retains `dirty` on failed publication, allowing retry. This does not make audio-file creation and session publication a multi-file transaction.

The writer preserves existing permission bits and rejects existing symlink/nonregular destinations. New files use private temporary-file permissions. The contract assumes one application writer per destination and a stable parent directory; concurrent external writers are not coordinated.

## 3. Implementation and reuse decision

- [Save-file transaction](../../src/session/save_file.c) / [API](../../include/daw/save_file.h): streamed candidate ownership, data sync, rename, directory sync, metadata and backup writes.
- [Session writer](../../src/session/session_io_write.c): schema validation, candidate readback, valid-primary backup policy, publication.
- [Session reader](../../src/session/session_io_read.c) and [application integration](../../src/session/session_apply.c): strict whole-file consumption and validated backup fallback.
- [Project manager](../../src/session/project_manager.c): marker ordering and truthful post-save marker failure reporting.
- [Media registry](../../src/audio/media_registry.c): publication-dependent dirty state.

Shared reuse scan: vendored and canonical `core_io` expose `core_io_write_all_atomic`, which takes a complete memory buffer and closes/renames a temporary file without file/directory sync or a validation boundary. `core_data` owns structured data and `core_pack` owns containers; neither supplies the required staged save contract. Decision: **reuse-deferred** for this stronger streamed save transaction. Keep the small application adapter and recovery policy local; no shared API, version, subtree, or adoption metadata changes. A future general streamed durable-write API belongs in `core_io`, not a new shared module.

## 4. Acceptance

[session_atomic_save_test](../../tests/session_atomic_save_test.c) compiles the production save adapter with test-local syscall fault injection and links the real session serializer/reader, registry, and project manager. It uses unique temporary roots, including isolated legacy project paths.

1. Initial save, replacement, retained prior revision, and permission preservation.
2. Seven failures: candidate creation, buffered flush, close, data sync, partial backup write, backup rename, and primary rename. Primary contents remain readable at the preceding revision; ordinary failures leave no candidates.
3. Child-process interruption immediately before backup or primary rename. The parent reads the prior complete primary and the appropriate complete backup. This is process-interruption evidence, not a physical power-cut test.
4. Reject invalid serialized numbers and trailing garbage; recover a missing/corrupt primary; preserve a valid backup when saving over a corrupt primary; prefer a valid primary over an older backup; reject recovery when both copies are invalid; refuse symlink destinations without modifying them.
5. Metadata rejection preserves previous bytes; a directory-sync failure after rename reports published-but-not-synced. Registry rejection retains dirty state and supports retry.
6. Real project-manager integration: failed primary publication retains project identity; failed marker publication after a successful session save retains the old marker while acknowledging the saved project.

## 5. Remaining S2 boundaries

- **S2.2 coherent capture/restore:** subsequently implemented at the authored-project/offline-engine boundary in [S2-CAPTURE-RESTORE.md](S2-CAPTURE-RESTORE.md). Device activation and user-facing recovery status remain separate.
- **Media durability:** recording/bounce currently write audio before insertion and register it afterward; session clips retain path fallbacks. WAV write-error propagation, streamed recoverable takes, and cross-file durability/order still need their dedicated slice. Atomic JSON does not prove referenced audio is durable or available.
- **Recovery policy:** one previous validated save only, not autosave history, unsaved-edit journaling, or multi-process conflict resolution. Recovery/durability warnings currently appear in logs, not a new dialog.
- **S2.3/S2.4:** subsequently implemented at the software callback boundary in [S2-CLOCKS-TRANSPORT.md](S2-CLOCKS-TRANSPORT.md). **S2.5/S2.6:** timestamped capture/alignment, media durability, and deadline diagnostics remain future work.
- S1 closeout deferrals remain recorded in [S1-CLOSEOUT.md](S1-CLOSEOUT.md).

## 6. Verification receipts

Completed commands all exited zero; [durable receipt](evidence/s2-saving.json) records exact commands, selected output, full-log hashes/locations, the sanitizer wrapper, and source fingerprints.

| Check | Result |
| --- | --- |
| `make test-session-atomic-save test-session test-data-path-contract` | Passed through real serializer/reader/registry/project-manager integration. |
| Same three targets with AddressSanitizer | Passed. |
| `make test-stable test-legacy` | Passed for the production changes. |
| Final added recovery precedence/invalid-copy/symlink cases: normal and AddressSanitizer atomic-save target | Passed after the full regression; these were test-only additions. |
| Final `make` | Passed. |
| `git diff --check`, new document links and receipt source hashes | Checked before handoff. |

This increment is complete at the software/file-publication boundary described above. No installation, release, physical power-loss, or device proof is implied. Stop and report before expanding into the next S2 slice.

Subsequent status: S2.5 and S2.6 are implemented at the software boundary documented in [recording/durability](S2-RECORDING-DURABILITY.md) and [diagnostics](S2-DIAGNOSTICS.md). Earlier future-work statements above describe this document's original handoff boundary.
