# Sonics shared UI rollout

The first adoption slice runs in the persistent Main Edit checkout. Canonical
`daw/` retains the safely checkpointed runtime baseline `fb832eb`; VERSION remains
0.3.0. This is development qualification, not a shipped UI release.

## Accepted dependency

Managed subtree refresh `9dc47a0` imports exactly
`09ff89a0a32d80981010b15a7d45a2cdf8e5fd9f`: kit_ui 0.18.0, kit_pane 0.5.0,
vk_renderer 1.7.0 and vk_runtime 0.6.0. The qualification script compares vendor
bytes to that immutable commit rather than live shared HEAD or dirty source.

## Transport contract

`src/ui/transport_controls.c` registers LOAD, SAVE, PLAY, STOP, GRID, beats B,
Fit W and Fit H through kit_ui surfaces and focus scopes. Product rectangles,
palettes and status labels remain owned by Sonics. Rounded outline/inset fill
replaces old button chrome; measured captions are centered and clipped to their
own bounds. Rendering is synchronous and uses the native Vulkan rounded primitive.
There is one painter per migrated control.

Buttons activate on matching release. Release outside, changed captured geometry,
window invalidation, modal/text/authoring takeover, and engine replacement cancel
ownership. Tab/Shift+Tab and Enter use shared button traversal/activation. Space
and Shift+Space retain their global playback/seek meanings. Text/modal/authoring
owners retain priority. The direct engine, recording finalization, project save,
load/new and viewport command bodies remain in transport_input.c. No parallel
command registry or worker policy was introduced.

## Qualification

Use a fresh task-owned BUILD_DIR for clean objects, then run in sequence:

```sh
make BUILD_DIR=build/ui-transport-20261006 -j4
make BUILD_DIR=build/ui-transport-20261006 test-input-delivery test-engine-parameter-transaction test-session-transaction test-media-preparation
make BUILD_DIR=build/ui-transport-20261006 run-headless-smoke
make BUILD_DIR=build/ui-transport-20261006 vulkan-rollout-self-test
make BUILD_DIR=build/ui-transport-20261006 package-desktop-main-edit-self-test
make BUILD_DIR=build/ui-transport-20261006 package-desktop-main-edit-refresh
```

The native proof covers startup, logical/drawable resize, renderer restart,
2x Retina scaling and validation-clean readback. Actual one-frame dark/light UI
captures use dummy audio and task-isolated runtime data. DAW draws through Vulkan;
SDL provides events and the platform window. This does not qualify a separate SDL
renderer, physical audio, OS IME, all fullscreen transitions or other platforms.
Shared module builds keep their own BUILD_DIR rather than inheriting the app path.

## Text editing and modal focus

The second bounded slice replaces byte-oriented editing in Project Save, BPM/time
signature, library rename, track rename, inspector clip name and inspector numeric
fields. Caller-owned kit_ui text state supplies UTF-8 scalar boundaries, selection,
clipboard, atomic capacity checks, single-line policy and staged SDL composition.
Sonics retains numeric grammar, validation, engine/undo/filesystem publication,
failed-submit retention, and original commit/cancel meanings.

`text_edit.c` adapts real SDL events; `text_edit_draw.c` uses shared measured text
presentation for selection, caret, preedit, horizontal reveal and pointer position.
The synchronous painter intersects and restores the real native Vulkan enclosing
clip. Existing field backgrounds remain; replaced glyph/caret passes are removed.

`project_modal_controls.c` owns Load/Cancel shared focus and matched-release
activation. Geometry, selection and modal changes cancel captured actions.
Background transport drains interrupted releases but gives new keys to the active
owner. Modal opening cancels staged composition, nesting is refused, and closing
restores text delivery to a surviving field without discarding its typed buffer.
Focus loss clears staged composition. Load rows retain their product selection and
double-click gesture; mixed field/button traversal is not claimed for every editor.

Qualification adds:

```sh
make BUILD_DIR=build/ui-text-20261006 -j4
make BUILD_DIR=build/ui-text-20261006 test-shared-text-focus test-input-delivery test-engine-parameter-transaction
make BUILD_DIR=build/ui-text-20261006 run-headless-smoke
make BUILD_DIR=build/ui-text-20261006 build-native-text-ui-proof
```

