# Guarded MIDI region authoring

## 1. Start without manual preparation

Read `capabilities` and `get_midi_layout`, then call `create_midi_region` with the endpoint instance, a fresh UUID request ID, `expected_layout`, and canonical decimal strings `start_frame` and `duration_frames`. Omit `target` to create one new trailing track and its first empty MIDI region atomically. Supply a current `track` handle to use an existing lane. The response contains `created_target` for instrument and note actions. Creation reserves history before publication and produces one undo command.

Use `get_instrument` / `set_instrument` to choose the sound, followed by `get_midi_region` / `create_midi_pattern` or `set_midi_notes`. For example, at 48 kHz and 120 BPM, create 96,000 frames, add a four-beat C-major melody, extend to 192,000 frames and append an arpeggio at beat four. Regions remain editable through the existing native MIDI editor and common note actions.

## 2. Extend and replay safely

`extend_midi_region` takes the current region's exact `expected_state` and a **new total** `duration_frames`, not an increment. It preserves position, notes and sound. Only untrimmed regions with zero source offset can extend. Shortening and overlap with any neighboring audio or MIDI region are refused. Touching boundaries is allowed. Equal duration returns an unchanged receipt without history.

Use `undo_midi_region` / `redo_midi_region` with a fresh layout token and the original region handle for structural history. Redo accepts the absent created region and restores the original track/clip identities, allowing later note and instrument commands to replay. Each tool consumes only its matching command at the shared history cursor; use note or instrument history tools when those commands are at the cursor. A created track can be removed only while it is the final track, contains only its pristine created region, and retains the captured track settings, name, EQ and empty effects/automation. Unrelated user changes refuse undo and retain history.

## 3. Exact state and limits

Layout reads copy generation, ordered track identities, all audio/MIDI placements, sample rate and the complete tempo map. Notes, instrument settings, mixer values and names are outside the layout precondition. Creation and structural history compare the full current layout; extension instead compares complete MIDI state and checks current neighbors. All writes require stopped transport, recording and Test, with no active editor gesture or reserved continuous edit.

The structural surface accepts at most 128 tracks, 2,048 placements and 1,024 tempo events. Reads page 64 tracks, 128 placements and 64 tempo events with independent `offset`, `clip_offset`, `tempo_offset` cursors. `-1` ends each stream. Reuse `expected_layout` across pages; changed or expired tokens refuse the read. Four exact layout snapshots are retained independently of the eight MIDI snapshots. Projects exceeding these bounds refuse rather than truncate. C callers obtain their expected layout through `daw_action_get_midi_layout`.

Frames must fit the signed timeline range and duration must be positive. All writes retain the existing same-user opt-in endpoint, 16 KiB request / 128 KiB response bounds and immutable 128-receipt ledger. Refusals and unchanged outcomes also consume receipts. After a lost response, query `request_status` using the original instance and request ID before taking another action. Per-send SIGPIPE suppression keeps disconnected clients from terminating the application.

## 4. Scope and qualification

The official-SDK server now exposes 24 tools, including five structural MIDI tools. Common C tests exercise atomic publication and history failures, overlap, overflow, busy states, existing-track metadata, exact identity replay and owner-thread refusal. SDK tests compose from zero tracks, undo fully to empty, redo, compare rendered audio and reopen a saved project. Native App_Run and actual restart use private runtime data. Source qualification does not update an installed application or register a client.

Arbitrary moving, trimming, deletion, batch arrangements, project save/open/export and transport MCP controls remain future slices. Session schema, VERSION, shared modules and dependencies remain unchanged. This is additive functionality for a later minor release decision. Atlas was considered: these actions extend the existing DAW authoring/control workflow without a new shared adoption or topology edge.
