# Desktop Packaging

DAW now supports standardized macOS app-bundle packaging via Makefile targets.

## Build Package

```sh
make package-desktop
```

Output:

- `dist/DAW.app`

## Validate Package (Automated)

```sh
make package-desktop-smoke
make package-desktop-self-test
```

`package-desktop-self-test` validates:
- launcher/binary/plist lanes
- config/audio/font/shader resources
- resolved launcher runtime roots

## Desktop Copy + Refresh Flow

```sh
make package-desktop-copy-desktop
make package-desktop-sync
make package-desktop-remove
make package-desktop-refresh
```

Default desktop destination:

- `$(HOME)/Desktop/DAW.app`

## Open Packaged App

```sh
make package-desktop-open
```

## Launcher Runtime Model

`daw-launcher`:
- sets `VK_RENDERER_SHADER_ROOT=<app>/Contents/Resources` when unset
- switches cwd to `<app>/Contents/Resources` before launching `daw-bin`
- logs startup to `~/Library/Logs/DAW/launcher.log` (fallback `${TMPDIR}/daw-launcher.log`)

Launcher diagnostics:
- `--self-test` validates bundled files/resources
- `--print-config` prints resolved roots/log path without launching UI

## Recommended Final Validation
1. `make -C daw package-desktop-self-test`
2. `make -C daw package-desktop-refresh`
3. `/Users/<user>/Desktop/DAW.app/Contents/MacOS/daw-launcher --print-config`
4. `open /Users/<user>/Desktop/DAW.app`
5. `tail -n 120 ~/Library/Logs/DAW/launcher.log`
