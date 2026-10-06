# S5 implementation ledger

Status: active; S5.1 and a bounded S5.6 prerequisite are in progress. This ledger does not replace the complete requirements in S5-FUNCTIONAL-SLICES.md.

## 1. Execution sequence

1. Make compound editing and history reliable, including live gestures, duplication, stable identity, project replacement and rejection feedback.
2. Build clear transport/recording workflow and recovery; stop for chosen-device physical acceptance when required.
3. Qualify arrangement/inspector, mixer/FX, analysis presentation and project/background-operation workflows in focused slices.
4. Keep MCP optional and downstream of common action semantics.

## 2. Implemented initial boundary

- `engine_transform_clips` prepares complete replacement clip arrays and owned MIDI notes before publishing a single source revision. Validation, allocation or publication failure preserves the project and prior revision. It now includes required appended destination tracks in the same publication; compound duplication remains separate work.
- Multi-clip transform undo/redo calls that batch operation instead of applying each clip separately. Rejection retains history on its original stack. Tests cover seven batch allocations, owned MIDI-note allocation, invalid later target, publication rejection, cross-track identity, one-publication success, undo/redo and duplicate rejection.
- Production transform snapshots now capture track runtime identity. Undo resolves that identity after preceding-track removal and rejects a removed destination instead of using another row. Other history command families still need identity migration; this is not completion of all history safety.
- Save failure preserves the dialog and typed name; load failure preserves the dialog and selection. Both display a short retry/cancel message. Project listing deduplicates files by filesystem identity as well as spelling, covering relative/absolute aliases. Tests use isolated temporary paths and retain the current engine on failed load.

## 3. Remaining audited S5.1 paths

Live multi-move and slip previews and horizontal ripple movement now use one compound transaction. Cross-track placement now uses the compound path; ripple trim, compound duplicate/delete and overlap side effects still have separate mutation paths. Migrate whole actions, including selection/inspector updates, without accepting partial success or silently skipping missing targets. Gesture history capture now requires every intended target; final history storage is reserved before editing and active history transfers ownership on release. Failed allocation of the ripple target list and MIDI trim baseline now cancels gesture entry; injected entry-path coverage remains pending. Other indexed history types include automation, track/FX/EQ snapshots and rename; sampler-pointer history remains in clip rename/add-remove. Audit project-generation invalidation, pending/applied/rejected feedback and frame-polled shortcuts alongside these migrations.

## 4. Shared ownership decision

Reviewed core data, scene, queue, jobs and action/kit boundaries. Reuse-deferred for a new shared compound-edit abstraction: clip ownership, DSP source publication and DAW history are application semantics. Reuse the existing source-plan publication and stable runtime track identities. No shared library source/version/adoption change.

## 5. Verification and limits

The compound engine and history tests passed under AddressSanitizer after correcting a new test's borrowed MIDI pointer reuse. The initial failing sanitizer run is retained at `/tmp/daw-s51-asan.log`; the corrected passing run is `/tmp/daw-s51-asan-retest.log`. No sanitizer finding is being dismissed as a passing result. Focused normal engine/input/MIDI/fade tests and final build are recorded at this checkpoint. UI error strings are source/test verified pending native visual rehearsal; physical-device and broad S5 acceptance remain open.

Initial checkpoint validation: normal `test-engine-parameter-transaction`, `test-input-delivery`, `test-timeline-midi-region`, and `test-fade-processing` passed. AddressSanitizer engine/history and input-dialog checks passed. Required final `make` passed; `git diff --check` passed. Native visual rehearsal and broader regression remain required at later slice boundaries.

## 6. Live-preview boundary

Horizontal multi-move, audio slip and ripple-move previews derive from complete initial history and publish once. Invalid/missing later targets leave the prior project and selection unchanged; accepted previews resolve selection and inspector indices after sorting. MIDI selections move by creation identity rather than a nullable audio sampler. A mixed audio/MIDI slip is rejected because MIDI regions have no supported source-offset slip semantics. Complete cross-track release including overlap handling, and ripple trim, are not claimed atomic yet.

