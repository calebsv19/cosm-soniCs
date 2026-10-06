# Undo history

Undo commands own the state required to reverse and reapply user edits. Before applying an undo or redo, the manager reserves the destination stack capacity. Failed edits retain their command on the original stack, allowing a later retry; successful edits move ownership to the opposite stack.

The timeline integration test verifies failed FX edit retries and explicit zero gain through audio/MIDI clipboard paste and undo/redo. Individual composite commands still need their remaining transactional failure audit: preserving history does not automatically roll back a partially applied command.

Clip transforms resolve stable creation identity for both audio and MIDI, falling back to sampler identity only for older states without an ID. A complete transform calls `engine_transform_clip` once for placement, bounds, gain/fades, and MIDI notes/settings. Rejection preserves the original clip and history entry without publishing intermediate field edits. The parameter and timeline tests cover rejected full transforms, retry, sampler replacement, and movement across a neighbor.

Grouped transforms now use a single engine batch transaction as documented below. Other history identity variants, whole-track restores, and caller feedback remain under the [S5 implementation audit](../../docs/improvement/S5-IMPLEMENTATION.md); they are not covered by the transform guarantee.

Track-setting snapshot undo/redo uses one engine settings transaction. A rejected snapshot retains engine values, panel values, and the history entry; successful acceptance updates the panel afterward. This covers the scalar settings snapshot, not whole-track content/session restoration.

Transform captures from production input paths retain destination track runtime identity. Multi-clip transform history uses one engine batch publication and retains its stack position on rejection. Other history command families and live compound gesture paths remain under S5.1 migration.

Gesture startup reserves undo-stack capacity, and committing the active drag transfers its owned command rather than cloning it after edits. Timeline startup rejects a partial capture of the intended target set.

An active gesture excludes undo/redo and a second gesture reservation. Availability queries agree with this refusal; rejected requests preserve the active command and both stacks until its owner commits or cancels. Inspector gain/fades reserve complete state and finalize scalar readback without allocating; inspector rename uses stable clip identity, and numeric edits reserve history before publication.

Compound drop history also guards generated trailing tracks against unrelated content/settings, removes them atomically on undo, and refreshes identities on redo. Successful history application rebuilds selection by clip identity; rejection leaves it untouched.

Slide drops attach retained before/after clip-content snapshots to the command so undo includes older neighbors trimmed, split or removed by overlap. Command clones share immutable snapshots; project teardown invalidates them safely before cache shutdown.

Generated-track drops include content and topology in one restore publication. Undo checks the existing authored-setting and effects guards and rejects unrelated clips on retiring rows. Redo reconstructs the same history-owned track identities; its high-water identity allocator continues advancing, so new track lifetimes cannot collide with restored identities.

`UNDO_CMD_CLIP_CONTENT` owns before/after selection IDs alongside retained content snapshots for compound duplicate/delete/paste. Application entry reserves all history storage before mutation; acceptance transfers ownership. History application selects the command's original or resulting identities only after successful restoration.

Pasted-content commands reserve generated-track guards before publication. Undo refuses authored-setting/effect/automation changes or unrelated clips on those rows, then restores content and topology together. Selection arrays may include zero identities to represent an empty selection.

Each successful gesture reservation receives a serial that survives clear/cancel and distinguishes stale effects releases from their current owner. EQ history applies the engine setter before changing UI or moving the history cursor. The recovery tests inject history-stack allocation rejection as well as engine-publication rejection. Whole-track restoration is now separately covered by the recovery continuation below; exhaustive history-allocation qualification remains open.


Recovery continuation: whole-track add/remove uses retained complete snapshots and one engine publication, with stable identity and neighbor guards. Mixer/EQ/FX history resolves track runtime identity. Discrete effect and mixer operations reserve history before mutation, and rejected history operations expose a temporary status message. See S5 implementation section 24 for tested scope and remaining native acceptance.
