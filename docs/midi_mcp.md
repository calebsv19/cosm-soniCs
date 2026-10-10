# MIDI MCP control

The local official-SDK server exposes 40 tools, including MIDI authoring, expression and arrangement tools backed by the same application actions as the native Patterns editor. This is qualified Main Edit source behavior. Building does not replace the installed app or register tools in an existing client session.

## 1. Creative workflow

1. Read `capabilities` for endpoint `instance`, write admission, receipt capacity and MIDI limits.
2. Find an existing `midi_region` target through `inspect_project`, or create one through the [guarded region workflow](midi_regions.md). Targets are generation/track/clip identities, not array indices.
3. Call `get_midi_region`. It returns region bounds, sample rate, counts, note and tempo pages, and a short `expected_state` token. Copy decimal frame strings without converting to floating point.
4. Supply that token, the endpoint instance and a fresh UUID request ID to `create_midi_pattern` or `set_midi_notes`. Choose `append` or `replace` explicitly. Check `ok` and `code` even when the MCP call succeeds.
5. Inspect the returned captured readback or refresh with `get_midi_region`. Use `undo_midi`/`redo_midi` with current tokens to step the matching common MIDI command at the history cursor.

Example pattern arguments:

```json
{
  "instance": "<capabilities instance>",
  "request_id": "<fresh UUID>",
  "target": {"generation": "<id>", "track_id": "<id>", "clip_id": "<id>", "kind": "midi_region"},
  "expected_state": "<get_midi_region token>",
  "mode": "replace",
  "pattern": "arpeggio",
  "root_note": 60,
  "start_beat": 0,
  "velocity": 0.65
}
```

`root_note` is the major-key tonic MIDI pitch, so 60 produces C major and 62 produces D major. `start_beat` is relative to region start, measured in quarter-note beats. Melody and arpeggio have eight onsets; chords use I/vi/IV/V triads. Each phrase spans four quarter-note beats with 85-percent gates. Onsets and ends convert separately through the captured tempo map. The generator rejects pitch overflow, nonzero source offsets and insufficient region length without extending the region. See [application MIDI behavior](midi_authoring.md).

## 2. Tool contracts and limits

| Tool | Behavior |
| --- | --- |
| `get_midi_region` | Read up to 128 notes and 64 tempo events; independent `offset` and `tempo_offset` cursors, `-1` ends each stream |
| `set_midi_notes` | Append or complete replacement of at most 64 supplied events as one undo step; empty replacement explicitly clears notes |
| `create_midi_pattern` | Generate and commit one bounded melody/chord/arpeggio phrase as one undo step |
| `undo_midi` / `redo_midi` | Step only matching common MIDI history; refuse another target, legacy MIDI entry or instrument command |

Explicit note objects contain only `start_frame` and `duration_frames` as canonical unsigned decimal strings, integer `pitch` 0–127, and finite `velocity` 0–1. Durations must be positive; all events must fit the fixed region bounds. Unknown/duplicate fields, invalid types, overflow, duplicate events, and a merged list beyond 4,096 notes reject the entire request. Writes require stopped transport/recording/Test, no active note gesture/marquee and no reserved continuous edit.

The transport keeps its 16 KiB request and 128 KiB response limits. There are at most 1,024 copied tempo events. Large regions can be read fully in bounded pages. To obtain one consistent listing, reuse the first `expected_state` on every later page; a changed or expired state rejects rather than combining different versions. For a finished stream use its total count as the offset while continuing the other stream. Offsets beyond current counts reject.

## 3. Exact state and retry ownership

Each endpoint retains eight independent full MIDI snapshots behind short tokens. Comparison uses exact semantic note values, complete region bounds/source offset, engine/track/clip identity, sample rate and every tempo event. It does not use a lossy JSON token or hash. Identical retained states reuse a token; distinct reads/writes eventually retire the oldest entry. An expired or changed token reports `state_conflict`; refresh before a deliberate new edit. Instrument, mixer and effects are outside this notes-only state precondition. Tokens are not global project revisions or authentication credentials.

MIDI writes use the existing opt-in same-user endpoint and immutable 128-receipt ledger. Admission reserves request/result storage before application mutation. Identical request bytes and ID return the original terminal response, including after its readback token expires. Different arguments with the same ID report `request_id_conflict`. Rejections and no-ops occupy receipts too; a full ledger refuses new IDs rather than evicting old results. Reads do not consume write receipts or alter notes/history.

A sent edit whose response was lost may have applied. Query `request_status` using the original instance and request ID, then reconcile readback. Never retry uncertain writes with a new ID. After endpoint restart `stale_instance` requires reconciling project state; old receipts and state tokens do not survive restart. Accepted mutations with unavailable post-edit state return `readback_required`; they must not be treated as failed edits or repeated with a new identity.

For explicit replacements larger than 64 notes there is no multi-request atomic transaction in this slice. Separate bounded edits each have their own state precondition, receipt and undo step. Guarded track/region creation and right-edge extension are covered in [region authoring](midi_regions.md). Project save/open/export and transport tools remain outside the MIDI surface.

## 4. Verification and scope

`make BUILD_DIR=<fresh-root> test-mcp-midi` exercises official SDK discovery/schema annotations, successful notes/pattern/history, exact paged reads at 4,096 notes and 1,024 tempo events, stale notes/tempo/targets, busy gestures, invalid/bounds/duplicate payloads, token expiry, read-only admission, lost-response recovery, receipt exhaustion and endpoint restart. Production float WAVs match exactly after MIDI undo/redo and saved-project reopen. Common application tests cover history/copy/publication failure and wrong-thread refusal.

The actual native App_Run acceptance independently performs MIDI reads, pattern/note edits, immutable receipt replay and undo/redo, then verifies normal project save and actual application restart under private runtime data with Vulkan validation. AddressSanitizer exercises the production adapter and cache lifetime. Existing instrument, preset and sound-analysis MCP tests and the stable headless suite remain passing.

Selected receipts/audio and native acceptance are retained under `build/receipts/mcp-midi-20261009/`. The qualified executable is `build/mcp-midi-20261009/targets/macOS-arm64/toolchains/clang/bin/daw_app`. No VERSION, shared-library source/minimum, session schema, global client registration, installed app, commit or release changes. Existing shared runtime/mailbox facilities remain reused; MIDI wire/state policy belongs to DAW rather than core_data/core_pack/core_time. Atlas was considered: this adapter is supporting source within the existing DAW control workflow, without a new plan/topology/adoption edge.

This additive control surface merits a minor-release review when release work is authorized. Future creative slices can add richer timing/key controls, region creation and arrangement operations through the same action/readback/history contracts. Physical listening preference and sustained audio-device performance are separate acceptance.