Focused tests exercise later-target rejection, absolute repeated preview rather than cumulative deltas, audio offsets, full undo/redo and active-history pointer ownership transfer. Normal and ASan MIDI-region tests cover this boundary. Remaining S5 requirements retain their original scope.

Live-preview checkpoint: normal engine-parameter, input-delivery and MIDI-region targets passed; ASan MIDI-region target passed; final `make` and `git diff --check` passed. Receipt: `evidence/s5-preview.json`. No native gesture acceptance or S5 slice completion is inferred from these focused tests.

## 7. Cross-track placement and growth boundary

Compound engine transforms now prepare required appended tracks and clone effects ownership before publishing the move. Publication failure restores original clip arrays, track count and effects ownership; reserved capacity and identity high-water marks may advance without exposing a new authored track. The live drop path uses this operation for the entire audio/MIDI selection, removing the previous per-sampler loop and repeated track-creation loop. Selection and transform history destinations update only after acceptance.

Tests cover growth rejection with an unchanged rendered revision, one-publication success, invalid destination rejection, cross-track MIDI placement and selection readback, plus transform undo/redo. Placement undo now removes gesture-created trailing tracks in the same publication as restoring clip placement, guarded against unrelated content or authored setting changes. Overlap processing can trim, split and remove older neighbors after placement; those changes need one integrated transaction and content-aware history. Neither complete-drop atomicity nor full S5.1 completion is claimed.

Placement checkpoint validation: normal engine-parameter, MIDI-region and input-delivery targets passed; ASan engine-parameter and MIDI-region targets passed; final `make` and whitespace checks passed. See `evidence/s5-drop-placement.json`.

## 8. Generated-track history boundary

Compound drop history owns normalized guards for each appended track. Undo requires the expected track topology and identities, unchanged mixer/instrument/name/EQ settings, no new track automation or FX, and no unrelated clips remaining after moving the gesture targets back. It then publishes clip restoration, trailing-track removal and effects topology together. Rejection preserves history and project; redo reconstructs generated tracks and refreshes their identity guards for repeated undo/redo.

Accepted undo/redo reconciles selection by clip identity and updates or clears the inspector, avoiding stale indices after track retirement or clip sorting. Gesture-command cloning now zero-initializes compound clip-state arrays so failed partial clones can be safely destroyed.

Tests cover growth/shrink publication rejection, unrelated clip preservation, mixer/FX protection, restoration of MIDI-enable side effects in the fixture, repeated undo/redo and selection readback. Overlap-neighbor capture, split/delete history, ripple trim, other command identities and native interaction acceptance remain open.

Generated-track history checkpoint: focused normal and ASan tests passed, full stable/legacy suites passed, required final `make` passed, and `git diff --check` passed. Receipt: `evidence/s5-created-track-history.json`. The earlier guard test intentionally remained rejected until its MIDI-enable side effect was restored; the corrected fixture and repeated-cycle checks passed.

## 9. Retained clip-content history prerequisite

`engine_clip_history.c` now captures complete clip content on explicitly selected tracks with stable track/clip identities. Clip metadata, MIDI notes, sampler controls and automation are independently owned; decoded media versions are retained through the existing cache rather than copied or reloaded. Restore prepares every affected row and publishes once, retaining the old revision and arrays on allocation or publication rejection. Missing track identities and clips moved outside the captured rows are rejected. Track mixer/FX settings are not replaced by this content API.

Snapshots are reference-counted on the control thread and registered with their engine. Engine destruction releases their owned content while the cache exists and invalidates outstanding handles, which can subsequently be released without touching the destroyed project. Snapshot count/media pins must remain bounded by application history ownership when the GUI adopts this API; it is not yet a claim about production history-memory performance.

Tests restore deleted, split and trimmed neighbors plus automation, compare original rendered audio exactly, restore after unlinking the source file, sweep preparation allocation failures, inject publication rejection, restore MIDI notes/automation, reject moved identities and exercise release after project destruction under ASan. This content-history foundation now supports the existing-track GUI drop integration below; generated-track overlap integration remains open.