The finite native proof renders actual Save/Load overlays in dark/light themes,
checks enclosing native clip restoration, and enables Vulkan validation. Injected
SDL composition proves adapter behavior; actual OS IME interaction and physical
audio acceptance remain separate. Evidence is retained under
`_private_workspace_artifacts/ui_unification/sonics_text_20261006`.

## Next slices

1. Timeline, library, effects and MIDI/instrument control adoption, one surface at
   a time with direct product command and one-painter checks.
2. Shared pane composition preserving DAW layout policy and subtle divider drag.
3. Fullscreen/window lifecycle qualification in the real application loop.
4. Full rollout audit and human Main Edit comparison before canonical adoption.

## 2026-10-06 editor discrete-control adoption

The next five host groups now use the pinned shared surface contract through
`editor_controls.c` and `editor_controls_layout.c`. Product layouts, colors and
engine/undo commands remain app-owned. The adapter binds accepted releases to the
original product action owner; it does not add an engine command registry.

1. Library Source/In Project modes, with sampled header activation suppressed.
2. Timeline add/remove track, MIDI region, loop, snap, automation mode/target,
   tempo overlay and automation labels.
3. MIDI editor preset menu, panel transition, Test, quantize and division,
   octave and default velocity; visible preset/category rows.
4. Instrument Notes/Preset controls, parameter group tabs and preset/category rows.
5. Effects view/spec/global preview/Add FX, overlay back/category/type controls,
   stack/detail enable/remove/preview and list enable toggles.

All registered discrete controls use matching release and cancel on interrupted
ownership. Scope context includes engine, window size, selected region creation
identity, track runtime identities, panel mode and effect-chain identity. Captured
or armed geometry changes cancel. Text/modal/authoring and active undo/pane drags
retain priority. After an editor control receives pointer focus, Tab/Shift+Tab
traverse registered editor controls and Enter activates on release; clicking
outside returns keyboard routing to the existing transport/other owners. Space
retains the global playback shortcut. This is discrete-button traversal, not
universal field/button or arbitrary widget navigation.

The existing button frame helper replaces rectangular chrome with shared compact
rounded geometry while preserving resolved product colors. Library and instrument
captions are measured/centered; effects header paint now uses that single frame
helper. A small focus underline is the only additional indicator. Product sliders,
knobs, list rows, clip/note gestures, and status visuals remain their original
painters. No generic fill is drawn over legacy controls.

Verification: fresh `make`, `test-shared-editor-controls`, `test-shared-text-focus`,
`test-input-delivery`, `test-engine-parameter-transaction`, `test-midi-editor-shell`,
then `run-headless-smoke`. `build-native-editor-controls-proof` renders actual
library/timeline/effects/MIDI/instrument painters in four frames per theme and
checks native Vulkan validation. Test execution uses an offline engine and isolated
runtime data. These proofs do not claim physical audio or arbitrary OS input.

The formerly retained track-header, snapshot and specialized EQ/meter/spec
exceptions are qualified in the follow-up below. Continuous gestures remain
app-owned. Next: shared pane composition and actual-loop fullscreen lifecycle.

Sampled pane-divider handling now skips presses owned by shared editor or transport
controls. A real routed toolbar capture plus sampled-divider regression proves
that a generous divider hit region cannot steal the pending button release.
Focused input/text and layout-sweep gates verify this follow-up; the preceding
full stable headless suite remains the broad checkpoint for this control slice.


## Discrete exceptions adoption (2026-10-06)

`editor_control_exceptions.c` registers the remaining discrete controls using
existing product geometry and the immutable kit_ui 0.18.0 surface. Timeline
mute/solo keys bind track runtime identity; their sampled press actions are
removed. Accepted releases invoke the same direct engine setters and selection
policy. Snapshot mute/solo and preset rows preserve their original undo commands.
EQ Master/Track and low/four mid/high toggles preserve the curve/history owner.
Meter MS/LR, LUFS modes and spectrogram palettes, spec toggles/dropdowns and
native/beats buttons retain the original accepted parameter publication paths.
Meter selection presentation reads back accepted parameters after activation.

