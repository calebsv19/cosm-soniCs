# Application control foundation

## 1. Scope and implementation sequence

1. Common action/results and a fully reserved instrument-settings action (implemented).
2. Owner-thread dispatch, instrument-target/capability snapshots and a local bridge (implemented).
3. MCP server, bounded instrument write/history surface and real-client integration tests (implemented).
4. Instrument quality and then MIDI authoring, extending MCP after each accepted feature (future).

The C application boundary is used by both the UI and the local MCP adapter.
The entry point is `include/daw/app_actions.h`; implementation lives in
`src/app/app_actions.c`. MIDI-region preset menus and knob gestures use these
actions. Track-default actions are available to C callers; existing track-rack
UI history remains on its prior reserved path.

## 2. Identity and threading

Actions execute synchronously on the engine's creating control thread. Reads
also verify ownership before accessing mutable track/clip storage. The local socket
thread marshals owned requests to that thread; it never dereferences AppState or
engine storage.

Targets carry the engine's process-local `project_generation`, track runtime
ID, and (for regions) clip creation ID. Handles survive sorting and shifted
track indices. Moving a region to another track rejects its old handle; recapture
it deliberately. Removed targets and replacement engines reject old handles,
even if an allocator reuses an address. IDs are not persisted across restarts.
Generation identifies lifetime. MCP writes additionally compare an opaque canonical
copy of the endpoint identity and target instrument stored/effective settings. This detects changed
instrument state and inherited defaults, but is not a whole-project revision or
a monotonic edit counter. Changing away and back to identical settings matches.

## 3. Instrument requests and results

Inspection copies stored settings and effective preset/parameters, distinguishing
inactive region overrides from inherited track settings. Stored `enabled` belongs
to tracks; `inherits_track` belongs to regions.

A request supplies an optional preset plus a parameter-ID bit mask and values.
Only masked values participate in validation. Selected values must be finite and
within the advertised range; unknown bits/presets reject instead of clamping.
A changed preset starts from defaults before applying the batch. Selecting the
same non-inherited preset preserves current values. Selecting a preset on an
inherited region starts an explicit override from that preset's defaults.
Parameter-only edits start from effective settings before breaking inheritance.
Track writes enable the instrument. Inheritance/disabled state restoration is
supported by history, not a new user-facing toggle action in this slice.

A successful batch produces one publication and one undo entry; exact no-ops
produce neither. Results distinguish applied, unchanged, invalid, wrong-thread,
stale-target, busy, unavailable-history and engine-rejected outcomes. Successful
writes return accepted authored readback, not worker adoption, callback delivery
or physical output. Rejections do not advertise a snapshot; inspect again on
the owner thread if needed. Reads use `UNCHANGED` with `has_snapshot=true`.

## 4. History and continuous editing

Instrument history stores scalar before/after settings, including inactive
parameters and inheritance. It does not clone or restore notes, automation,
bounds or selection. Undo/redo resolves the original generation/identities and
preserves the history cursor on rejection.

Edits reserve the final history slot before publication. An active gesture
rejects competing actions. Tokens carry target and history serial; stale
motion/release cannot consume a newer reservation. Motion addresses the original
region despite selection changes. Release transfers preallocated history without
allocating a final note snapshot. Escape restores the baseline and preserves
redo. Rejected cancellation retains the baseline for retry. Panel loss requests
cancellation; deliberate panel controls finish accepted edits before transition.
UI rejections use the existing bounded timeline history banner.

## 5. Shared reuse decision

Existing engine publication, control ownership, reserved undo and UI routing are
reused. `core_action` registers identity/triggers, not validation/transactions,
so adoption is deferred for this typed boundary. The bridge reuses the existing
`daw_mainthread_wake_push` adapter over `core_wake`. Existing `core_queue`,
`core_time`, `core_jobs`, `core_workers`, `core_wake`, `core_sched` and
`core_kernel` mechanisms remain unchanged; a single transport-owned mailbox supplies bounded dispatch ownership. The app-local
mutex/condition pair protects request/response ownership across shutdown; no new
shared scheduler or general-purpose queue is introduced.
Storage (`core_io`, `core_data`, `core_pack`, `core_memdb`), domain/measurement
(`core_scene`, `core_space`, `core_trace`, `core_math`) and UI (`core_theme`,
`core_font`, kits) do not own instrument edit semantics. New shared abstractions
are deferred; no shared API/source/version or adoption metadata changes occur.

## 6. Verification and remaining work

