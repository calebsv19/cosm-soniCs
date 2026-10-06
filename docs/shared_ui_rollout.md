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

## Next slices

1. Bounded project/library/track/inspector/tempo text editing and modal focus.
2. Timeline, library, effects and MIDI/instrument control adoption, one surface at
   a time with direct product command and one-painter checks.
3. Shared pane composition preserving DAW layout policy and subtle divider drag.
4. Fullscreen/window lifecycle qualification in the real application loop.
5. Full rollout audit and human Main Edit comparison before canonical adoption.
