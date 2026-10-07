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

Remaining discrete exceptions: timeline track-header mute/solo, effects track
snapshot controls and specialized EQ/meter/spec widgets. These need their own
target/gesture checks; existing behavior is retained. Continuous gesture ownership
is not migrated by wrapping button events. Next: qualify those exceptions before
shared pane composition and actual-loop fullscreen lifecycle.
