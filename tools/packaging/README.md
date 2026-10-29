# Release producer containment

The maintained Registry preparation owner supplies `RELEASE_PREPARATION_ROOT`
and a fixed release target. `contained_producer.py` copies the exact frozen
Git inputs into an attempt-owned working directory, runs the existing Make
gate there, and verifies input bytes and executable modes afterward. Compiler
output, relative test fixtures and vendored-library output stay in that attempt;
tests use the owner's `TMPDIR` for temporary files.

`make/release-native.mk` exposes the fixed signing, notarization and final
archive phases to the Registry authentication owner. Its initial audit builds
the public package in the bound release root. Later phases retain that exact
app and the Accepted submission identity. `native_auth.py` writes checksum and
manifest sidecars from the final archive. The Registry supplies the selected
signing identity, notary profile and explicit login Keychain route; invoke the
maintained `releasectl` workflow for credential effects and continuation.

The public package contains the licensed instrument banks and public seed
configuration. Private `artifacts/`, `music_projects/` and runtime state remain
outside the source inventory and package. Release preparation does not replace
the installed standard or Main Edit app.
