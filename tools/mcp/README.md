# soniCs local MCP baseline

1. Install the isolated SDK environment from the repository root (Python 3.11+):

   ```sh
   python3 -m venv build/mcp-venv
   build/mcp-venv/bin/python -m pip install -r tools/mcp/requirements.lock
   ```

   The lock records the complete tested Python 3.14/macOS dependency set, including
   official `mcp==1.30.0`. Other platforms/Python versions require their own qualification.

2. Build the DAW, then launch the resulting binary with an explicit private endpoint.
   The example uses the verified task build; the default `make` binary lives under
   `build/targets/macOS-arm64/toolchains/clang/bin/daw_app` on this host.

   ```sh
   control_dir="/tmp/sonics-control-$UID"
   mkdir -p -m 700 "$control_dir"
   export DAW_CONTROL_SOCKET="$control_dir/control.sock"
   DAW_CONTROL_ALLOW_WRITE=1 build/agent-actions-20261008/targets/macOS-arm64/toolchains/clang/bin/daw_app
   ```

   Omit `DAW_CONTROL_ALLOW_WRITE=1` for read-only access. Without
   `DAW_CONTROL_SOCKET` the app creates no endpoint. Startup refuses an existing
   socket/path, an unsafe parent directory, or a socket path of 104 bytes or more.
   Clean shutdown removes only the socket inode created by that app instance.
   After a crash, establish that the old app is stopped before removing its stale socket.
   The endpoint grants same-user clients access to the enabled tools; it is not a
   per-client approval or isolation boundary. No TCP listener is created.

3. Generate the client configuration with the **same** endpoint environment:

   ```sh
   python3 tools/mcp/client_config.py
   ```

   This prints a common `mcpServers` JSON example with absolute paths. Adapt it to
   your client's stdio MCP configuration; the generator does not edit global
   client settings. Configure/restart the client to discover tools. Merely building
   this repository does not add new callable tools to an already-running chat.

4. Use the tools in this order:

   - `capabilities`: get endpoint instance, presets/defaults, parameter ranges and
     which controls are visible for each preset. Visibility is UI metadata, not a
     promise that every DSP parameter has equal audible influence in every preset.
   - `inspect_project`: obtain track and MIDI-region targets. Page with `next_offset`
     until it is `-1`; repeat discovery if the user changes arrangement structure.
   - `get_instrument`: read stored and effective settings and `expected_state`.
   - `set_instrument`: supply capability `instance`, a fresh UUID `request_id`, the
     copied target, `expected_state`, optional preset ID, and parameter dictionary.
     One accepted batch produces one undo entry; an exact no-op produces none.
   - `undo_instrument` / `redo_instrument`: supply a fresh request ID and current
     expected state. Only a matching instrument command at the history cursor is
     eligible; these cannot undo unrelated edits or skip history commands.
   - `request_status`: recover the terminal response using the original instance
     and request ID after a timeout or disconnect. Identical set/history calls
     replay their original result; changed payloads with the same ID reject.

   Every domain response has `ok` and `code`; check both. Protocol/connection errors
   use MCP errors. `expected_state` is an opaque canonical copy of the addressed
   instrument state, including endpoint identity and effective inheritance, **not** a whole-project
   revision or an edit counter. A change away and back to identical state matches.
   Region movement/removal and project replacement invalidate old targets.

   Receipts are in memory, limited to 128 admitted mutation attempts (including
   rejected attempts). They never evict earlier outcomes. A full ledger refuses
   new IDs until a new endpoint lifetime; already-retained IDs remain queryable.
   Restart changes `instance` and loses receipts. A stale instance cannot prove
   whether a previous edit was saved; reconcile from the current project instead
   of automatically retrying. Replay returns historical accepted state, so use a
   fresh `get_instrument` to inspect current state after later changes.

5. Run the integration test after SDK setup:

   ```sh
   make BUILD_DIR=build/agent-actions-20261008 test-mcp-control
   ```

   This negotiates MCP with an official SDK client against the production bridge
   and engine in an isolated C fixture. It needs permission to bind local Unix
   sockets. It does not open user projects, audio devices, or an installed app.
   The ordinary headless smoke suite remains independent of Python dependencies
   and local socket permission. See [application control](../../docs/application_control.md).

The baseline intentionally exposes instrument operations only. Transport/mixer,
effects, arrangement/MIDI authoring, importing/downloading media, project open/save,
render/export jobs and audio analysis are subsequent application-action slices.
No physical listening, native UI acceptance, installation or release is implied
by the software integration test. MCP follows each validated application feature.


The [instrument-controls refinement](../../docs/instrument_controls.md) adds
preset-specific supported `visible_parameters` and `control_scale` metadata.
Writes still use physical values, not normalized UI knob positions. Use
`make BUILD_DIR=build/synth-controls-20261008 test-mcp-instrument-loop` to exercise
a real rendered C-major phrase with exact undo/redo output restoration. Its
fixture exports are test-only; no render tool is added to the MCP surface.

The additive [rounded family](../../docs/rounded_synth.md) is discovered as
`rounded_keys`, `rounded_pluck` and `rounded_pad`. Select one with an empty
parameter object to apply its factory defaults when changing preset, then edit
its ten supported controls through the existing guarded action surface. The
musical test now checks these selections and undo/redo, and exports a held pad
chord in addition to the shared eight-note phrase. No tool signature changes.
