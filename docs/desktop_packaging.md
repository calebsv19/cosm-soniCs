# Desktop Packaging

DAW (`soniCs`) supports standardized macOS app-bundle packaging and release notarization via Makefile targets.

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
