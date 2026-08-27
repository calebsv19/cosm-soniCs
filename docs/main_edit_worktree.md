# Persistent Main Edit Worktree

DAW uses the CodeWork MEW1 topology for functional development without
turning the canonical checkout or canonical desktop package into an active
scratch lane.

## Lane identities

- canonical source: catalog-declared canonical branch in the primary `daw/`
  checkout
- persistent integration lane: `<workspace>/_worktrees/daw_main_edit`
- integration branch: `codex/daw-main-edit`
- canonical package: `soniCs.app`, `com.cosm.sonics`
- development package: `soniCs Main Edit.app`,
  `com.cosm.sonics.main-edit`
- canonical mutable namespaces: `DAW`
- development mutable namespaces: `DAW-Main-Edit`

The development package is written under
`build/targets/<target-triple>/dist/dev/main-edit/`. It carries
`Contents/Resources/build_identity.json` using
`codework_local_development_build_identity_v1`. That identity binds the
package to the branch, commit, dirty state, complete source fingerprint,
architecture, toolchain, program version, and packaged binary digest.

## Start and readback gate

From `<workspace>`:

```sh
python3 shared/scripts/mew1/mew1.py lane-state \
  --canonical-root daw \
  --main-edit-root _worktrees/daw_main_edit
```

Before edits, record both lane commits, `VERSION`, cleanliness, ahead/behind
counts, staged/unstaged/untracked paths, and the registered worktree inventory.
Stop on an unexpected branch, base, owner, or dirty path.

One writer owns the persistent Main Edit checkout at a time. Serial feature
work may happen directly there. A genuinely independent or risky feature may
use a specialist branch from the accepted base, but it must reconcile into
Main Edit before canonical adoption.

## Checkpoint gate

Run from the Main Edit checkout:

```sh
git diff --check
make clean
make
make run-headless-smoke
make package-desktop-main-edit-self-test
```

The package target captures the source fingerprint before and after assembly
and discards only its generated Main Edit bundle if the source changes. The
self-test verifies the generic identity envelope, exact source and binary
digests, bundle identity, runtime/log namespaces, architecture, resources,
and code signature.

`package-desktop-main-edit-refresh` is a separate host-required operation. It
audits for a running `soniCs Main Edit` process and refuses replacement when
one is found. It never targets `soniCs.app`. `package-desktop-main-edit-open`
is an explicit GUI action and is not part of status or package self-test.

## Integration gate

Before canonical adoption:

1. classify any canonical-only commits
2. merge expected canonical drift into Main Edit
3. rerun the affected focused, build, headless, and Main Edit package gates
4. adopt Main Edit into canonical by fast-forward when possible, or by a
   reviewed merge when history legitimately diverged
5. independently read back the final canonical commit and cleanliness

Do not rebase a shared persistent lane merely to make history linear.

## Retain or recycle

Retaining the clean named lane after adoption is the default. Before any
recycle, prove it is clean, has no untracked user-owned bytes, has no running
process owner, and has no unique unreachable commits. Preserve specialist
worktrees and ignored evidence. Never reset, clean, force-remove, or repurpose
the lane to recover its name.

## Explicit exclusions

Main Edit proof is local development evidence only. It does not change
`VERSION`, create a release candidate, mutate Production Registry, publish,
deploy, push, or authorize replacement or closure of a running app.