These controls keep their existing specialized painters, colors and captions.
Only the existing small focus marker is added. No second frame, background fill,
plot or generic widget renderer covers them. The spec visible-body helper is
shared by its painter, legacy hit test and semantic collector, including the
six-pixel vertical inset. Sliders, knobs, curve handles, list double-clicks,
clip/note gestures and audio commands retain their product owners.

Registration follows the painted detail mode: generic effect-detail controls
are absent in EQ/meter views in both shared and legacy input paths. Snapshot
preset menus suppress underlying registered detail/row controls and dismiss
before a background detail action. Scope includes target, detail view, EQ source,
spec mode and preset menu/category state. Changed target identity, geometry,
presentation, modal/focus takeover and outside release cancel the old action.
The finite shared surface remains bounded to 256 controls; large-layout and
program-wide pane qualification remains a later acceptance boundary.

Qualification commands:

```sh
make BUILD_DIR=build/ui-exceptions-20261006 -j4
make BUILD_DIR=build/ui-exceptions-20261006 test-shared-editor-exceptions test-shared-editor-controls test-shared-text-focus test-input-delivery test-engine-parameter-transaction build-native-editor-controls-proof run-headless-smoke
make BUILD_DIR=build/ui-exceptions-20261006 package-desktop-main-edit-refresh
```

The new test uses real application routing, initialized product fonts and an
offline engine. It checks matching release, sampled duplicate suppression,
snapshot undo, preset category ownership, EQ source takeover, meter parameter
readback/undo, spec control action and both spec/legacy time-mode cancellation.
Native qualification renders twelve actual frames per dark/light theme with
Vulkan validation: existing editor groups, snapshot/menu, EQ, three meter views,
spectrogram rack and spec widgets. Enlarged lower-pane geometry uses the existing
supported mixer ratio; menu rows are absent when available height is insufficient.
Evidence: `_private_workspace_artifacts/ui_unification/sonics_exceptions_20261006`.
Physical audio, actual OS IME, other platforms and all real-loop fullscreen cases
remain separate. VERSION stays 0.3.0; this is Main Edit development delivery.

## Pane composition adoption (2026-10-06)

The sibling `pane_composition.c` adapter adopts immutable shared `09ff89a` /
kit_pane 0.5.0 shell, header and content geometry for the existing four fixed panes.
Product topology, ratios, minimum sizes, colors, fonts, specialized painters and
continuous gestures stay with Sonics. Ordinary divider resizing retains generous
hit regions and thin visuals without activating workspace authoring.

The native Vulkan clip helpers now operate on renderer state rather than being
no-ops. Pane painters receive shared region clips; nested library/effect/preview
and Load-list clips intersect the enclosing clip and restore its exact state.
One divider pass runs before modal overlays. Focus loss, hiding/minimizing, size
changes and modal/authoring takeover cancel active divider dragging; a held pointer
must release before another drag. Shared controls and active undo gestures retain
priority over sampled divider input. Last accepted pane geometry is retained.

Qualification used a fresh task build, geometry/hit/hidden-pane/clip/resize tests,
existing layout, editor, text, input and authoring tests, and stable headless smoke:

```sh
make BUILD_DIR=build/ui-panes-20261006 -j4
make BUILD_DIR=build/ui-panes-20261006 test-shared-pane-composition test-layout-sweep test-shared-editor-exceptions test-shared-editor-controls test-shared-text-focus test-input-delivery test-workspace-authoring-host test-workspace-authoring-profile build-native-editor-controls-proof run-headless-smoke
make BUILD_DIR=build/ui-panes-20261006 package-desktop-main-edit-refresh
```

Thirteen actual Vulkan frames per dark/light theme pass native validation. An exact
pixel containment check at 2x drawable scale proves all pixels outside an enclosing
clip remain unchanged while real pane content paints inside it. Evidence:
`_private_workspace_artifacts/ui_unification/sonics_panes_20261006`.
This qualifies fixed-pane composition, not generic pane trees or shared splitter
migration. Next: actual-loop fullscreen/window lifecycle, then a program-wide audit
and human comparison. Physical audio, OS IME and other-platform acceptance remain
separate. VERSION stays 0.3.0; Main Edit delivery does not promote canonical/release.
