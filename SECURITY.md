# Security Notes

This DAW is alpha software and currently optimized for trusted local desktop
use.

## Current Trust Model
- Single-user local desktop workflows.
- Trusted projects, trusted local audio assets, and trusted runtime roots.
- Local build/run tooling executed in user context.

## Boundary Considerations
- Opening untrusted projects can expose command/config/input surfaces. R4
  hardens current local path boundaries, but it does not claim sandboxed or
  broadly untrusted-project safety.
- Project/session `output_root` and `library_copy_root` values are refused when
  they target unsafe app-bundle write roots.
- Drag/drop import and library rename paths reject unsafe path components
  before writing inside the active library-copy or library directory.
- Audio recording writes generated WAV files under `<output_root>/recordings/`
  and falls back away from unsafe recording roots.
- Package and release helpers honor local environment overrides for package
  destinations, icon sources, signing identity, and notarization profile.
  Destructive package/release destinations are basename-guarded before remove
  operations.
- Plugin/extension style integrations should be treated as trusted-only.
- Do not run the DAW as root/administrator.

## Recommended Safe Usage
- Use trusted repositories and audio assets.
- Review project/session `data_paths` before opening files from untrusted
  sources.
- Keep package/release helper environment variables pointed at local trusted
  paths.
- Keep regular backups and commit frequently.
- Keep your OS/toolchain dependencies updated.

## Public Artifact Boundary
- App bundles include only the `assets/audio/README.md` placeholder from
  `assets/audio/`; ignored local user audio files are excluded from public
  packages.
- App bundles include allowlisted public/default config resources and exclude
  generated runtime config, `last_session.json`, and local project state.
- Runtime-generated caches, local session/project state, and packaging local
  icon inputs are gitignored.
- Package/release audits fail if local user audio, generated session/project
  state, or private planning docs appear in the app bundle.
- Manual packaged-app microphone proof remains separate from automated package
  and smoke gates.
