# Desktop Packaging

DAW (`soniCs`) supports standardized macOS app-bundle packaging and release notarization via Makefile targets.

Last updated: 2026-04-25

## Local Desktop Package

```sh
make -C daw package-desktop
```

Output:
- `dist/soniCs.app`

## Local Validation Gates

```sh
make -C daw package-desktop-smoke
make -C daw package-desktop-self-test
make -C daw package-desktop-refresh
```

`package-desktop-self-test` validates launcher/binary/plist lanes, packaged resource presence, and launcher runtime config output.

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

Release outputs:
- `build/release/soniCs-<version>-macOS-stable.zip`
- `build/release/soniCs-<version>-macOS-stable.zip.sha256`
- `build/release/soniCs-<version>-macOS-stable.manifest.txt`

## Launcher Runtime Model

`tools/packaging/macos/daw-launcher`:
- resolves writable runtime root under `~/Library/Application Support/DAW/runtime` (tmp fallback)
- generates runtime Vulkan ICD config for MoltenVK
- exports runtime Vulkan env:
  - `VK_ICD_FILENAMES`
  - `VK_DRIVER_FILES`
  - `MOLTENVK_DYLIB`
- launches `daw-bin` from runtime cwd
- logs startup to `~/Library/Logs/DAW/launcher.log` (tmp fallback)

Launcher diagnostics:
- `--self-test`
- `--print-config`

## Manual Validation

```sh
/Users/<user>/Desktop/soniCs.app/Contents/MacOS/daw-launcher --print-config
open /Users/<user>/Desktop/soniCs.app
tail -n 120 ~/Library/Logs/DAW/launcher.log
```

Note:
- a fresh clone will still need an `AppIcon.icns` copied into `tools/packaging/macos/local_app_icon/` before plain packaging picks it up, because that lane is intentionally ignored.
