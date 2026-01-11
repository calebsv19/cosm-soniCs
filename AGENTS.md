# Repository Guidelines

## Project Structure & Module Organization
- `src/` contains the C implementation (app, engine, audio, input, UI, effects).
- `include/` mirrors `src/` with public headers (e.g., `include/engine/`, `include/ui/`).
- `assets/` holds runtime assets (UI art, fonts, etc.).
- `config/` stores default configuration files.
- `tests/` contains C test harnesses (serialization, cache, overlap, smoke).
- `docs/` includes design notes and QA checklists.
- Build outputs land in `build/` (binary: `build/daw_app`).

## Build, Test, and Development Commands
- `make` — builds the app into `build/daw_app`.
- `make clean` — removes `build/`.

## Coding Style & Naming Conventions
- C style: 4-space indentation, braces on the same line as control statements.
- Prefer `snake_case` for functions/variables and `UpperCamelCase` for structs.
- Keep UI constants grouped and named with clear prefixes (e.g., `EQ_DETAIL_*`).
- Follow existing layout/renderer patterns in `src/ui/` and `src/input/`.

## Testing Guidelines
- Tests are plain C executables in `tests/`.
- Name tests by intent (e.g., `session_serialization_test.c`).
- Run targeted tests with the `make test-*` targets above; run more than one when touching engine/session logic.

## Commit & Pull Request Guidelines
- ONLY MAKE A COMMIT AFTER ASKING EXPLICIT PERMISSION TO DO SO
- Recent commits use short, Title Case summaries (e.g., “Spectra Analysis Functional”).
- Keep commits focused and descriptive; avoid bundling unrelated changes.
- PRs should include:
  - A short summary of changes.
  - Test results (commands run).
  - Screenshots or short clips for UI changes (effects panel, EQ, timeline).

## Configuration Tips
- Check `config/` for defaults before adding new runtime settings.
- Assets referenced by UI should live under `assets/` with stable paths.

## Runtime Programming
- Always add a short one sentence summary for structs and methods to reference what their contents are or do. 
- Create plans in numbered steps for reference in future messages.
- After big changes update readme docs in the project dirs to reflect changes
- Before returning summary of completed work, always run 'make' to see if system compiles and fix any errors if possible, if errors are unresolved or complex give a summary of what is failing