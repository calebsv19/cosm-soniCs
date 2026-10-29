# Reusable user presets

## 1. Library and project ownership

The custom-preset sequence is implemented in persistent DAW Main Edit. The personal library stores reusable sounds; every project stores its own complete instrument values through the existing session contract. Loading a user preset copies its base voice and all twenty-seven physical parameters into the selected MIDI region or track using the common instrument action. Library changes, rename or deletion do not change existing projects. There is no automatic link or sync back to a library sound.

By default, files live at `$HOME/Music/soniCs/User Library/Presets`. Set `DAW_PRESET_LIBRARY` to an absolute directory to use an alternate library, including a private test library. Read-only listing does not create directories. The initial browser keeps at most 128 entries, scans at most 4096 directory entries, and reports invalid files and truncation. A directly addressed valid preset remains available through MCP even if outside the cached list. Search covers names and derived categories, with ASCII-insensitive matching; Unicode names are preserved.

Each `<opaque-id>.sonics-preset.json` contains format version 5, synthesis version 1, a stable ID, a revision, a printable UTF-8 name of at most 95 bytes, base voice, derived category and every parameter, including dormant values. Version-one files with fourteen complete parameters remain readable with disabled filter modulation; version two stores all eighteen values; version three stores all twenty-four values, including percussion controls; version four stores all twenty-seven values including articulation settings; version five stores all twenty-nine values including sampled-instrument dynamics. Missing fields in older formats use factory defaults. Older formats cannot claim the appended percussion voices. Names are labels, so duplicate names and slashes are supported. Importing these portable files means copying them into the library with their original ID filename. Renaming the file alone is invalid. Unsupported future formats/voices are rejected instead of falling back to a different sound. This is complete parameter recall for the current engine; it does not freeze an executable or promise bit-identical sound across future DSP changes. A synthesis-version migration policy must accompany incompatible future behavior.

Library publication reuses the app's checked atomic save facade. Explicit Update requires the current ID and revision, preserves one `.previous` file, then publishes a new revision. Cooperating writers use a nonblocking per-library lock. A stale update leaves the current sound untouched. The previous document can be recovered by copying it back to its original ID filename while writers are stopped; automatic recovery UI is future work. Publication with unavailable directory sync is reported as accepted but not fully durable, so callers must not blindly retry it under a new request ID. Library updates are external file operations and do not enter project undo history. The current safety contract coordinates application writers, rather than providing a transactional compare-and-swap against arbitrary manual file editors.

Preset reads are bounded to 16 KiB regular files without following preset-file symlinks. The library and Previews directory cannot themselves be symlinks. Unknown/duplicate fields, missing parameters, nonfinite values and unsupported ranges are rejected before application.

## 2. Instrument workflow

1. Select a MIDI region and open its Instrument pane. Shape the sound with existing physical controls.
2. Use **Save Preset As...**, enter a name and press Save or Enter. The captured project sound must still match when publication occurs.
3. Open **My Presets**, search by name/category and select a sound. Arrow keys select visible rows; Enter loads; Escape closes. Refresh reloads the cached library.
4. Editing a loaded sound displays `*`. Use Save As for a separate reusable sound, or **Update Preset** for the explicitly loaded library entry. Update detects revision conflicts.
5. Use **Audition C4** or **Audition Chord** while transport and keyboard Test are stopped. The 1.5-second sample-counted gate continues to note-off if focus is lost. Repeated audition starts replace preview voices. Track mixer/effects still affect live audition; it authors no notes and adds no history.

The controls share native button presentation, text editing and application actions. In compact panes the preset controls occupy the header, preserving waveform space; larger panes have a separate action row. Errors and publication/durability feedback remain visible. The library is refreshed on opening, explicit Refresh and successful save, rather than scanning during drawing.

The loaded label/modified comparison is editor metadata. Projects currently persist the full sound rather than a custom name or durable library provenance. On reopen, a refreshed library can match an identical sound to its saved label; otherwise the base voice and modified indicator remain available. The sound itself is independent of that label lookup.

## 3. MCP adoption and standardized WAVs

The existing private owner-thread bridge now exposes forty-nine SDK tools, including the five preset tools described below. Five additions adopt the preset workflow:

| Tool | Admission and effect |
| --- | --- |
| `list_presets` | Read-only; name/category search and pages of 32 entries |
| `get_preset` | Read-only; complete values, optional expected revision |
| `save_preset` | Guarded write; exact target/state, new ID or explicit ID+revision update; project unchanged |
| `apply_preset` | Guarded write; exact target/state and library revision; one reserved instrument undo entry |
| `preview_preset` | Guarded write; fixed isolated note/chord WAV publication for a specified revision; project/device unchanged |

