# Desktop Packaging

DAW (`soniCs`) supports standardized macOS app-bundle packaging and release notarization via Makefile targets.

Last updated: 2026-08-27

## Local Desktop Package

```sh
make -C daw package-desktop
```

Output:
- `dist/soniCs.app`
- target-scoped package outputs also stage under `build/targets/<target-triple>/...`

## Local Validation Gates

```sh
make -C daw package-desktop-smoke
make -C daw package-desktop-self-test
make -C daw package-desktop-refresh
```

`package-desktop-self-test` is the package proof lane for the R6 demo contract.
It is separate from the first automated proof command,
`make -C daw run-headless-smoke`.

Expected success lines:

```text
self-test: ok
package-desktop-self-test passed.
```

`package-desktop-self-test` validates launcher/binary/plist lanes, packaged
resource presence, bundled public-resource hygiene, and launcher runtime config
output. The packaged launcher may write runtime state under
`~/Library/Application Support/DAW/runtime` and logs under
`~/Library/Logs/DAW/launcher.log`, with tmp fallbacks; sandboxed agent runs may
need approval for those launcher runtime writes.

## Persistent Main Edit Package

The isolated development package uses a distinct identity and mutable-state
namespace:

- app: `soniCs Main Edit.app`
- bundle identifier: `com.cosm.sonics.main-edit`
- profile: `main-edit`
- runtime: `~/Library/Application Support/DAW-Main-Edit/runtime`
- logs: `~/Library/Logs/DAW-Main-Edit/launcher.log`
- build output: `build/targets/<target-triple>/dist/dev/main-edit/`

Build and validate it without installing or launching the app:

```sh
make -C daw package-desktop-main-edit
make -C daw package-desktop-main-edit-self-test
```

The package embeds `Contents/Resources/build_identity.json` using the generic
`codework_local_development_build_identity_v1` schema. The shared MEW1 helper
binds that file to exact source state and packaged binary bytes, while a
before/after source fingerprint guard rejects a package assembled across a
source mutation.

Desktop replacement is an explicit, separate host operation:

```sh
make -C daw package-desktop-main-edit-refresh
```

Refresh performs a read-only process audit and refuses to replace a running
Main Edit app. It cannot overwrite canonical `soniCs.app`. Opening the app is
another explicit boundary through `package-desktop-main-edit-open`.

See `docs/main_edit_worktree.md` for lane start, checkpoint, integration, and
retain/recycle gates.

Package resources are allowlisted for public/default state. Bundled config
includes the default engine/config support files and
`config/templates/public_default_project.json`; generated runtime roots,
`config/last_session.json`, local project state, and the local
`config/library_index.json` media registry are excluded. Bundled audio includes
only `assets/audio/README.md`, so ignored local user `.wav`/`.mp3` files under
`assets/audio/` are not copied into `soniCs.app`.

The ignored runtime preference `config/font_zoom_step.txt` is also excluded;
fresh packages begin from the source-defined default instead of copying a
maintainer-local UI preference.

Optional icon inputs:

```sh
make -C daw package-desktop-refresh \
  PACKAGE_APP_ICONSET_SRC="/absolute/path/AppIcon.iconset"
```

or

```sh
make -C daw package-desktop-refresh \
  PACKAGE_APP_ICON_SRC="/absolute/path/AppIcon.icns"
```

If either variable is supplied, packaging will bundle `Contents/Resources/AppIcon.icns` and the app plist will advertise `CFBundleIconFile=AppIcon`.

Default local icon store:
- `daw/tools/packaging/macos/local_app_icon/AppIcon.icns`
- `daw/tools/packaging/macos/local_app_icon/AppIcon.iconset`

Plain `make -C daw package-desktop-refresh` and `package-desktop-self-test` now look in that local store first. The local icon store is gitignored so refreshed icon copies do not dirty the normal repo worktree.

Packaging removes are guarded before `rm -rf`: app-bundle destinations must
resolve to the expected `soniCs.app` basename, and `release-clean` only removes
the `release` basename. This keeps normal `DESKTOP_APP_DIR=/path/to/soniCs.app`
overrides working while refusing root, parent-traversal, or parent-directory
destinations.