## 10. Existing-track drop and neighbor history

Single and compound slide drops confined to existing tracks now prepare both complete history states, final placements, and older-neighbor trim/split/removal results before one source publication. Preparation/publication rejection exposes none of those candidate changes. Undo/redo restores retained content directly, including affected neighbors and track instrument settings changed by a move; decoded media is not reopened. Selection is rebuilt from surviving stable clip identities after acceptance. The release handler skips the old second overlap pass for these commands.

Release-time overlap resolution now requires an actual slide movement; click-only selection, slip and ripple release do not trigger a destructive overlap pass. Horizontal ripple placement remains covered by the earlier atomic preview path.

Tests exercise engine publication rejection with no escaped history, exactly one publication on success, complete neighbor restoration, single and multi-selection GUI action helpers, undo/redo after source removal, MIDI content and failed allocation cleanup. The source-level action helpers are exercised; native pointer/keyboard rehearsal remains separate.

Open: compound drops that append tracks still use atomic placement followed by the older overlap path. Their guarded track-removal history must be combined with retained neighbor content before full drop atomicity can be claimed. Ripple trim, compound duplicate/delete, remaining index-sensitive history and visible action rejection also remain S5.1 requirements.

## 11. Generated-track overlap integration

Compound slide drops that append tracks now prepare final placement, overlap trims/splits/removals, generated rows and both retained history states before one source publication. Undo restores neighbors and placement while retiring the generated rows in the same transaction; redo reconstructs the history-owned track identities and content. Existing authored-setting/FX guards remain active, and unrelated clips on retiring rows reject undo. Failure may reserve capacity/advance identity high-water marks but preserves authored content, track count, effects ownership and rendered revision.

Normal tests cover mixed audio/MIDI selection that splits an existing-track neighbor while creating another track, three repeated undo/redo cycles after source-file removal, publication rejection during drop/shrink/regrowth, and an allocation-failure sweep with no escaped history. Existing setting, FX and unrelated-clip guard tests now exercise this content path. Native pointer/keyboard rehearsal remains separate. The previous section's generated-track integration gap is superseded by this section; ripple trim, compound duplicate/delete, other history identities and visible rejection remain open.

The first ASan build invocation failed before tests because a recursive shared-library clean inherited the temporary build directory. It is retained at `/tmp/daw-s5-growth-asan.log`; the corrected invocation clears recursive command-line overrides. Sanitizer and final regression outcomes are recorded in the boundary receipt after completion.

Generated-track overlap checkpoint: focused normal and corrected ASan tests passed; stable/legacy regression, final default `make`, whitespace checks and documentation stale-reference checks passed. Receipt: [generated overlap evidence](evidence/s5-generated-overlap.json). S5 remains active.

## 12. Reachable trim and mixed-media ripple behavior

The gesture audit corrected the previous ripple-trim assumption: the press handler makes trim and ripple modes mutually exclusive. Ordinary edge presses select trim; Alt on an audio edge selects fade; Alt on a clip body selects ripple movement. No supported gesture entered the incremental ripple-trim branch. That unreachable incremental code is removed without assigning a new shortcut or inventing a new trim mode. A separately designed ripple-trim gesture remains future product scope if wanted; it is not an existing workflow claimed complete here.

The reachable audio left-trim path now publishes offset, duration and timeline position in one engine transform instead of two setters. Rejection preserves all three and selection; acceptance reconciles all selected clip identities after sorting. Ripple target capture now uses stable creation identities and includes downstream MIDI regions as well as audio. The complete captured action uses the existing atomic preview/history path.

Tests invoke the real press handler for ordinary right trim, Alt-edge fade and Alt-body ripple; verify mixed audio/MIDI capture, motion, later-target rejection and undo/redo. The audio-trim action test injects publication rejection and checks exactly one publication on success, plus selected anchor/neighbor identities after sorting. An initial test compile failure from pointer-valued assertions was corrected explicitly; passing verification is recorded in the boundary receipt.

