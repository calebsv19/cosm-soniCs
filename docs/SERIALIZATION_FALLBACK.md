# Serialization Fallback Baseline

This DAW build includes a public-safe fallback session template so startup remains deterministic even when local saved sessions are absent.

## Startup Load Order

On launch, the app attempts session restore in this order:

1. last remembered project path (`config/projects/last_project.txt`)
2. last autosave (`config/last_session.json`)
3. public fallback template (`config/templates/public_default_project.json`)
4. in-memory fresh state bootstrap (final fallback)

## Public Release Intent

- Personal project/session files are intentionally excluded from public commits.
- The fallback template contains no private project history and no user media references.
- Local users can still create/save projects normally; those files remain local unless explicitly committed.

## Updating the Template

If UI/session schema evolves, update `config/templates/public_default_project.json` to keep first-run behavior stable and compatible.

