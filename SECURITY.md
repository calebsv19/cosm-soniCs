# Security Notes

This DAW is alpha software and currently optimized for trusted local use.

## Current Trust Model
- Single-user local desktop workflows.
- Trusted projects and trusted local assets.
- Local build/run tooling executed in user context.

## Boundary Considerations
- Opening untrusted projects can expose command/config/input surfaces.
- Plugin/extension style integrations should be treated as trusted-only.
- Do not run the DAW as root/administrator.

## Recommended Safe Usage
- Use trusted repositories and audio assets.
- Keep regular backups and commit frequently.
- Keep your OS/toolchain dependencies updated.