Remaining S5.1 work includes compound duplicate/delete and other compound trim entry points, remaining index/pointer-sensitive history, visible action rejection, history-entry allocation injection and native interaction acceptance. This section supersedes earlier references to an existing reachable ripple-trim gesture.

Reachable trim/ripple checkpoint: corrected normal tests, focused ASan engine/gesture/input checks, stable and legacy regression, final default `make`, whitespace and documentation checks passed. Receipt: [trim and ripple evidence](evidence/s5-trim-ripple.json). Native acceptance and the broader S5 goal remain open.

## 13. Compound duplicate/delete and selection history

Timeline duplication and selection deletion now prepare all stable clip targets and retained before/after content before a single source publication. Duplicating preserves the existing placement rule (each clip's end plus one configured DSP block), names copies consistently, and includes MIDI notes/automation alongside audio metadata and retained media. Missing/repeated targets, overflow, allocation failure or rejected publication leave authored content and output identities unchanged; identity high-water marks may advance during failed preparation.

The application reserves one owned history entry before publishing. A rejected action cancels that reservation while retaining prior undo/redo history and selection. Accepted duplicate/delete transfers the reserved entry without a post-publication allocation. Undo restores original or deleted selection identities; redo selects copies or clears deleted selection. Primary selection and inspector now agree after history application. Content remains recoverable after unlinking source media. These actions reject entry while a gesture already owns active history.

The actual Ctrl/Cmd-D shortcut now uses the SDL key event's recorded modifiers, fixing a failure exposed by the event test when current global modifiers no longer match the queued event. An earlier new test also exposed primary selection remaining on the last inserted entry while the inspector showed the first; application/undo reconciliation now explicitly selects the first restored identity as primary.

Focused tests cover real shortcut dispatch, mixed audio/MIDI duplication and deletion, one history entry per selection, repeated undo/redo, source-file removal, MIDI notes/automation preservation, missing/repeated targets, intercepted preparation-allocation sweep, one publication, and application rejection preserving redo/selection. Initial failing shortcut and primary-selection runs remain in the recorded logs; corrected normal and ASan checks pass. Native interaction remains separate.

Remaining S5.1 scope includes paste and other composed edit paths, other compound trim entry points, remaining index/pointer-sensitive history, visible action failure, gesture-entry allocation injection and native interaction acceptance. Compound duplicate/delete is no longer an open implementation item for the timeline selection paths described here.

Duplicate/delete checkpoint: corrected focused normal and ASan checks, full stable/legacy regression, final default `make`, whitespace and documentation stale-reference checks passed. Receipt: [duplicate/delete evidence](evidence/s5-duplicate-delete.json). The broader S5 goal remains active.

## 14. Clipboard replacement and rejected track creation

Clipboard copy now prepares the complete selected snapshot in private owned storage. Only a fully prepared selection replaces the last clipboard. Invalid/repeated targets, excessive selection count, candidate allocation failure and a later metadata/notes/automation copy failure retain the previous clipboard; partial candidates release their owned fields. The existing primary-selection fallback and anchor policy are preserved.

The paste audit also found a loop that retried `engine_add_track` indefinitely after rejection. Paste now returns when creation fails or makes no progress. This prevents the hang but does not make multi-track creation or pasted clip/property insertion atomic. The remaining paste path still performs per-clip creation and separate setters, with separate history entries; complete retained-content insertion remains the next implementation task.

Tests inject candidate allocation failure and a later copied-entry failure after owned notes were prepared, verify the old clipboard can still paste, reject an invalid later selected entry, and verify successful whole replacement. A larger-project clipboard fixture with rejected destination-track creation returns without changing topology. These tests compile the production clipboard source under bounded fault-injection wrappers; normal timeline clipboard regression also runs.

Clipboard prerequisite checkpoint: focused normal and ASan engine/clipboard tests, final default `make`, whitespace and documentation checks passed. Full stable/legacy suites were not repeated for this bounded input change; they passed at the immediately preceding duplicate/delete boundary. Receipt: [clipboard prerequisite evidence](evidence/s5-clipboard-copy.json). Complete paste and the broader S5 goal remain open.

## 15. Complete clipboard paste transaction

Paste now adapts the complete clipboard to borrowed insertion descriptors, reserves selection/history and generated-track guards, and calls one `engine_clip_content_insert` operation. The engine prepares owned audio media/samplers, MIDI notes/instruments, gain, fades, names and automation along with any required tracks/effects topology before one source publication. Missing media, invalid later fields, allocation failure and rejected publication preserve authored content, track count, previous history and selection. Cache preparation and identity/capacity high-water marks may advance on rejection; source decoding remains synchronous in this existing control-thread path.

The existing placement contract is preserved: all copied clips go to the selected/fallback destination track; timing uses presentation playhead plus the existing nonnegative offset from the copied primary anchor. Overflow is rejected. One history entry restores or reapplies complete content and selected identities. Generated tracks are retired/recreated with content; authored settings, effects, track automation and unrelated clips guard undo. Explicit topology presence distinguishes restoration to zero tracks from an ordinary content-only restore.

History records introduced identities and rejects undo if an inserted/duplicated clip has moved outside the captured rows. This prevents a stale content undo from leaving a relocated copy behind. Snapshot invalidation and release also reclaim the identity allow-list.

Tests cover mixed audio/MIDI insertion and real copy/paste key events, relative timing and destination policy, one publication/history entry, notes/automation/zero gain and rendered audio, intercepted preparation allocation failure, invalid later fields and missing media, publication rejection, empty-project topology, generated-track setting/effect/unrelated-clip protection, moved-identity rejection and repeated undo/redo after source removal. The previous section's partial-paste warning is superseded by this implementation. Native paste/gesture rehearsal, remaining composed trim/split paths, remaining history identities and visible rejection remain open.

Complete paste checkpoint: normal focused tests, expanded ASan checks, full stable/legacy regression, final default `make`, whitespace and documentation checks passed. Receipt: [paste transaction evidence](evidence/s5-paste-transaction.json). Native workflow acceptance and the broader S5 goal remain open.

## 16. Key-edge history and edit commands

The native rehearsal accepted duplication but did not respond to a quick Cmd-Z. Global undo/redo and timeline deletion now dispatch once from SDL key events rather than sampled keyboard state. Recorded event modifiers select undo versus redo; repeat events are consumed. Modal, inspector, library, tempo and track-name text ownership remains protected, and MIDI/automation/tempo editors retain their deletion paths.

Copy, paste and duplicate also consume repeat events without executing another command. Deterministic input tests deliver complete down/up pairs with no intervening frame, cover Cmd undo/redo and Ctrl undo, preserve history during repeated keys, and verify that a subsequent ordinary paste still inserts exactly one clip with one history entry. Normal input-delivery and MIDI-region tests passed (`/tmp/daw-s5-key-edge-tests.log`).

The post-fix native rehearsal remains unqualified: replacing the temporary executable initially caused a signature failure (exit 137); refreshing the temporary wrapper's ad-hoc signature allowed startup and project restoration. CUA then returned `AXError.cannotComplete` for tree and screenshot inspection. No post-fix keyboard or visual result is claimed. The isolated dummy-audio process was stopped and its terminal exit was 0. Logs are `/tmp/daw-s5-native/postfix.stdout.log` and `/tmp/daw-s5-native/postfix.stderr.log`. This temporary wrapper was not installed or released. Remaining S5.1 composed edit paths, history identity/feedback and native acceptance remain open.

Key-edge checkpoint: focused normal and ASan checks, full stable/legacy regression, final default `make` and whitespace checks passed. Receipt: [key-edge evidence](evidence/s5-key-edges.json). The next audited transaction gap is in both MIDI left-trim apply helpers: notes, region duration and timeline start still use separate engine calls and need one atomic publication. This source finding is not yet a completed fix.

## 17. Atomic MIDI left trim

Both current-region and gesture-baseline MIDI left-trim helpers now publish shifted/clipped notes, region duration and timeline position through one existing engine transform. Clip gain, fade settings and instrument settings are carried into that transform; the engine owns validation and bounded fade adjustment. The caller's clip index changes only after acceptance, including when timeline sorting changes its index. The existing gesture and note-relative timing policy are unchanged.

Focused tests cover shortening and extension through both entry points, rejected publication, every intercepted engine preparation allocation, unchanged notes/bounds/render revision/index after rejection, and exactly one publication with correct sorted identity on success. Existing gesture-baseline note restoration and trim undo/redo tests also run. Native pointer acceptance remains separate. The pointer caller still uses the existing selection-index update path; complete multiselection reconciliation after sorting remains under audit alongside other S5.1 history and feedback work.

MIDI trim checkpoint: normal focused engine-transaction and MIDI-region tests, the intercepted allocation sweep, both focused ASan tests, default `make` and whitespace checks passed. Receipt: [MIDI trim evidence](evidence/s5-midi-trim.json). Full stable/legacy regression passed at the preceding key-edge checkpoint and was not repeated for this bounded helper change. S5 remains active.

## 18. Selection preservation through single-clip sorting

The selection-index helper now applies the complete removal/reinsertion permutation: the moved clip takes its new index, crossed neighbors shift in the opposite direction, and other tracks stay unchanged. Primary focus is remapped even when it belongs to a crossed neighbor. Both current callers operate on one clip within one track (ordinary slide preview and MIDI left trim); batch edits continue to use stable-identity reconciliation. The public helper contract explicitly excludes batch reorders.

Tests build a real multi-track MIDI selection and compare all selected creation identities after ordinary position edits and atomic MIDI trims in both directions, an unchanged position, and a partial crossing. They also verify a neighboring primary selection and untouched selection on another track. This closes the single-clip sorting issue from section 17; it does not establish native pointer acceptance or complete other history/focus paths.

Selection-sort checkpoint: focused normal and ASan MIDI-region/engine-transaction tests, default `make` and whitespace checks passed. Receipt: [selection-sort evidence](evidence/s5-selection-sort.json). Full stable/legacy suites retain their preceding key-edge result and were not repeated for this bounded permutation correction. S5 remains active.

## 19. Inspector numeric edit history and rejected input

Numeric timeline start/end/length and source start/end edits now reserve a complete clip-transform history entry before calling the existing engine setter. Rejection cancels only that reservation, preserving prior history and typed input. Success records actual scalar readback (including existing source-bound clamping) without a post-publication allocation and transfers the reserved history entry. Timeline-start edits use the returned sorted index and remap selection before refreshing the inspector. Active gesture history prevents a conflicting numeric commit.

Parsing rejects non-finite values and frame conversion checks an exclusive 2^64 bound before casting, avoiding signed rounding overflow. Invalid input remains active for correction or Escape. Inspector playback rate remains its existing local setting, with finite float-range validation; no engine playback-rate feature is introduced. An explanatory error label and stable target binding across external project changes remain follow-ups.

Tests cover all five audio fields with readback/undo/redo, source-limit clamping, MIDI position sorting and history, publication rejection, invalid/overflowing values, retained typed input and preserved redo on rejection. Rename still uses sampler-oriented history and inspector drag startup still needs an enforced history-reservation contract; this section does not claim those paths complete.

Numeric inspector checkpoint: expanded normal tests, focused ASan engine/input/MIDI tests, full stable/legacy regression, default `make` and whitespace checks passed. Receipt: [numeric inspector evidence](evidence/s5-inspector-numeric.json). Native interaction and broader S5 work remain open.

## 20. Inspector rename identity and release ownership

Clip rename history now stores creation identity rather than a sampler pointer and mutable track index. Undo/redo resolves that identity across tracks, supporting MIDI regions as well as audio and rejecting a removed target without moving the history cursor. The inspector reserves history before naming, preserves typed input when another gesture owns history or reservation fails, and transfers the accepted command without another allocation. Unchanged names do not add history.

The input audit also found that an inspector mouse-up could consume unrelated active history before the timeline received release. History finalization now requires an inspector gain/fade drag to have been active at release. A regression test routes a release with an unrelated reservation through the real inspector event handler and verifies preservation.

Tests exercise audio/MIDI rename, conflicting active history, sorted clips, cross-track movement and removed targets. This does not yet fix inspector drag-start reservation failures or incomplete fade-state capture. Those remain next work, along with stable text-edit target binding, visible rejection explanations and native interaction acceptance.

Rename/ownership checkpoint: focused normal and ASan engine/input/MIDI checks, default `make` and whitespace checks passed. Receipt: [rename and ownership evidence](evidence/s5-rename-ownership.json). Full stable/legacy suites passed at the preceding numeric-inspector checkpoint and were not repeated here. S5 remains active.

## 21. Reserved inspector gain/fade history

1. Gain, fade-track, immediate waveform-edge and delayed waveform-fade entry points now require a successful shared history reservation before enabling adjustment or changing content. An existing active reservation is preserved and the new edit is refused.
2. The shared capture uses complete clip state, including stable track identity, MIDI notes and instrument settings. Fade curve commands use the same reservation and cancel it when engine publication rejects the edit. The previous partial fade snapshots and post-edit history push are removed.
3. Release resolves the captured clip identity and updates only gain/fade scalar readback in the preallocated after-state. Accepted edits transfer the reserved entry without a new note snapshot allocation; unchanged edits cancel it. This applies to scalar gain/fade operations, not arbitrary concurrent content edits.

Tests exercise actual inspector release and fade-curve handlers, MIDI note/instrument preservation through undo/redo, conflicting reservations, refused fade startup, publication rejection and no-op history. The initial test fixture omitted timeline selection, so undo correctly cleared inspector targeting before the curve step; that failing run is retained at `/tmp/daw-s5-inspector-drag-tests.log`. The corrected fixture explicitly selects the region. Native gain/fade gestures, history-allocation injection and broader target/focus/error presentation remain open.

Inspector scalar-history checkpoint: corrected focused normal and ASan engine/input/MIDI checks, default `make` and whitespace checks passed. Receipt: [inspector drag evidence](evidence/s5-inspector-drag.json). Full stable/legacy results remain the preceding numeric-inspector checkpoint; S5 is not complete.

## 22. History exclusion during an active gesture

History availability and undo/redo application now reject while a gesture owns an active reservation. Beginning another gesture likewise rejects without destroying the existing command. This preserves the live preview and original baseline until the owner commits or cancels; it does not implicitly cancel or finish the gesture on a history shortcut.

Tests create both undo and redo entries, begin an audible gain preview, attempt direct and real shortcut undo/redo plus a competing reservation, and verify unchanged render revision, preview gain, selection, history counts and original baseline. Committing the preview then clears obsolete redo and remains undoable to the captured baseline. This guards the shared history manager; it does not establish that every legacy caller correctly handles a refused reservation.

The user requested a pause at this checkpoint. Finish validation and write the handoff only; do not begin another implementation slice without renewed instruction.

Pause boundary verified: focused normal and ASan engine/input/MIDI tests, full stable/legacy regression, default `make` and whitespace checks passed. Receipt: [pause-boundary evidence](evidence/s5-pause-boundary.json). Read [the fresh-task handoff](S5-PAUSE-HANDOFF.md) before resuming. S5 remains incomplete; implementation is stopped at the user's request.

## 23. Fresh-task recovery: effects reservation and inspector edit ownership

The user renewed implementation direction after the read-only audit. Effects sliders, track gain/pan, EQ drags and EQ reset/toggles now reserve history before enabling mutation. Effects motion/release verifies a reservation serial, protecting a newer command from stale UI flags. EQ release publishes its final pending curve before history finalization; rejected curve changes restore the last accepted preview. EQ undo/redo checks engine acceptance before changing panel values or consuming history.

Inspector numeric edits and rename buffers bind to the clip creation identity at edit startup. A changed target rejects commit and retains text. This is rejection-based target protection; it does not claim automatic text-edit migration across every selection/project transition or complete drag target binding.

Focused tests exercise actual track and EQ release handlers, history-stack allocation rejection, competing and stale reservations, gain undo, EQ engine rejection and selection changes during text edits. The first final-EQ rejection fixture omitted accepted cut values and failed; its log remains `/tmp/daw-recovery-focused-3.log`. The corrected fixture and subsequent tests pass. See `evidence/s5-recovery-20260922.json` for final command outcomes and source/log hashes.

S5.1 is still incomplete. Legacy whole-track restore can partially apply and remains the next separate engine transaction boundary. Other discrete mixer/FX history paths, stable effect/track targeting, complete clone-allocation failure coverage, visible rejection feedback and native acceptance remain open. No later S5 slice, MCP, physical audio qualification, commit or release is claimed.


## 24. Recovery continuation: atomic track and effect history

1. Whole-track add/remove history now reserves retained content before mutation. Restoration prepares clips, retained media, MIDI notes, automation, EQ, instrument settings and the effects chain before one publication. Rejection preserves editable content and the history cursor. Runtime identities and neighboring-track anchors reject stale topology; removal rejects unrecorded authored changes. Engine destruction invalidates retained snapshots safely.
2. Mixer, EQ and effect history binds to track runtime identity. An active effects gesture retains its original panel target. Track-only selection survives undo/redo. Complete instrument snapshots preserve disabled instrument parameters; legacy scalar-only commands retain compatibility.
3. Effect restoration prepares the full original instance, including its ID, parameters, modes, enabled state and order, on a private candidate before publication. Add/remove, bypass, reorder, discrete parameters, mode changes, mute/solo and instrument presets reserve history before mutation. Failed reservations or publications preserve prior state and history.
4. The timeline status banner displays brief history rejection messages. This covers the migrated history paths; it is not a claim that every legacy input failure has explanatory feedback.

Validation passed: focused engine-parameter-transaction, input-delivery and timeline-midi-region tests; the same targets under AddressSanitizer; full test-stable and test-legacy suites; default make; git diff --check. Allocation/publication fault tests cover whole-track preparation, retained audio after source deletion, MIDI/automation preservation, effect identity and disabled state, shifted/removed targets, competing reservations and undo/redo. Earlier failing fixtures exposed track-only selection loss and legacy scalar instrument compatibility; both were fixed before the final runs. Failure logs remain preserved.

An isolated, ad-hoc-signed native wrapper launched with dummy audio and rendered the two-track fixture. Native pointer delivery failed with windowNotFoundAtPosition, and the subsequent keyboard attempt failed because the Computer Use native pipe closed. The owned test process was stopped. Native edit/undo acceptance, physical audio/capture and the full import/edit/record/export/save/reopen workflow remain unverified.

Evidence: [recovery continuation receipt](evidence/s5-recovery2-20260922.json). S5 remains incomplete: other legacy history families, exhaustive allocation coverage and full native acceptance remain open. No performance qualification, commit or release is claimed. This section supersedes the whole-track/discrete-effect follow-ups in section 23, without changing historical receipts.


## 25. Main Edit acceptance findings: rack spectrogram and diagnostic banner

The September 27 Main Edit package passed readiness and package checks, then user acceptance found a blank spectrogram rack card and intrusive lifetime audio counters. The rack treated the zero-parameter spectrogram as an ordinary empty effect; analyzer subscription and rendering existed only in the separate meter-detail view. The rack now chooses one stable spectrogram target, preferring a selected meter, and draws the existing calibrated history with target-identity checks and bypass/selection messages. Non-meter list views disable the obsolete spectrogram subscription. Raw audio totals are gated by the existing timing-log setting; recording/error and undo-rejection status remain available normally.

The analyzer lifecycle regression now exercises the rack subscription and bypass invalidation across worker restarts. Final validation, installed identity and acceptance limitations are recorded in build/receipts/main-edit/20260927/CLOSEOUT.md. This remains a local development package, with S5 and physical listening/capture acceptance open.

Palette follow-up: user acceptance confirmed live spectrogram drawing but found the rack palette controls missing. W/B, B/W and Heat controls now share render/hit-test geometry, write the existing palette effect parameter through reserved undo, and render from accepted parameter readback. Tests exercise actual pointer-handler palette changes, undo and publication rejection.