`test-application-instrument-action` exercises real actions, setters and history
with test-local publication/history-growth failures: batch readback, inheritance,
disabled state, no-ops, strict validation, failed writes/redo, cancellation retry,
allocation-free release, stale serials, sorting/removal/replacement and wrong
threads. `test-midi-editor-shell` exercises actual preset/knob handlers, changed
selection, Escape and stale release ownership.

`src/app/control_bridge.c` provides the opt-in, same-user private Unix endpoint.
`src/app/control_dispatch.c` validates typed wire input and calls common actions.
`tools/mcp/server.py` is a thin official Python SDK stdio adapter with seven tools:
capabilities, project target discovery, instrument read/write, scoped undo/redo and
request-status recovery. Setup and client configuration are documented in
[the MCP README](../tools/mcp/README.md). Native `main.c` starts the bridge only
when configured, polls in its background tick, invalidates the UI after dispatch,
and stops the transport before retiring application state. Only newly applied
edits request authored UI refresh; reads, no-ops, replays and rejections do not
synchronize panels or invalidate content.

Writes require `DAW_CONTROL_ALLOW_WRITE=1`, an endpoint instance nonce, a runtime
target, exact expected state and an immutable request ID. They reject active
recording, bounce, conflicting gestures, invalid values, stale identities, and
changed settings. Unknown/duplicate fields and trailing JSON reject. Request
frames are limited to 16 KiB, target pages to 64, and receipt storage to 128
admitted mutation attempts. No old receipt is evicted to make room for a new
write. Receipt memory is reserved before application mutation. Identical wire
replays return the original terminal result, while changed payloads with the
same ID reject. SDK requests are serialized canonically. A full receipt ledger
rejects new IDs; retained results remain readable. Receipts are endpoint-lifetime
memory, not a durable operation journal. A restart changes instance identity.

Scoped undo/redo checks both the current instrument state and the next history
command's target/type. It cannot undo other command kinds or skip intervening
edits. Results describe authored acceptance; subsequent UI changes do not alter
historical receipts. Always read again for current state.

Global project revisions, transport/mixer/effect tools, arrangement/MIDI authoring,
media acquisition/import, project open/save, async render jobs and measured audio
analysis remain future slices. Synthesis and illustrative preview formulas are
unchanged. No physical audio, native pointer, installed-package or release
qualification is claimed here.

### 2026-10-08 software acceptance

Focused normal action/UI/engine/project checks passed, followed by
`make BUILD_DIR=build/agent-actions-20261008 -j4 run-headless-smoke test-legacy`.
The four focused targets also passed AddressSanitizer in the separate
`build/agent-actions-asan-20261008` tree. The initial action fixture incorrectly
assumed region creation left track instruments disabled; the fixture now
explicitly disables its track. That first failing log is retained separately.
Final default-toolchain build and whitespace verification are recorded with
source/log hashes under `build/agent-actions-20261008/receipts/`.

### MCP acceptance extension

The official SDK client negotiates MCP `2025-11-25` against the production C
bridge and real engine in an isolated fixture. Tests cover tool discovery/schema,
authored readback, batch/no-op history, inheritance, replay without duplicate
history, lost-response recovery, ID collision, strict input, numeric overflow,
scoped history rejection, active gestures, simulated UI edits, project replacement,
read-only mode, receipt saturation, endpoint-bound state tokens, refresh isolation,
and endpoint cleanup with an incomplete request. The fixture does not
open user projects or physical devices. This is separate from native GUI and
physical listening acceptance.

The first socket attempt was blocked by sandbox socket permissions; local socket
integration runs require the corresponding execution permission. An expanded
shutdown check caught a blocking-accept join regression; transport acceptance now
uses bounded polling and cancellation checks. An overlapping aggregate/legacy
build encountered shared-library output deletion; verification must invoke those
aggregate lanes sequentially because the existing shared build rules clean their
own intermediate directories. Failed logs are retained separately from final
acceptance evidence.

Final sequential `run-headless-smoke` and `test-legacy` lanes passed. The final
application build passed. MCP integration passed in both normal and
AddressSanitizer fixtures; the four focused application/UI/engine/session checks
also passed under AddressSanitizer. Logs, failed-attempt labels, source hashes and
binary hashes are retained in
`build/agent-actions-20261008/receipts/mcp-baseline/acceptance.json`.
No commit, installed-app replacement or global MCP client-setting change was made.


The subsequent [instrument-control slice](instrument_controls.md) refines preset
applicability, knob mapping and diagnostic plots. Capability reads gain additive
`control_scale` metadata and supported-control lists; existing write signatures,
physical values, target preconditions and history guarantees remain unchanged.