## Release Distribution Pipeline

Required variables:
- `APPLE_SIGN_IDENTITY` (Developer ID Application identity)
- `APPLE_NOTARY_PROFILE` (keychain notary profile)

One-shot release command:

```sh
make -C daw release-distribute \
  APPLE_SIGN_IDENTITY="Developer ID Application: <Name> (<TEAMID>)" \
  APPLE_NOTARY_PROFILE="cosm-notary"
```

This runs:
- `release-contract`
- `release-build`
- `release-bundle-audit`
- `release-sign`
- `release-verify-signed`
- `release-notarize`
- `release-staple`
- `release-verify-notarized`
- `release-artifact`

`release-bundle-audit` fails if generated runtime/session/project state, local
library-index metadata, local user audio, private planning docs, non-portable
local dylib linkage, or unresolved `@rpath` dylib linkage appear in the app
bundle/audit outputs.

Release outputs:
- `build/release/soniCs-<version>-<platform>-<arch>-stable.zip`
- `build/release/soniCs-<version>-<platform>-<arch>-stable.zip.sha256`
- `build/release/soniCs-<version>-<platform>-<arch>-stable.manifest.txt`

## Launcher Runtime Model

`tools/packaging/macos/daw-launcher`:
- resolves writable runtime root under `~/Library/Application Support/DAW/runtime` (tmp fallback)
- generates runtime Vulkan ICD config for MoltenVK
- seeds runtime `shaders/` and `vk_renderer/` lanes from bundled resources when required
- exports runtime Vulkan env:
  - `VK_ICD_FILENAMES`
  - `VK_DRIVER_FILES`
  - `MOLTENVK_DYLIB`
- points `VK_RENDERER_SHADER_ROOT` at the writable runtime root that contains the copied shader lanes
- launches `daw-bin` from runtime cwd
- logs startup to `~/Library/Logs/DAW/launcher.log` (tmp fallback)

Launcher diagnostics:
- `--self-test`
- `--print-config`

Intel target note:

- `TARGET_ARCH=x86_64` emits Intel artifact names in the form `soniCs-<version>-macOS-x86_64-stable.*`
- bundle closure now respects target dependency search roots instead of assuming one Homebrew lane

## Manual Validation

```sh
/Users/<user>/Desktop/soniCs.app/Contents/MacOS/daw-launcher --print-config
open /Users/<user>/Desktop/soniCs.app
tail -n 120 ~/Library/Logs/DAW/launcher.log
```

Manual microphone proof remains an operator-run package validation lane, not an
automated package gate. For the current R6 boundary, the checklist is:
- launch the packaged `soniCs.app`
- select and record-arm the intended audio track
- validate record-armed solo routing
- record with transport running, then pause/stop and confirm capture does not
  append while transport is stopped
- confirm live waveform preview and finished audio-clip insertion on the
  selected/armed track

The 2026-06-19 live packaged-app proof covered selected-track recording,
record-armed solo setup, live waveform preview, and play/pause-gated capture.
Follow-up feature work remains in the audio lane, not in package automation.

Note:
- a fresh clone will still need an `AppIcon.icns` copied into `tools/packaging/macos/local_app_icon/` before plain packaging picks it up, because that lane is intentionally ignored.

## Isolated release input

`make release-artifact-disposable RELEASE_ROOT=build/release-authenticated/<job-id>` creates a new job-owned directory containing `soniCs.app`, a ZIP, SHA-256 sidecar and source-bound manifest. It runs the existing package smoke with an isolated `DIST_DIR`; `release-package-self-test` seeds a temporary runtime under `build/` and removes it on success or failure, avoiding writes into the installed app runtime. Existing destinations, traversal and symlink ancestors are rejected before packaging. This target performs local ad-hoc signing only; Developer ID authentication and publication remain separate Decision 1 and Decision 2 stages. It does not replace installed apps.
