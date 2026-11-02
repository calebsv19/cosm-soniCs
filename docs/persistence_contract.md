# Document and persistence contract

This describes the supported soniCs source behavior. It does not imply that a
particular installed or released app contains these changes.

| Artifact | Authority and admission | Publication and recall |
| --- | --- | --- |
| Project/session JSON | Authored tracks, clips, resolved instrument/FX parameters, routing, automation, timeline and dependency references. Current schema 26; supported older versions retain their documented defaults. Unknown semantic keys, duplicate members, unsupported instrument identities and future versions are refused. | Detached parse and engine preparation precede adoption. Failed load retains the live engine, project identity and undo state. Open rebuilds an offline engine; it is not sample-exact DSP continuation. |
| Collection version / recovery | A named version is an authored project checkpoint, not a DSP-memory snapshot. `.bak` is one previous validated project. | New collection versions are create-only. Update current replaces its exact path. Invalid/missing primary may recover `.bak`, with an explicit product notice; neither file is rewritten by recovery. Temporary candidates are never auto-promoted. |
| Instrument / FX preset | Versioned, bounded, validated resolved parameter values. Library identity/revision governs explicit library updates. | Applying copies values; a project does not depend on later library labels or item retention. Sampled instruments still depend on their declared installed instrument banks. Presets do not capture running voices or effect history. |
| Preferences | Independent theme, font and zoom scalars; input/output/library roots. These are separate user-local preferences, not one cross-file project transaction. | Complete scalar/root candidates are admitted before replacement. Writes use checked same-directory publication. Optional missing preferences use defaults. Malformed existing preference files are retained; repair/remove them and restart before persisting replacements. Startup engine config remains a diagnostic, tolerant defaults format; malformed entries are reported, overflow integers and nonfinite values refused. |
| Workspace appearance profile (`.wapp`) | Fixed versioned pane projection and module requirements; bounded 64 KiB container. | Validated detached import; export stages the existing container, validates it, then atomically replaces. Publication is promised; directory-sync uncertainty is logged. |
| Media registry | Typed manifest of content IDs, paths and bounded metadata; derived lookup, not audio storage. | Complete validated candidate replaces accepted entries; failed parse retains the registry. Strings are escaped. File-size integers are exact in the supported range 0…2^53−1 bytes; larger values are refused. |
| WAV / recorded take | Audio result with sample rate/channel/frame meaning; take journals identify complete recoverable prefixes. | Existing staged WAV/export and checksummed journal owners distinguish publication from sync and partial takes from complete outputs. Playback is not project/engine resume. |
| Waveform/media caches | Derived from audio/source identity and format; in-memory pinned media and optional waveform pack acceleration. | Rebuilt/invalidated by existing cache owners. Missing caches do not replace authored projects. Cache removal is not media cleanup authority. |

## Open and precision

Session and registry input is limited to 16 MiB, 32 JSON nesting levels, 128
members per object, 100,000 array entries and 100,000 total JSON values before
semantic decoding. JSON numbers cannot exceed the finite float range; session
engine admission caps sample rate at 768 kHz, block size at 65,536 frames and
track count at 1,024 before runtime preparation. Registry entries additionally cap at 65,536. Strings are
validated without truncation, and numbers must be finite and syntactically
valid. Exact frame counters use checked uint64 decimal decoding rather than a
floating intermediate. Existing round-trip float serialization and dedicated
exact automation codecs remain in use. Refusing an oversized/unsupported file
leaves original bytes and accepted state intact.

## Publication and acknowledgement

The existing app-owned save adapter stages beside the destination, checks writes,
flush, file sync and close, validates session bytes, then renames (replace) or
links (create-only) and syncs the parent directory. Prepublication failure leaves
the predecessor unchanged. Postpublication directory-sync failure means **saved,
sync unconfirmed**; it does not mean rollback. The result API carries FAILED,
PUBLISHED or SYNCED. Legacy Boolean adapters mean published, not durable.

The native save dialog retains the uncertainty warning and disables further
publication controls until dismissal. A persistent status banner covers keyboard
save and recovered-project outcomes. Shutdown reports failed/unconfirmed saves
before closing. Startup markers and registry/preferences are independent metadata
transactions; their failure does not undo an already published project. Metadata
Boolean APIs promise atomic publication only; stronger sync diagnostics are logged.

Saves are synchronous on the existing application owner and capture a coherent
accepted snapshot. There is no async enqueue-as-saved baseline. One cooperating
application writer per replace destination is assumed; arbitrary external editors
are not coordinated. Create-only publication protects collisions. Preset libraries
retain their stronger lock/revision checks. No periodic unsaved-edit journal or
multi-process session merge is advertised.

## Dependencies and portability

Dropping into the library makes a complete owned copy in the configured copy root,
using atomic create-only publication; removing the original does not remove that
copy. Timeline ingestion and saved project paths are references. Save As/version
creation copies the project document, not media or caches. Referenced media must
remain accessible. Open verifies available audio against a saved nonempty content
ID and refuses changed/missing bytes before adopting the candidate. Legacy clips
without IDs, or with the historical hash of their path, derive a content ID from
their referenced bytes; those older records cannot detect prior content changes. Relative paths retain the
application runtime base; moving only the JSON is not a portable-media bundle.

The current scope does not promise physical power-cut survival, arbitrary external
writer coordination, portable installed sample banks, hardware/audio acceptance,
or exact continuation of live DSP/device state.

## Verification entry points

Use a contained source/output snapshot: `make all`, the targeted
`test-l1-persistence test-session-atomic-save test-session-transaction
 test-preset-library test-project-collections test-media-durability
 test-config-diagnostics test-session` gates, then `run-headless-smoke`
(which includes `test-stable`). `build-project-collections-native` builds the
native modal proof for an isolated runtime folder, including sync uncertainty,
repeat-save prevention and backup recovery. Do not run these fixtures against
user projects or clean retained build roots.