Use `capabilities`, `inspect_project`, `get_instrument` and its opaque `expected_state` before target writes. All writes require explicit endpoint write admission, the current instance and a fresh request ID. Immutable retries and `request_status` recovery reuse the existing 128-attempt lifetime receipt ledger. Reads remain available at read-only endpoints. Preview requires write admission because it publishes a file.

Preview uses the production instrument source in isolated state: three seconds, 48 kHz stereo PCM16, velocity 0.5, C4 or notes 48/55/60/64, a 1.5-second gate and natural release. It uses original level, no normalization, no project automation or effects, and rejects nonfinite/clipping samples. Peak and RMS come from the float source before PCM quantization. Artifacts live under `Previews/<id>-<revision>-note|chord.wav`; repeated requests for the same sound/mode produce identical PCM on the qualified engine. Preview is a deliberately narrow production sound renderer, not full project bounce, spectral analysis or MIDI authoring.

Configure the MCP client using [the existing setup](../tools/mcp/README.md). Building source does not register new tools in an already-running client or replace an installed Desktop package.

## 4. Verification and delivery boundaries

`make BUILD_DIR=<fresh-root> test-preset-library` covers exact all-parameter roundtrip, duplicate labels, revision updates/backups, stale state, locking, future formats, symlink refusal, undo/redo, repeated auditions, deterministic original-level WAVs, project reopen after library deletion, and browser text/Enter handling. `test-mcp-presets` qualifies SDK discovery/calls, strict reads, write admission, receipt replay, explicit updates, independent project copies and PCM output. Existing action/lifecycle/instrument/MCP musical-loop tests remain applicable.

Native qualification exercises actual product painters and Vulkan clip containment, plus actual `App_Run` save, preview, apply, normal shutdown and reopen without the fixture library. Task-owned runtime data is private. Retained proof and listening WAVs live in `build/receipts/user-presets-20261009/`; the qualified source-built executable remains under `build/preset-library-20261009/targets/macOS-arm64/toolchains/clang/bin/daw_app` for user audition. Disposable compiler/test/sanitizer output is retired after consumers finish.

Physical listening quality and sustained real-device performance remain human/device acceptance. No commit, release, installed app replacement or global MCP configuration is part of this slice. This feature merits a minor-version review in a later authorized release; VERSION remains 0.4.0 here. Next work can focus on bounded signal analysis, then transactional MIDI authoring and its MCP adoption.

The following [sound-inspection slice](sound_inspection.md) now shares the preview renderer with calibrated analysis, reference comparisons and two read-only MCP tools. That historical checkpoint had fourteen tools; current discovery has 49, including composition capture and MIDI authoring.

## Current packaged closeout (2026-10-11)

The preset workflow is included in the normal `soniCs Main Edit.app` package.
The current adapter registers 49 tools, including the five preset operations,
sound analysis, musical authoring, and four region-automation operations.
Earlier delivery exclusions above describe the original qualification checkpoint.
Current package checks use a private project/library to exercise save, explicit
update, apply, standardized WAV preview, instrument undo/redo and native save/reopen.
Preset labels/library provenance remain editor metadata; complete sound settings
persist independently. Sampled instruments and sample-asset packaging are the next
separate development lane, not part of parameter-preset completion.

Final installed-package acceptance passed: 49-tool discovery, list/get, save/update,
immutable replay/receipt recovery, note/chord PCM preview, apply/undo/redo, native
Save, normal quit, and exact sound reopen after removing the private library.
The normal Night Pocket project remained byte-for-byte unchanged. Compact proof:
`/Users/calebsv/Desktop/CodeWork/daw/artifacts/preset-closeout/20261011/`.
The parameter-preset MCP lane is complete; sampled violin work is subsequent.

## Sampled violin extension (2026-10-11)

Preset format v5 appends Dynamics and Velocity Timbre (29 parameters). Formats v1–v4 keep their original 14/18/24/27 parameter counts. The `sampled_violin` voice recalls the fixed bundled `vsco2_solo_violin_v1` bank; user presets do not copy WAV assets. See [sampled violin](sampled_violin.md).

Piano expression extension: [piano_expression.md](piano_expression.md) describes the three piano-only controls, binary region/live sustain, 88 string modes, recorded key/pedal mechanics, 32-field v6 presets and session v25. Older preset versions 1–5 and session v24 remain readable; existing non-piano voices keep their behavior.
