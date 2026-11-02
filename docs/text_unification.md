# Shared text and font acceptance

Last updated: 2026-10-31

soniCs Main Edit adopts kit_render 0.16.0 from committed shared source
`683068aa3c89c3570f719880b12f738c23d4b3d5`. Only that module advances; the
existing vk_renderer 1.7.0 / vk_runtime 0.6.0 pin remains `09ff89a0`.
`tools/verify-vulkan-rollout.py` checks both immutable source selections.
App VERSION remains 0.5.0; this is uncommitted development, separate from
canonical adoption, installed delivery and public release.

The font bridge prepares a complete replacement before releasing working fonts.
It retains the current requested UI scales independently of their rounded point
sizes, opens the candidate base and used sizes, then exchanges ownership. Failed
loads leave the active settings and handles usable. The bounded cache supports
16 loaded sizes and 64 remembered scale requests; capacity failure is explicit.
A failed new-size acquisition also preserves the existing handles.

Shared semantic font/zoom setters attach to the native font owner after startup.
Before that attachment they remain renderer-free for startup preference loading
and semantic tests. Once attached, candidate load failure rolls back the semantic
choice, so the normal shortcut and authoring paths cannot persist an unrendered
font choice. Loading persisted zoom reports a preparation failure. The disabled
shared-font toggle keeps its existing fixed Montserrat fallback. Startup refuses
font initialization failure through the existing application cleanup path.

Authoring retains the entry font bank until Apply or Cancel. Cancel restores the
same font handles and semantic preset/zoom without opening files; Apply releases
the retained bank. Existing input ownership, shortcuts, accepted-only preference
writes, theme presets, custom-control stubs and save/cancel behavior remain.
Font families, regular style, light hinting and kerning policy are unchanged.

Normal text opts into shared native-pixel source registration. Dense clipped text
retains the app's 512-entry texture working set and GPU-safe retirement. Both paths
preserve native raster dimensions and snap origins to drawable pixels. Logical
measurement encloses that same raster. Clipped labels use destination scissor
intersection and restore the enclosing clip; they do not stretch a cropped source
into rounded logical bounds. Long labels follow the same native placement without
requiring a cache entry. Renderer binding precedes measure-only layout and field
presentation, so painting and pointer/caret measurements use one density policy.

## Qualification

Use fresh isolated source/shared inputs and task-owned output. The legacy app
Make rules clean shared-module outputs; changing BUILD_DIR alone does not isolate
vendored dependencies. Do not run a broad clean against retained consumer builds.

The build-only `build-text-settings-proof` and `build-text-pixels-proof` targets
produce the real TTF settings lifecycle and CPU/GPU pixel oracles. Run settings in
an isolated directory with `config/` and the bundled font paths; a second process
with `--reopen` verifies accepted preferences. The pixel test takes a font path
and existing output directory, and requires a native Vulkan window and validation.

October 31 qualification passed:

- Seven injected loader refusals, partial working-set rollback, exact no-open
  Cancel, Apply/save and a separate-process reload; both presets and all ten zoom
  steps, including legacy-toggle behavior.
- Actual 1×/2× native pixel tests: 26,880,000 complete-frame pixel comparisons across
  ratios 1/1.25/1.5/2, cold/replay/640-label cache eviction, short/long labels and
  nested clips. Targeted ASan/UBSan repeats passed on the changed owners.
- Native compact field/modal and dense arrangement tests (15 tracks and 240 named
  MIDI regions), plus actual application startup captures at both scales and a
  missing-font startup refusal. Vulkan validation stayed clean in the native
  proof harnesses.
- Existing theme/font, authoring, layout, field/input, pane and lifecycle tests;
  full stable/headless audio/session regression; Main Edit package isolation.

These are controlled macOS renderer ratios, not physical external-monitor
transitions. Existing fixed control widths still clip labels at larger text sizes.
Broader typography/layout redesign, glyph coverage, grapheme/bidi/shaping, native
IME/clipboard acceptance, physical audio and other platforms remain separate.
